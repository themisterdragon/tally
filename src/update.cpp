#include "update.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "datapack.h"
#include "net.h"
#include "util.h"

namespace update {

using net::J;
using net::quote_arg;
using net::run_curl;

namespace {

const char *BLS_URL = "https://api.bls.gov/publicAPI/v1/timeseries/data/";

}  // namespace

bool curl_available() { return net::curl_available(); }

std::vector<std::string> price_series() {
    // Must match scripts/build_data.py. All regions are always requested together.
    static const char *cpi[] = {"SA0", "SAF11", "SEFV", "SAH1", "SAH2", "SETB01", "SAT", "SAM", "SAA", "SAE", "SAR", "SEHF01"};
    static const char *ap_regional[] = {"74714", "72610", "72620"};
    static const char *ap_national[] = {"708111", "709112", "702111", "703112", "706111"};
    static const char *areas[] = {"0000", "0100", "0200", "0300", "0400"};
    std::vector<std::string> ids;
    for (auto a : areas) {
        for (auto c : cpi) ids.push_back(std::string("CUUR") + a + c);
        for (auto p : ap_regional) ids.push_back(std::string("APU") + a + p);
    }
    for (auto p : ap_national) ids.push_back(std::string("APU0000") + p);
    return ids;
}

std::string describe_requests(const std::string &pack_url) {
    auto ids = price_series();
    return "Prices: " + std::to_string((ids.size() + 24) / 25) + " requests to " + BLS_URL +
           " asking for " + std::to_string(ids.size()) +
           " public price series (all U.S. regions, the same list for everyone).\n"
           "Tax tables: 1 download of " + pack_url + "\n"
           "Not sent: your ZIP, state, income, expenses, loans, name, or anything you typed.";
}

Outcome update_prices() {
    Outcome res;
    if (!curl_available()) { res.message = "curl isn't installed, so Tally can't download. Your built-in data still works."; return res; }
    auto ids = price_series();
    int year = util::today().y;
    std::string body_path = util::join(util::data_dir(), "bls-request.json");
    std::string lines;
    int count = 0;
    for (size_t start = 0; start < ids.size(); start += 25) {
        std::string body = "{\"seriesid\":[";
        for (size_t k = start; k < ids.size() && k < start + 25; k++) body += (k > start ? ",\"" : "\"") + ids[k] + "\"";
        body += "],\"startyear\":\"" + std::to_string(year - 2) + "\",\"endyear\":\"" + std::to_string(year) + "\"}";
        if (!util::write_file_atomic(body_path, body, false)) { res.message = "Couldn't write to the data folder."; return res; }
        std::string reply;
        bool ok = run_curl("-H \"Content-Type: application/json\" --data-binary @" + quote_arg(body_path) + " --proto =https --proto-redir =https " + BLS_URL, reply);
        std::remove(body_path.c_str());
        if (!ok) { res.message = "Couldn't reach the BLS (no internet?). Nothing changed."; return res; }
        bool bad = false;
        J root = net::parse_json(reply, bad);
        if (bad || root["status"].s != "REQUEST_SUCCEEDED") {
            res.message = "The BLS said no (it allows about 25 requests a day per network). Try again tomorrow.";
            return res;
        }
        for (const J &s : root["Results"]["series"].a) {
            std::vector<std::string> pts;
            for (const J &d : s["data"].a) {
                const std::string &per = d["period"].s, &val = d["value"].s;
                if (per.size() != 3 || per[0] != 'M' || per == "M13" || val.empty() || val == "-") continue;
                pts.insert(pts.begin(), d["year"].s + "-" + per.substr(1) + "=" + val);  // BLS lists newest first
            }
            if (pts.empty()) continue;
            lines += "series " + s["seriesID"].s;
            for (const auto &p : pts) lines += " " + p;
            lines += "\n";
            count++;
        }
    }
    if (!count) { res.message = "The download had no prices in it. Nothing changed."; return res; }
    util::write_file_atomic(datapack::prices_path(), "# Downloaded " + util::stamp_now() + " from the BLS\n" + lines, false);
    datapack::reload();
    res.ok = true;
    res.message = "Prices updated: " + std::to_string(count) + " series from the Bureau of Labor Statistics.";
    return res;
}

static Outcome accept_pack(const std::string &text, const std::string &how) {
    Outcome res;
    DataPack p;
    std::string err;
    if (!p.parse(text, err)) { res.message = err + " Nothing changed."; return res; }
    const DataPack &cur = datapack::active();
    if (p.tax_year < cur.tax_year || (p.tax_year == cur.tax_year && p.date <= cur.date)) {
        res.ok = true;
        res.message = "You already have the newest tax tables (" + std::to_string(cur.tax_year) + ", " + cur.date + ").";
        return res;
    }
    if (!util::make_dirs(util::data_dir()) || !util::write_file_atomic(datapack::saved_path(), text, false)) {
        res.message = "Couldn't save the data pack.";
        return res;
    }
    datapack::reload();
    res.ok = true;
    res.message = "Tax tables " + how + ": " + std::to_string(p.tax_year) + " (built " + p.date + ").";
    return res;
}

Outcome update_pack(const std::string &url) {
    Outcome res;
    if (!curl_available()) { res.message = "curl isn't installed, so Tally can't download."; return res; }
    if (!util::starts_with(url, "https://")) { res.message = "The update address must start with https://"; return res; }
    if (!net::safe_url(url)) { res.message = "The update address has a character that can't be in a web address."; return res; }
    std::string text;
    // https only, even if the server redirects somewhere else
    if (!run_curl("--proto =https --proto-redir =https " + quote_arg(url), text)) { res.message = "Couldn't download the data pack (no internet, or it isn't published yet)."; return res; }
    return accept_pack(text, "updated");
}

Outcome import_pack(const std::string &path) {
    std::string text;
    if (!util::read_file(path, text)) return {false, "Couldn't open " + path};
    return accept_pack(text, "imported");
}

Outcome export_pack(const std::string &path) {
    if (!util::write_file_atomic(path, datapack::active().text(), false)) return {false, "Couldn't write " + path};
    return {true, "Saved the data pack to " + path};
}

}  // namespace update
