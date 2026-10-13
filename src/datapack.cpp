#include "datapack.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "util.h"

namespace {
#include "data/datapack.inc"

std::vector<Bracket> parse_brackets(std::istringstream &in) {
    std::vector<Bracket> out;
    std::string tok;
    while (in >> tok) {
        if (tok == "none") continue;
        size_t c = tok.find(':');
        if (c == std::string::npos) break;
        out.push_back({std::atof(tok.substr(0, c).c_str()), std::atof(tok.substr(c + 1).c_str())});
    }
    return out;
}

int filing_index(const std::string &k) {
    return k == "single" ? 0 : k == "joint" ? 1 : k == "hoh" ? 2 : k == "separate" ? 3 : -1;
}

std::string br_text(const std::vector<Bracket> &b) {
    if (b.empty()) return "none";
    std::string o;
    for (const auto &x : b) o += (o.empty() ? "" : " ") + util::fixed(x.over, 0) + ":" + util::fixed(x.rate, 5);
    return o;
}
}  // namespace

bool DataPack::parse(const std::string &text, std::string &error) {
    *this = DataPack();
    bool header = false;
    for (const auto &raw : util::split(text, '\n')) {
        std::string line = util::trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (!header) {
            if (kind != "tally-datapack") { error = "This isn't a Tally data pack."; return false; }
            header = true;
        } else if (kind == "date") in >> date;
        else if (kind == "taxyear") in >> tax_year;
        else if (kind == "source") { std::getline(in, source); source = util::trim(source); }
        else if (kind == "fed") {
            std::string what;
            in >> what;
            if (what == "std") {
                std::string k; double v;
                while (in >> k >> v) { int i = filing_index(k); if (i >= 0) fed.std_ded[i] = v; }
            } else if (what == "brackets") {
                std::string k;
                in >> k;
                int i = filing_index(k);
                if (i >= 0) fed.brackets[i] = parse_brackets(in);
            } else if (what == "payroll" || what == "credits") {
                std::string k; double v;
                while (in >> k >> v) {
                    if (k == "ss_rate") fed.ss_rate = v;
                    else if (k == "ss_base") fed.ss_base = v;
                    else if (k == "med_rate") fed.med_rate = v;
                    else if (k == "addl_rate") fed.addl_rate = v;
                    else if (util::starts_with(k, "addl_")) { int i = filing_index(k.substr(5)); if (i >= 0) fed.addl_threshold[i] = v; }
                    else if (k == "ctc") fed.ctc = v;
                    else if (k == "ctc_refundable") fed.ctc_refundable = v;
                    else if (k == "odc") fed.odc = v;
                    else if (k == "phase_single") fed.phase_single = v;
                    else if (k == "phase_joint") fed.phase_joint = v;
                }
            }
        } else if (kind == "state") {
            std::string code, what;
            in >> code >> what;
            StateTax &s = states[code];
            if (what == "single") s.single = parse_brackets(in);
            else if (what == "joint") s.joint = parse_brackets(in);
            else if (what == "std") {
                int cr = 0, a = 0, b = 0, c = 0;
                std::string ex, pay;
                in >> s.std_single >> s.std_joint >> cr >> ex >> s.ex_single >> a >> s.ex_joint >> b >> s.ex_dep >> c >> pay >> s.payroll_pct;
                s.std_credit = cr;
                s.ex_single_credit = a;
                s.ex_joint_credit = b;
                s.ex_dep_credit = c;
            }
        } else if (kind == "series") {
            std::string id, tok;
            in >> id;
            auto &pts = series[id];
            while (in >> tok) {
                int y = 0, m = 0;
                double v = 0;
                size_t eq = tok.find('=');
                if (eq == std::string::npos || std::sscanf(tok.c_str(), "%d-%d", &y, &m) != 2) continue;
                v = std::atof(tok.c_str() + eq + 1);
                pts.push_back({y, m, v});
            }
        }
    }
    if (!header) { error = "The file is empty."; return false; }
    if (fed.brackets[0].empty() || states.size() < 51) { error = "The data pack is missing tax tables."; return false; }
    return true;
}

std::string DataPack::text() const {
    static const char *fk[] = {"single", "joint", "hoh", "separate"};
    std::string o = "tally-datapack 1\ndate " + date + "\ntaxyear " + std::to_string(tax_year) + "\nsource " + source + "\n";
    o += "fed std";
    for (int i = 0; i < 4; i++) o += std::string(" ") + fk[i] + " " + util::fixed(fed.std_ded[i], 0);
    o += "\n";
    for (int i = 0; i < 4; i++) o += std::string("fed brackets ") + fk[i] + " " + br_text(fed.brackets[i]) + "\n";
    o += "fed payroll ss_rate " + util::fixed(fed.ss_rate, 4) + " ss_base " + util::fixed(fed.ss_base, 0) +
         " med_rate " + util::fixed(fed.med_rate, 4) + " addl_rate " + util::fixed(fed.addl_rate, 4);
    for (int i = 0; i < 4; i++) o += std::string(" addl_") + fk[i] + " " + util::fixed(fed.addl_threshold[i], 0);
    o += "\nfed credits ctc " + util::fixed(fed.ctc, 0) + " ctc_refundable " + util::fixed(fed.ctc_refundable, 0) +
         " odc " + util::fixed(fed.odc, 0) + " phase_single " + util::fixed(fed.phase_single, 0) +
         " phase_joint " + util::fixed(fed.phase_joint, 0) + "\n";
    for (const auto &kv : states) {
        const StateTax &s = kv.second;
        o += "state " + kv.first + " single " + br_text(s.single) + "\n";
        o += "state " + kv.first + " joint " + br_text(s.joint) + "\n";
        o += "state " + kv.first + " std " + util::fixed(s.std_single, 0) + " " + util::fixed(s.std_joint, 0) + " " +
             (s.std_credit ? "1" : "0") + " ex " + util::fixed(s.ex_single, 0) + " " + (s.ex_single_credit ? "1" : "0") + " " +
             util::fixed(s.ex_joint, 0) + " " + (s.ex_joint_credit ? "1" : "0") + " " + util::fixed(s.ex_dep, 0) + " " +
             (s.ex_dep_credit ? "1" : "0") + " payroll " + util::fixed(s.payroll_pct, 3) + "\n";
    }
    for (const auto &kv : series) {
        o += "series " + kv.first;
        char buf[64];
        for (const auto &p : kv.second) {
            std::snprintf(buf, sizeof buf, " %d-%02d=%s", p.y, p.m, util::fixed(p.v, 3).c_str());
            o += buf;
        }
        o += "\n";
    }
    return o;
}

namespace datapack {

namespace {
DataPack g_builtin, g_active;
bool g_loaded = false;

void merge_prices(DataPack &into, const DataPack &fresh) {
    // A price file only replaces a series when its newest month is newer.
    for (const auto &kv : fresh.series) {
        auto &old = into.series[kv.first];
        auto key = [](const std::vector<Point> &v) { return v.empty() ? 0 : v.back().y * 12 + v.back().m; };
        if (key(kv.second) >= key(old)) old = kv.second;
    }
}
}  // namespace

std::string saved_path() { return util::join(util::data_dir(), "datapack.txt"); }
std::string prices_path() { return util::join(util::data_dir(), "prices.txt"); }

const DataPack &built_in() {
    if (g_builtin.date.empty()) {
        std::string text, err;
        for (const char **l = DATAPACK_LINES; *l; l++) { text += *l; text += '\n'; }
        g_builtin.parse(text, err);
        g_builtin.origin = "built in";
    }
    return g_builtin;
}

bool load_saved(DataPack &out) {
    std::string text, err;
    if (!util::read_file(saved_path(), text)) return false;
    if (!out.parse(text, err)) return false;
    return true;
}

void reload() {
    g_active = built_in();
    DataPack saved;
    if (load_saved(saved) && (saved.tax_year > g_active.tax_year ||
                              (saved.tax_year == g_active.tax_year && saved.date > g_active.date))) {
        DataPack base = g_active;
        g_active = saved;
        g_active.origin = "updated " + saved.date;
        merge_prices(g_active, base);
    }
    // prices.txt holds just "series" lines from a direct price update
    std::string text;
    if (util::read_file(prices_path(), text)) {
        DataPack p;
        for (const auto &line : util::split(text, '\n')) {
            if (!util::starts_with(line, "series ")) continue;
            std::istringstream in(line);
            std::string kind, id, tok;
            in >> kind >> id;
            auto &pts = p.series[id];
            while (in >> tok) {
                int y = 0, m = 0;
                size_t eq = tok.find('=');
                if (eq != std::string::npos && std::sscanf(tok.c_str(), "%d-%d", &y, &m) == 2)
                    pts.push_back({y, m, std::atof(tok.c_str() + eq + 1)});
            }
        }
        merge_prices(g_active, p);
    }
    g_loaded = true;
}

const DataPack &active() {
    if (!g_loaded) reload();
    return g_active;
}

}  // namespace datapack
