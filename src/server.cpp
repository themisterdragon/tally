#include "server.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "net.h"
#include "util.h"

namespace server {

using net::J;

namespace {

const char *API = "/api/tally/v1/";
const int PROTOCOL = 1;

bool safe_token(const std::string &s, size_t max) {
    if (s.empty() || s.size() > max) return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

std::string base(const std::string &url) {
    std::string u = util::trim(url);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

// host part of "scheme://host:port/path"
std::string host_of(const std::string &url) {
    size_t p = url.find("://");
    std::string rest = p == std::string::npos ? url : url.substr(p + 3);
    size_t end = rest.find_first_of("/?#");
    if (end != std::string::npos) rest = rest.substr(0, end);
    if (!rest.empty() && rest[0] == '[') return util::lower(rest.substr(1, rest.find(']') - 1));   // IPv6
    size_t colon = rest.rfind(':');
    if (colon != std::string::npos) rest = rest.substr(0, colon);
    return util::lower(rest);
}

bool ends_with(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool home_network(const std::string &h) {
    if (h == "localhost" || h == "::1" || ends_with(h, ".local") || ends_with(h, ".lan") || ends_with(h, ".home.arpa") || ends_with(h, ".internal"))
        return true;
    int a = -1, b = -1, c = -1, d = -1;
    char extra;
    if (std::sscanf(h.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) == 4) {
        if (a == 127 || a == 10) return true;
        if (a == 192 && b == 168) return true;
        if (a == 172 && b >= 16 && b <= 31) return true;
        if (a == 169 && b == 254) return true;
        if (a == 100 && b >= 64 && b <= 127) return true;   // Tailscale / carrier-grade NAT
        return false;
    }
    if (util::starts_with(h, "fd") || util::starts_with(h, "fe80")) return h.find(':') != std::string::npos;   // private IPv6
    return !h.empty() && h.find('.') == std::string::npos && h.find(':') == std::string::npos;   // bare name like "myserver"
}

// The pairing code goes in a private curl config file instead of on the command line,
// so other programs on this computer can't read it from the process list.
struct CodeFile {
    std::string path;
    explicit CodeFile(const std::string &code) {
        if (code.empty()) return;
        util::make_dirs(util::data_dir());
        path = util::join(util::data_dir(), "server-request.cfg");
        if (!util::write_file_atomic(path, "header = \"Authorization: Bearer " + code + "\"\n", true)) path.clear();
    }
    ~CodeFile() { if (!path.empty()) std::remove(path.c_str()); }
};

std::string status_problem(int status) {
    if (status == 0) return "Couldn't reach your server. Is it running, and is this computer on the same network?";
    if (status == 401 || status == 403) return "The server didn't accept the pairing code. Get a new one from the server's Settings page.";
    if (status == 413) return "The server sent far more than any purchase list could be, so Tally stopped. Nothing changed.";
    if (status == 404) return "That address answered, but it isn't a Tally server (no " + std::string(API) + " there).";
    if (status >= 300 && status < 400) return "The server tried to send Tally somewhere else. Use the exact address it shows (maybe https://).";
    return "The server had a problem (HTTP " + std::to_string(status) + "). Nothing changed.";
}

std::string match_category(const Budget &b, const std::string &name) {
    for (const auto &c : b.categories)
        if (c.name == name) return c.name;
    for (const auto &c : b.categories)
        if (util::lower(c.name) == util::lower(name)) return c.name;
    return name.empty() ? "Groceries" : "Other";
}

// Adds what's new from one reply. Shared by the network and the file import.
// Returns false (and sets err) only when the reply isn't Tally link data at all.
bool take(Budget &b, const J &root, Outcome &res, std::string &err) {
    if (root.t != J::OBJ || root["purchases"].t != J::ARR) { err = "That isn't Tally purchase data."; return false; }
    if (root["tally_link"].num() > PROTOCOL) { err = "Your server is newer than this Tally. Update Tally, then try again."; return false; }
    std::set<std::string> have;
    for (const auto &p : b.purchases) if (!p.id.empty()) have.insert(p.id);
    for (const J &j : root["purchases"].a) {
        Purchase p;
        p.id = util::cut(j["id"].s, 64);
        if (!p.id.empty() && have.count(p.id)) continue;
        p.name = util::cut(util::trim(j["name"].s), 80);
        p.amount = j["amount"].num();
        if (p.name.empty() || !(p.amount > -100000 && p.amount < 100000)) continue;
        p.date = valid_date(j["date"].s) ? j["date"].s : today_iso();
        p.category = match_category(b, j["category"].s);
        p.store = util::cut(util::trim(j["store"].s), 60);
        if (!p.id.empty()) have.insert(p.id);
        b.purchases.push_back(p);
        res.added++;
        res.added_total += p.amount;
    }
    const J &food = root["food"];
    if (food.t == J::OBJ && food["month"].s.size() == 7) {
        b.link.food_month = food["month"].s;
        b.link.food_wasted = std::max(0.0, food["wasted"].num());
        b.link.food_saved = std::max(0.0, food["saved_by_freezing"].num());
    }
    return true;
}

std::string added_text(const Outcome &r) {
    if (!r.added) return "No new purchases.";
    return "Added " + std::to_string(r.added) + (r.added == 1 ? " purchase" : " purchases") + " (" + util::money(r.added_total) +
           "). Find them on Prices > B (Bought).";
}

}  // namespace

bool valid_date(const std::string &d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit((unsigned char)d[i])) return false;
    int m = std::atoi(d.c_str() + 5), day = std::atoi(d.c_str() + 8);
    return m >= 1 && m <= 12 && day >= 1 && day <= 31;
}

std::string today_iso() {
    util::Date t = util::today();
    char s[16];
    std::snprintf(s, sizeof s, "%04d-%02d-%02d", t.y, t.m, t.d);
    return s;
}

bool code_ok(const std::string &code) { return safe_token(code, 128); }

std::string address_problem(const std::string &url) {
    std::string u = base(url);
    if (u.empty()) return "Type your server's address, for example http://192.168.1.20:8484";
    if (!net::safe_url(u)) return "The address has a character that can't be in a web address.";
    if (util::starts_with(u, "https://")) return host_of(u).empty() ? "The address is missing the server's name." : "";
    if (util::starts_with(u, "http://")) {
        if (home_network(host_of(u))) return "";
        return "Plain http:// only works for a server in your home network. For anything else, use https:// "
               "so your pairing code and purchases travel encrypted.";
    }
    return "The address must start with http:// (home network) or https://";
}

std::string describe(const ServerLink &l) {
    return "Tally will ask " + base(l.url) + " for purchases added since last time.\n\n"
           "Sent: your pairing code and a bookmark number your server gave Tally last time.\n"
           "Not sent: your income, expenses, loans, ZIP, or anything else in your budget. Tally only downloads.\n\n"
           "Received: what you bought (date, name, price, category, store) and this month's food totals "
           "(expired food vs. money saved by freezing).";
}

Outcome check(const ServerLink &l) {
    Outcome res;
    if (!net::curl_available()) { res.message = "curl isn't installed, so Tally can't reach a server. Import from a file instead."; return res; }
    std::string problem = address_problem(l.url);
    if (!problem.empty()) { res.message = problem; return res; }
    if (!code_ok(l.code)) { res.message = "Type the pairing code from your server's Settings page first."; return res; }
    CodeFile cf(l.code);
    std::string out;
    int status = net::fetch(base(l.url) + API + "hello", cf.path, out);
    if (status != 200) { res.message = status_problem(status); return res; }
    bool bad = false;
    J root = net::parse_json(out, bad);
    if (bad || root["tally_link"].t != J::NUM) { res.message = "That address answered, but not like a Tally server."; return res; }
    std::string name = util::cut(root["name"].s, 40);
    res.ok = true;
    res.message = "Connected" + (name.empty() ? std::string() : " to " + name) + ". The pairing code works.";
    return res;
}

Outcome pull(Budget &b) {
    Outcome res;
    ServerLink &l = b.link;
    if (!l.on) { res.message = "The server link is off. Turn it on in Settings > Server link."; return res; }
    if (!net::curl_available()) { res.message = "curl isn't installed, so Tally can't reach a server. Import from a file instead."; return res; }
    std::string problem = address_problem(l.url);
    if (!problem.empty()) { res.message = problem; return res; }
    if (!code_ok(l.code)) { res.message = "Type the pairing code from your server's Settings page first."; return res; }
    CodeFile cf(l.code);
    // A server sends a page at a time and says "more": keep going, but never forever.
    for (int page = 0; page < 50; page++) {
        std::string url = base(l.url) + API + "purchases";
        if (safe_token(l.cursor, 64)) url += "?since=" + l.cursor;
        std::string out, err;
        int status = net::fetch(url, cf.path, out);
        if (status != 200) {
            res.message = status_problem(status);
            if (res.added) res.message = added_text(res) + " Then: " + res.message;
            return res;
        }
        bool bad = false;
        J root = net::parse_json(out, bad);
        if (bad || !take(b, root, res, err)) { res.message = bad ? "The server's reply was garbled. Nothing more changed." : err; return res; }
        std::string next = root["cursor"].s;
        bool more = root["more"].s == "true" && safe_token(next, 64) && next != l.cursor;
        if (safe_token(next, 64)) l.cursor = next;
        if (!more) break;
    }
    l.last_sync = util::stamp_now();
    res.ok = true;
    res.message = added_text(res);
    return res;
}

Outcome import_file(Budget &b, const std::string &path) {
    Outcome res;
    std::string text, err;
    if (!util::read_file(path, text)) { res.message = "Couldn't open " + path; return res; }
    bool bad = false;
    J root = net::parse_json(text, bad);
    if (bad) { res.message = "That file isn't readable purchase data (it should be the .json your server exports)."; return res; }
    if (!take(b, root, res, err)) { res.message = err; return res; }
    res.ok = true;
    res.message = added_text(res);
    return res;
}

}  // namespace server
