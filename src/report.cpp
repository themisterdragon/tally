#include "report.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>

#include "budget.h"
#include "loans.h"
#include "util.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace report {

namespace {

std::string dated(const std::string &folder, const std::string &name, const std::string &ext) {
    util::Date d = util::today();
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.y, d.m, d.d);
    return util::join(folder, name + " " + buf + "." + ext);
}

bool ready(const std::string &folder, std::string &error) {
    if (!util::make_dirs(folder)) {
        error = "Couldn't create the folder " + folder;
        return false;
    }
    return true;
}

struct LoanLine {
    const Loan *loan;
    double payment;
    loans::Payoff base, with_extra;
};

std::vector<LoanLine> loan_lines(const Budget &b) {
    std::vector<LoanLine> out;
    for (const auto &l : b.loans) {
        LoanLine x{&l, l.monthly_payment(), {}, {}};
        x.base = loans::simulate(l.balance, l.apr, x.payment);
        x.with_extra = loans::simulate(l.balance, l.apr, x.payment, l.extra, l.lump);
        out.push_back(x);
    }
    return out;
}

std::string months_text(const loans::Payoff &p) {
    if (p.never) return "never";
    int y = p.months / 12, m = p.months % 12;
    if (!y) return std::to_string(m) + " mo";
    return std::to_string(y) + " yr" + (m ? " " + std::to_string(m) + " mo" : "");
}

std::string pct_signed(double f) { return (f >= 0 ? "+" : "") + util::percent(f, 1); }

}  // namespace

std::string export_folder(const Budget &b) {
    if (!b.settings.export_dir.empty()) return b.settings.export_dir;
    return util::join(util::documents_dir(), "Tally");
}

// =============================================================================
// Plain text
// =============================================================================

std::string text_report(const Budget &b, int W) {
    budget::Summary s = budget::summarize(b);
    std::string o;
    auto line = [&](const std::string &t = "") { o += t + "\n"; };
    auto rule = [&]() { line(std::string(W, '-')); };
    auto row = [&](const std::string &label, const std::string &value) {
        line(util::fit(label, W - 16) + util::pad_left(value, 16));
    };
    line("TALLY - HOUSEHOLD BUDGET");
    line(util::date_text(util::today()) + (s.place.found ? "   " + s.place.county + ", " + s.place.state + " " + s.place.zip : ""));
    rule();
    row("Take-home pay (monthly)", util::money(s.income_month));
    row("Spending (monthly)", util::money(s.spend_month));
    row(s.left_month >= 0 ? "Left over (monthly)" : "Short (monthly)", util::money(s.left_month));
    line();
    line("WHERE THE MONEY GOES (monthly)");
    double maxv = 0;
    for (const auto &c : s.cats) maxv = std::max(maxv, c.monthly);
    int barw = std::max(10, W - 46);
    for (const auto &c : s.cats) {
        int n = maxv > 0 ? (int)std::lround(c.monthly / maxv * barw) : 0;
        std::string pct = s.spend_month > 0 ? util::percent(c.monthly / s.spend_month, 0) : "";
        line(util::fit(c.name, 17) + " " + util::fit(std::string(n, '#'), barw) + " " + util::pad_left(util::money(c.monthly, false), 10) +
             " " + util::pad_left(pct, 5));
    }
    line();
    line("EXPENSES");
    line(util::fit("Name", W - 54) + util::fit("Category", 16) + util::fit("How often", 15) + util::pad_left("Amount", 11) + util::pad_left("Monthly", 12));
    rule();
    std::vector<const Expense *> ex;
    for (const auto &e : b.expenses) ex.push_back(&e);
    std::stable_sort(ex.begin(), ex.end(), [](const Expense *a, const Expense *b) { return a->category < b->category; });
    for (auto e : ex)
        line(util::fit(e->name + (e->need ? "" : " (want)"), W - 54) + util::fit(e->category, 16) + util::fit(freq_name(e->freq), 15) +
             util::pad_left(util::money(e->amount), 11) + util::pad_left(util::money(e->monthly()), 12));
    if (ex.empty()) line("(none yet)");
    line();
    line("INCOME AND TAXES (yearly estimate, " + std::to_string(datapack::active().tax_year) + " tables)");
    rule();
    row("Gross pay", util::money(s.tax.gross + s.tax.untaxed));
    row("Pre-tax deductions (401k, health)", "-" + util::money(s.tax.pretax));
    row("Federal income tax", "-" + util::money(s.tax.federal));
    row("Social Security + Medicare", "-" + util::money(s.tax.social_security + s.tax.medicare + s.tax.self_employment));
    row("State income tax (" + (s.tax.state_code.empty() ? std::string("?") : s.tax.state_code) + ")", "-" + util::money(s.tax.state + s.tax.state_payroll));
    if (s.tax.local > 0) row("Local income tax", "-" + util::money(s.tax.local));
    if (s.tax.after_tax_deductions > 0) row("After-tax deductions", "-" + util::money(s.tax.after_tax_deductions));
    row("Take-home pay", util::money(s.tax.net));
    for (const auto &p : s.tax.earners)
        line("  " + p.name + ": about " + util::money(p.per_check()) + " per paycheck (" + util::fixed(p.checks, 0) + " a year)");
    auto ll = loan_lines(b);
    if (!ll.empty()) {
        line();
        line("LOANS");
        rule();
        for (const auto &x : ll) {
            line(x.loan->name + " (" + x.loan->kind + "): " + util::money(x.loan->balance) + " at " + util::fixed(x.loan->apr, 2) + "%, " +
                 util::money(x.payment) + "/mo");
            line("  Paid off in " + months_text(x.base) + ", total interest " + util::money(x.base.interest));
            if (x.loan->extra > 0 || x.loan->lump > 0)
                line("  With extra: " + months_text(x.with_extra) + ", saves " + util::money(x.base.interest - x.with_extra.interest) + " in interest");
        }
    }
    if (!b.wants.empty()) {
        line();
        line("WANTS (real hourly pay " + (s.real_hourly > 0 ? util::money(s.real_hourly) : std::string("unknown")) + ")");
        rule();
        for (const auto &w : b.wants) {
            auto m = budget::want_math(s, w.price);
            line(util::fit(w.name, W - 16) + util::pad_left(util::money(w.price), 16));
            for (const auto &x : util::wrap(m.verdict, W - 6)) line("      " + x);
        }
    }
    if (!s.price_month.empty()) {
        line();
        line("PRICES (last 12 months, " + places::region_name(s.region) + ", to " + s.price_month + ")");
        rule();
        row("Overall inflation", pct_signed(s.all_items_yoy));
        for (const auto &c : s.cats)
            if (c.has_yoy) row("  " + c.name, pct_signed(c.yoy));
        row("This budget a year from now (monthly)", util::money(s.next_year_month));
    }
    if (!s.tips.empty()) {
        line();
        line("CHECKUPS");
        rule();
        for (const auto &t : s.tips) {
            line(std::string(t.mood == budget::GOOD ? "[ok]  " : t.mood == budget::OKAY ? "[--]  " : "[!!]  ") + t.title);
            for (const auto &w : util::wrap(t.detail, W - 6)) line("      " + w);
        }
    }
    line();
    line("Estimates for planning, not tax advice. Made with Tally.");
    return o;
}

std::string txt(const Budget &b, const std::string &folder, std::string &error) {
    if (!ready(folder, error)) return "";
    std::string path = dated(folder, "Tally Budget", "txt");
    if (!util::write_file_atomic(path, text_report(b), true)) { error = "Couldn't write " + path; return ""; }
    return path;
}

// =============================================================================
// CSV
// =============================================================================

static std::string csv_cell(const std::string &s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
    return o + "\"";
}

std::string csv(const Budget &b, const std::string &folder, std::string &error) {
    if (!ready(folder, error)) return "";
    std::string o = "Name,Category,Need or Want,Amount,How Often,Times per Year,Monthly,Yearly,Due Day,Notes\n";
    for (const auto &e : b.expenses)
        o += csv_cell(e.name) + "," + csv_cell(e.category) + "," + (e.need ? "Need" : "Want") + "," + util::fixed(e.amount, 2) + "," +
             csv_cell(freq_name(e.freq)) + "," + util::fixed(per_year(e.freq), 0) + "," + util::fixed(e.monthly(), 2) + "," +
             util::fixed(e.yearly(), 2) + "," + (e.due_day ? std::to_string(e.due_day) : "") + "," + csv_cell(e.notes) + "\n";
    for (const auto &l : b.loans)
        if (l.in_budget)
            o += csv_cell(l.name + " (loan)") + "," + (l.kind == "Mortgage" ? "Housing" : "Debt & Loans") + ",Need," +
                 util::fixed(l.monthly_payment() + l.extra, 2) + ",Monthly,12," + util::fixed(l.monthly_payment() + l.extra, 2) + "," +
                 util::fixed((l.monthly_payment() + l.extra) * 12, 2) + ",,\n";
    std::string path = dated(folder, "Tally Expenses", "csv");
    if (!util::write_file_atomic(path, o, true)) { error = "Couldn't write " + path; return ""; }
    return path;
}

std::string loan_schedule_csv(const Budget &b, size_t i, const std::string &folder, std::string &error) {
    if (i >= b.loans.size()) { error = "No loan selected."; return ""; }
    if (!ready(folder, error)) return "";
    const Loan &l = b.loans[i];
    std::string o = "Month,Date,Payment,Interest,Principal,Balance\n";
    auto rows = loans::schedule(l.balance, l.apr, l.monthly_payment(), l.extra, l.lump);
    for (const auto &r : rows)
        o += std::to_string(r.month) + "," + l.payment_month(r.month) + "," + util::fixed(r.payment, 2) + "," +
             util::fixed(r.interest, 2) + "," + util::fixed(r.principal, 2) + "," + util::fixed(r.balance, 2) + "\n";
    std::string safe;
    for (char c : l.name) safe += (std::isalnum((unsigned char)c) || c == ' ' || c == '-') ? c : '_';
    std::string path = dated(folder, "Tally Payoff - " + safe, "csv");
    if (!util::write_file_atomic(path, o, true)) { error = "Couldn't write " + path; return ""; }
    return path;
}

// =============================================================================
// ZIP (store only) for .xlsx
// =============================================================================

namespace {

uint32_t crc32(const std::string &d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : d) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Zip {
    std::string out, central;
    int count = 0;
    static void u16(std::string &s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); }
    static void u32(std::string &s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); }
    void add(const std::string &name, const std::string &data) {
        uint32_t crc = crc32(data), off = (uint32_t)out.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0x21);
        u32(out, crc); u32(out, (uint32_t)data.size()); u32(out, (uint32_t)data.size());
        u16(out, (uint16_t)name.size()); u16(out, 0);
        out += name;
        out += data;
        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0x21);
        u32(central, crc); u32(central, (uint32_t)data.size()); u32(central, (uint32_t)data.size());
        u16(central, (uint16_t)name.size()); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, off);
        central += name;
        count++;
    }
    std::string finish() {
        uint32_t cd_off = (uint32_t)out.size();
        std::string r = out + central;
        u32(r, 0x06054b50); u16(r, 0); u16(r, 0); u16(r, (uint16_t)count); u16(r, (uint16_t)count);
        u32(r, (uint32_t)central.size()); u32(r, cd_off); u16(r, 0);
        return r;
    }
};

std::string xml(const std::string &s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default:
                if ((unsigned char)c >= 32 || c == '\t' || c == '\n') o += c;
        }
    }
    return o;
}

std::string col(int c) {  // 0 -> A
    std::string s;
    c++;
    while (c) { s.insert(s.begin(), (char)('A' + (c - 1) % 26)); c = (c - 1) / 26; }
    return s;
}

// Style ids (see styles.xml below)
enum { XS_NORMAL = 0, XS_BOLD = 1, XS_HEAD = 2, XS_MONEY = 3, XS_PCT = 4, XS_BOLD_MONEY = 5, XS_TITLE = 6, XS_NOTE = 7, XS_CAT0 = 8 };

struct Sheet {
    std::string name;
    std::string rows;
    std::vector<double> widths;
    int r = 0;
    std::string cur;
    int c = 0;
    void row() { if (r) rows += "<row r=\"" + std::to_string(r) + "\">" + cur + "</row>"; r++; c = 0; cur.clear(); }
    std::string ref() const { return col(c) + std::to_string(r); }
    void text(const std::string &t, int style = XS_NORMAL) {
        if (!t.empty()) cur += "<c r=\"" + ref() + "\" s=\"" + std::to_string(style) + "\" t=\"inlineStr\"><is><t>" + xml(t) + "</t></is></c>";
        c++;
    }
    void num(double v, int style = XS_MONEY) {
        cur += "<c r=\"" + ref() + "\" s=\"" + std::to_string(style) + "\"><v>" + util::fixed(v, 6) + "</v></c>";
        c++;
    }
    void formula(const std::string &f, double cached, int style = XS_MONEY) {
        cur += "<c r=\"" + ref() + "\" s=\"" + std::to_string(style) + "\"><f>" + xml(f) + "</f><v>" + util::fixed(cached, 6) + "</v></c>";
        c++;
    }
    void skip() { c++; }
    std::string finish() {
        row();
        std::string cols;
        if (!widths.empty()) {
            cols = "<cols>";
            for (size_t i = 0; i < widths.size(); i++)
                cols += "<col min=\"" + std::to_string(i + 1) + "\" max=\"" + std::to_string(i + 1) + "\" width=\"" + util::fixed(widths[i], 1) + "\" customWidth=\"1\"/>";
            cols += "</cols>";
        }
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
               "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>" +
               cols + "<sheetData>" + rows + "</sheetData></worksheet>";
    }
};

}  // namespace

std::string xlsx(const Budget &b, const std::string &folder, std::string &error) {
    if (!ready(folder, error)) return "";
    budget::Summary s = budget::summarize(b);
    std::map<std::string, int> cat_style;
    for (size_t i = 0; i < b.categories.size(); i++) cat_style[b.categories[i].name] = XS_CAT0 + (int)i;
    auto cstyle = [&](const std::string &name) {
        auto it = cat_style.find(name);
        return it == cat_style.end() ? XS_NORMAL : it->second;
    };

    // --- Expenses -------------------------------------------------------------
    Sheet ex;
    ex.name = "Expenses";
    ex.widths = {28, 18, 9, 12, 16, 9, 12, 12, 8, 30};
    ex.row();
    for (auto h : {"Name", "Category", "Need?", "Amount", "How often", "Per year", "Monthly", "Yearly", "Due day", "Notes"}) ex.text(h, XS_HEAD);
    for (const auto &e : b.expenses) {
        ex.row();
        std::string rr = std::to_string(ex.r);
        ex.text(e.name);
        ex.text(e.category, cstyle(e.category));
        ex.text(e.need ? "Need" : "Want");
        ex.num(e.amount);
        ex.text(freq_name(e.freq));
        ex.num(per_year(e.freq), XS_NORMAL);
        ex.formula("D" + rr + "*F" + rr + "/12", e.monthly());
        ex.formula("D" + rr + "*F" + rr, e.yearly());
        if (e.due_day) ex.num(e.due_day, XS_NORMAL); else ex.skip();
        ex.text(e.notes);
    }
    for (const auto &l : b.loans) {
        if (!l.in_budget || l.balance <= 0) continue;
        ex.row();
        std::string rr = std::to_string(ex.r);
        std::string cat = l.kind == "Mortgage" ? "Housing" : "Debt & Loans";
        double pay = l.monthly_payment() + l.extra;
        ex.text(l.name + " (loan)");
        ex.text(cat, cstyle(cat));
        ex.text("Need");
        ex.num(pay);
        ex.text("Monthly");
        ex.num(12, XS_NORMAL);
        ex.formula("D" + rr + "*F" + rr + "/12", pay);
        ex.formula("D" + rr + "*F" + rr, pay * 12);
        ex.skip();
        ex.text("From the Loans tab");
    }
    int last_ex = std::max(2, ex.r);
    ex.row();
    ex.text("Total", XS_BOLD);
    ex.skip(); ex.skip(); ex.skip(); ex.skip(); ex.skip();
    ex.formula("SUM(G2:G" + std::to_string(last_ex) + ")", s.spend_month, XS_BOLD_MONEY);
    ex.formula("SUM(H2:H" + std::to_string(last_ex) + ")", s.spend_month * 12, XS_BOLD_MONEY);
    std::string exr = "Expenses!$G$2:$G$" + std::to_string(last_ex), excat = "Expenses!$B$2:$B$" + std::to_string(last_ex);
    std::string exneed = "Expenses!$C$2:$C$" + std::to_string(last_ex);

    // --- Income ---------------------------------------------------------------
    Sheet in;
    in.name = "Income";
    in.widths = {34, 16, 16};
    in.row();
    in.text("Income and taxes (" + std::to_string(datapack::active().tax_year) + " estimate)", XS_HEAD);
    in.text("Per year", XS_HEAD);
    in.text("Per month", XS_HEAD);
    auto line = [&](const std::string &label, double v, int style = XS_MONEY) {
        in.row();
        in.text(label, style == XS_BOLD_MONEY ? XS_BOLD : XS_NORMAL);
        in.num(v, style);
        in.formula("B" + std::to_string(in.r) + "/12", v / 12, style);
    };
    line("Gross pay", s.tax.gross + s.tax.untaxed);
    line("Pre-tax deductions (401k, health)", -s.tax.pretax);
    line("Federal income tax", -s.tax.federal);
    line("Social Security", -s.tax.social_security);
    line("Medicare", -s.tax.medicare);
    if (s.tax.self_employment > 0) line("Self-employment tax", -s.tax.self_employment);
    line("State income tax", -(s.tax.state + s.tax.state_payroll));
    if (s.tax.local > 0) line("Local income tax", -s.tax.local);
    if (s.tax.after_tax_deductions > 0) line("After-tax deductions", -s.tax.after_tax_deductions);
    int first = 2, last = in.r;
    in.row();
    in.text("Take-home pay", XS_BOLD);
    in.formula("SUM(B" + std::to_string(first) + ":B" + std::to_string(last) + ")", s.tax.net, XS_BOLD_MONEY);
    in.formula("B" + std::to_string(in.r) + "/12", s.tax.net / 12, XS_BOLD_MONEY);
    std::string takehome = "Income!$C$" + std::to_string(in.r);
    in.row();
    in.row();
    in.text("Paychecks", XS_HEAD);
    in.text("Per paycheck", XS_HEAD);
    in.text("Paychecks a year", XS_HEAD);
    for (const auto &p : s.tax.earners) {
        in.row();
        in.text(p.name);
        in.num(p.per_check());
        in.num(p.checks, XS_NORMAL);
    }
    in.row();
    in.row();
    in.text("Estimates for planning, not tax advice.", XS_NOTE);

    // --- Summary --------------------------------------------------------------
    Sheet su;
    su.name = "Summary";
    su.widths = {30, 14, 12, 16, 16};
    su.row();
    su.text("Tally budget - " + util::date_text(util::today()), XS_TITLE);
    su.row();
    su.text(s.place.found ? s.place.county + ", " + s.place.state + " " + s.place.zip : "", XS_NOTE);
    su.row();
    su.row();
    su.text("Take-home pay (monthly)", XS_BOLD);
    su.formula(takehome, s.income_month, XS_BOLD_MONEY);
    int th_row = su.r;
    su.row();
    su.text("Spending (monthly)", XS_BOLD);
    su.formula("SUM(" + exr + ")", s.spend_month, XS_BOLD_MONEY);
    int sp_row = su.r;
    su.row();
    su.text("Left over (monthly)", XS_BOLD);
    su.formula("B" + std::to_string(th_row) + "-B" + std::to_string(sp_row), s.left_month, XS_BOLD_MONEY);
    su.row();
    su.text("Needs / wants", XS_NOTE);
    su.formula("SUMIF(" + exneed + ",\"Need\"," + exr + ")", s.needs, XS_MONEY);
    su.formula("SUMIF(" + exneed + ",\"Want\"," + exr + ")", s.wants, XS_MONEY);
    su.row();
    su.row();
    for (auto h : {"Category", "Monthly", "Share", "Price change (12 mo)", "Monthly next year"}) su.text(h, XS_HEAD);
    int cat_first = su.r + 1;
    for (const auto &c : s.cats) {
        su.row();
        std::string rr = std::to_string(su.r);
        su.text(c.name, cstyle(c.name));
        su.formula("SUMIF(" + excat + ",A" + rr + "," + exr + ")", c.monthly);
        su.formula("IF(B$" + std::to_string(sp_row) + "=0,0,B" + rr + "/B$" + std::to_string(sp_row) + ")",
                   s.spend_month > 0 ? c.monthly / s.spend_month : 0, XS_PCT);
        su.num(c.has_yoy ? c.yoy : 0, XS_PCT);
        su.formula("B" + rr + "*(1+D" + rr + ")", c.monthly * (1 + (c.has_yoy ? c.yoy : 0)));
    }
    int cat_last = su.r;
    su.row();
    su.text("Total", XS_BOLD);
    su.formula("SUM(B" + std::to_string(cat_first) + ":B" + std::to_string(cat_last) + ")", s.spend_month, XS_BOLD_MONEY);
    su.skip();
    su.skip();
    su.formula("SUM(E" + std::to_string(cat_first) + ":E" + std::to_string(cat_last) + ")", s.next_year_month, XS_BOLD_MONEY);
    su.row();
    su.row();
    su.text("Price changes: BLS consumer price index for your region. Change a number in Expenses and everything here updates.", XS_NOTE);

    // --- Loans ----------------------------------------------------------------
    Sheet lo;
    lo.name = "Loans";
    lo.widths = {24, 12, 13, 8, 12, 11, 14, 14, 16, 14};
    lo.row();
    for (auto h : {"Loan", "Kind", "Balance", "APR %", "Payment", "Extra/mo", "Payoff (months)", "Total interest", "With extra (months)", "Interest saved"})
        lo.text(h, XS_HEAD);
    for (const auto &x : loan_lines(b)) {
        lo.row();
        std::string rr = std::to_string(lo.r);
        lo.text(x.loan->name);
        lo.text(x.loan->kind);
        lo.num(x.loan->balance);
        lo.num(x.loan->apr, XS_NORMAL);
        lo.num(x.payment);
        lo.num(x.loan->extra);
        // NPER works in Excel, LibreOffice and Google Sheets, so these update if you edit the row
        if (x.base.never) lo.text("never"); else
            lo.formula("IF(D" + rr + "=0,C" + rr + "/E" + rr + ",NPER(D" + rr + "/1200,-E" + rr + ",C" + rr + "))", x.base.months, XS_NORMAL);
        lo.num(x.base.interest);
        if (x.with_extra.never) lo.text("never"); else lo.num(x.with_extra.months, XS_NORMAL);
        lo.num(x.base.interest - x.with_extra.interest);
    }
    // Payment schedules, one block per loan
    Sheet sc;
    sc.name = "Payoff Schedules";
    sc.widths = {24, 8, 10, 12, 12, 12, 14};
    sc.row();
    for (auto h : {"Loan", "Month", "Date", "Payment", "Interest", "Principal", "Balance"}) sc.text(h, XS_HEAD);
    for (const auto &l : b.loans) {
        for (const auto &r : loans::schedule(l.balance, l.apr, l.monthly_payment(), l.extra, l.lump)) {
            sc.row();
            sc.text(l.name);
            sc.num(r.month, XS_NORMAL);
            sc.text(l.payment_month(r.month));
            sc.num(r.payment);
            sc.num(r.interest);
            sc.num(r.principal);
            sc.num(r.balance);
        }
    }

    // --- styles ---------------------------------------------------------------
    std::string fills = "<fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
                        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF2B3A55\"/></patternFill></fill>";
    std::string xfs =
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"2\" borderId=\"0\" applyFont=\"1\" applyFill=\"1\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"164\" fontId=\"1\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"0\" applyFont=\"1\"/>";
    int fill_id = 3;
    for (const auto &c : b.categories) {
        fills += "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF" + util::hex256(c.color) + "\"/></patternFill></fill>";
        xfs += "<xf numFmtId=\"0\" fontId=\"" + std::string(util::is_dark256(c.color) ? "5" : "1") + "\" fillId=\"" +
               std::to_string(fill_id++) + "\" borderId=\"0\" applyFont=\"1\" applyFill=\"1\"/>";
    }
    std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"&quot;$&quot;#,##0.00;[Red]-&quot;$&quot;#,##0.00\"/>"
        "<numFmt numFmtId=\"165\" formatCode=\"0.0%\"/></numFmts>"
        "<fonts count=\"6\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"16\"/><name val=\"Calibri\"/></font>"
        "<font><i/><sz val=\"10\"/><color rgb=\"FF555555\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"" + std::to_string(fill_id) + "\">" + fills + "</fills>"
        "<borders count=\"1\"><border/></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"" + std::to_string(XS_CAT0 + b.categories.size()) + "\">" + xfs + "</cellXfs>"
        "</styleSheet>";

    std::vector<Sheet *> sheets = {&su, &ex, &in, &lo, &sc};
    std::string wb = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                     "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
    std::string rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    std::string types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    Zip z;
    for (size_t i = 0; i < sheets.size(); i++) {
        std::string n = std::to_string(i + 1);
        wb += "<sheet name=\"" + xml(sheets[i]->name) + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"/>";
        rels += "<Relationship Id=\"rId" + n + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet" + n + ".xml\"/>";
        types += "<Override PartName=\"/xl/worksheets/sheet" + n + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    }
    wb += "</sheets><calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>";
    rels += "<Relationship Id=\"rId99\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>";
    types += "</Types>";
    z.add("[Content_Types].xml", types);
    z.add("_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                         "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");
    z.add("xl/workbook.xml", wb);
    z.add("xl/_rels/workbook.xml.rels", rels);
    z.add("xl/styles.xml", styles);
    for (size_t i = 0; i < sheets.size(); i++) z.add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", sheets[i]->finish());

    std::string path = dated(folder, "Tally Budget", "xlsx");
    if (!util::write_file_atomic(path, z.finish(), true)) { error = "Couldn't write " + path; return ""; }
    return path;
}

// =============================================================================
// PDF: a printable page with color charts. Built-in Helvetica, US Letter.
// =============================================================================

namespace {

const short HELV[95] = {278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
                        556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778,
                        722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
                        278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
                        556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
const short HELVB[95] = {278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
                         556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778,
                         722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333,
                         278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
                         611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};

// UTF-8 -> the PDF's WinAnsi bytes (Latin-1 range; anything else becomes '?')
std::string ansi(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int cp, len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        if (i + len > s.size()) break;
        cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        if (cp == 0x2026) o += "...";
        else if (cp == 0x2014 || cp == 0x2013) o += '-';
        else if (cp == 0x2019 || cp == 0x2018) o += '\'';
        else if (cp == 0x201C || cp == 0x201D) o += '"';
        else if (cp < 256) o += (char)cp;
        else o += '?';
    }
    return o;
}

struct Pdf {
    std::vector<std::string> pages;
    std::string cur;
    double y = 0;
    static constexpr double PW = 612, PH = 792, M = 50;
    int page_no = 0;
    std::string footer;

    static double width(const std::string &s, double size, bool bold) {
        double w = 0;
        for (unsigned char c : ansi(s)) w += (c >= 32 && c < 127) ? (bold ? HELVB : HELV)[c - 32] : 556;
        return w * size / 1000.0;
    }
    static std::string esc(const std::string &s) {
        std::string o;
        for (char c : ansi(s)) {
            if (c == '(' || c == ')' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    }
    static std::string rgb(int c, bool fill) {
        int r, g, b;
        util::rgb256(c, r, g, b);
        return util::fixed(r / 255.0, 3) + " " + util::fixed(g / 255.0, 3) + " " + util::fixed(b / 255.0, 3) + (fill ? " rg\n" : " RG\n");
    }
    void new_page() {
        if (!cur.empty()) finish_page();
        page_no++;
        cur.clear();
        y = PH - M;
    }
    void finish_page() {
        text(M, 28, footer, 8, false, 244);
        std::string pn = "Page " + std::to_string(page_no);
        text(PW - M - width(pn, 8, false), 28, pn, 8, false, 244);
        pages.push_back(cur);
        cur.clear();
    }
    void need(double h) { if (y - h < M + 10) new_page(); }
    void text(double x, double yy, const std::string &s, double size, bool bold, int color = 236) {
        cur += rgb(color, true) + "BT /" + std::string(bold ? "F2" : "F1") + " " + util::fixed(size, 1) + " Tf " + util::fixed(x, 2) + " " +
               util::fixed(yy, 2) + " Td (" + esc(s) + ") Tj ET\n";
    }
    void text_right(double xr, double yy, const std::string &s, double size, bool bold, int color = 236) {
        text(xr - width(s, size, bold), yy, s, size, bold, color);
    }
    void rect(double x, double yy, double w, double h, int color) {
        cur += rgb(color, true) + util::fixed(x, 2) + " " + util::fixed(yy, 2) + " " + util::fixed(w, 2) + " " + util::fixed(h, 2) + " re f\n";
    }
    void rect_rgb(double x, double yy, double w, double h, double r, double g, double b) {
        cur += util::fixed(r, 3) + " " + util::fixed(g, 3) + " " + util::fixed(b, 3) + " rg " + util::fixed(x, 2) + " " + util::fixed(yy, 2) + " " +
               util::fixed(w, 2) + " " + util::fixed(h, 2) + " re f\n";
    }
    void hline(double x1, double x2, double yy, double gray = 0.75) {
        cur += util::fixed(gray, 2) + " G 0.6 w " + util::fixed(x1, 2) + " " + util::fixed(yy, 2) + " m " + util::fixed(x2, 2) + " " + util::fixed(yy, 2) + " l S\n";
    }
    std::string fit(const std::string &s, double maxw, double size, bool bold) {
        if (width(s, size, bold) <= maxw) return s;
        std::string t = s;
        while (!t.empty() && width(t + "...", size, bold) > maxw) util::pop_utf8(t);
        return t + "...";
    }
    void heading(const std::string &s) {
        need(40);
        y -= 18;
        text(M, y, s, 13, true, 24);
        y -= 6;
        hline(M, PW - M, y, 0.6);
        y -= 14;
    }
    void para(const std::string &s, double size = 9.5, int color = 238) {
        std::string line;
        double maxw = PW - 2 * M;
        for (const auto &w : util::split(s, ' ')) {
            std::string t = line.empty() ? w : line + " " + w;
            if (width(t, size, false) > maxw && !line.empty()) {
                need(size + 4);
                text(M, y, line, size, false, color);
                y -= size + 3;
                line = w;
            } else {
                line = t;
            }
        }
        if (!line.empty()) {
            need(size + 4);
            text(M, y, line, size, false, color);
            y -= size + 3;
        }
    }
    std::string build() {
        if (!cur.empty()) finish_page();
        std::vector<std::string> objs;
        // 1 catalog, 2 pages, 3 font, 4 bold font, then page/content pairs
        std::string kids;
        for (size_t i = 0; i < pages.size(); i++) kids += std::to_string(5 + 2 * i) + " 0 R ";
        objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
        objs.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
        for (size_t i = 0; i < pages.size(); i++) {
            objs.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " +
                           std::to_string(6 + 2 * i) + " 0 R >>");
            objs.push_back("<< /Length " + std::to_string(pages[i].size()) + " >>\nstream\n" + pages[i] + "endstream");
        }
        std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offs;
        for (size_t i = 0; i < objs.size(); i++) {
            offs.push_back(out.size());
            out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
        }
        size_t xref = out.size();
        out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
        for (size_t o : offs) {
            std::string n = std::to_string(o);
            out += std::string(10 - std::min<size_t>(10, n.size()), '0') + n + " 00000 n \n";
        }
        out += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root 1 0 R /Info << /Title (Tally Budget) /Producer (Tally) >> >>\nstartxref\n" +
               std::to_string(xref) + "\n%%EOF\n";
        return out;
    }
};

}  // namespace

std::string pdf(const Budget &b, const std::string &folder, std::string &error) {
    if (!ready(folder, error)) return "";
    budget::Summary s = budget::summarize(b);
    Pdf p;
    const double M = Pdf::M, W = Pdf::PW - 2 * Pdf::M;
    p.footer = "Tally - estimates for planning, not tax advice. " + util::date_text(util::today());
    p.new_page();

    // Title
    p.text(M, p.y - 20, "Household Budget", 22, true, 17);
    p.text_right(Pdf::PW - M, p.y - 12, util::date_text(util::today()), 10, false, 240);
    if (s.place.found) p.text_right(Pdf::PW - M, p.y - 26, s.place.county + ", " + s.place.state + " " + s.place.zip, 10, false, 240);
    p.y -= 50;

    // Three big numbers
    struct Box { const char *label; double v; int color; };
    Box boxes[3] = {{"Take-home pay / month", s.income_month, 28},
                    {"Spending / month", s.spend_month, 25},
                    {s.left_month >= 0 ? "Left over / month" : "Short / month", s.left_month, s.left_month >= 0 ? 28 : 124}};
    double bw = (W - 20) / 3;
    for (int i = 0; i < 3; i++) {
        double x = M + i * (bw + 10);
        p.rect_rgb(x, p.y - 52, bw, 52, 0.95, 0.96, 0.98);
        p.rect(x, p.y - 52, 4, 52, boxes[i].color);
        p.text(x + 12, p.y - 18, boxes[i].label, 9, false, 240);
        p.text(x + 12, p.y - 42, util::money(boxes[i].v, false), 20, true, boxes[i].color == 124 ? 124 : 236);
    }
    p.y -= 70;

    // Stacked bar: how take-home pay is split
    if (s.income_month > 0 && !s.cats.empty()) {
        p.heading("How each month's pay is split");
        double total = std::max(s.income_month, s.spend_month), x = M, h = 22;
        for (const auto &c : s.cats) {
            double w = W * c.monthly / total;
            p.rect(x, p.y - h, w, h, c.color);
            if (w > 40) p.text(x + 3, p.y - 14, p.fit(c.name, w - 6, 7.5, true), 7.5, true, util::is_dark256(c.color) ? 231 : 16);
            x += w;
        }
        if (s.left_month > 0) {
            double w = W * s.left_month / total;
            p.rect_rgb(x, p.y - h, w, h, 0.88, 0.90, 0.92);
            if (w > 40) p.text(x + 3, p.y - 14, p.fit("Left over", w - 6, 7.5, true), 7.5, true, 238);
        }
        p.y -= h + 16;
    }

    // Category bar chart
    if (!s.cats.empty()) {
        p.heading("Where the money goes (monthly)");
        double maxv = s.cats.front().monthly, label_w = 120, val_w = 110, bar_w = W - label_w - val_w;
        for (const auto &c : s.cats) {
            p.need(18);
            p.rect(M, p.y - 10, 9, 9, c.color);
            p.text(M + 14, p.y - 9, p.fit(c.name, label_w - 16, 9.5, false), 9.5, false);
            double w = maxv > 0 ? bar_w * c.monthly / maxv : 0;
            p.rect(M + label_w, p.y - 11, std::max(1.0, w), 11, c.color);
            std::string v = util::money(c.monthly, false) + "  " + (s.spend_month > 0 ? util::percent(c.monthly / s.spend_month, 0) : "");
            p.text_right(Pdf::PW - M, p.y - 9, v, 9.5, false);
            p.y -= 17;
        }
    }

    // Checkups
    if (!s.tips.empty()) {
        p.heading("Checkups");
        for (const auto &t : s.tips) {
            p.need(30);
            int c = t.mood == budget::GOOD ? 28 : t.mood == budget::OKAY ? 136 : 124;
            const char *tag = t.mood == budget::GOOD ? "GOOD" : t.mood == budget::OKAY ? "NOTE" : "WATCH";
            p.rect(M, p.y - 11, 40, 13, c);
            p.text(M + 20 - Pdf::width(tag, 7.5, true) / 2, p.y - 8, tag, 7.5, true, 231);
            p.text(M + 48, p.y - 8, t.title, 10, true);
            p.y -= 14;
            for (const auto &w : util::wrap(t.detail, 110)) {
                p.need(12);
                p.text(M + 48, p.y - 6, w, 8.5, false, 240);
                p.y -= 11;
            }
            p.y -= 4;
        }
    }

    // Expenses table, grouped by category
    if (!b.expenses.empty()) {
        p.heading("Expenses");
        double cx[] = {M, M + 210, M + 300, M + 380, M + 450};
        auto header = [&]() {
            p.text(cx[0], p.y, "Name", 8.5, true, 240);
            p.text(cx[1], p.y, "How often", 8.5, true, 240);
            p.text_right(cx[3] - 10, p.y, "Amount", 8.5, true, 240);
            p.text_right(cx[4] - 10, p.y, "Monthly", 8.5, true, 240);
            p.text_right(Pdf::PW - M, p.y, "Yearly", 8.5, true, 240);
            p.y -= 12;
        };
        header();
        std::vector<std::string> order;
        for (const auto &c : b.categories) order.push_back(c.name);
        for (const auto &e : b.expenses)
            if (std::find(order.begin(), order.end(), e.category) == order.end()) order.push_back(e.category);
        for (const auto &cat : order) {
            double total = 0;
            int n = 0;
            for (const auto &e : b.expenses)
                if (e.category == cat) { total += e.monthly(); n++; }
            if (!n) continue;
            p.need(30);
            const Category *cc = b.category(cat);
            p.rect(M, p.y - 3, 4, 12, cc ? cc->color : 245);
            p.text(M + 9, p.y, cat, 10, true);
            p.text_right(cx[4] - 10, p.y, util::money(total), 10, true);
            p.y -= 13;
            for (const auto &e : b.expenses) {
                if (e.category != cat) continue;
                p.need(13);
                std::string name = e.name + (e.need ? "" : "  (want)") + (e.due_day ? "  - due the " + std::to_string(e.due_day) + (e.due_day % 10 == 1 && e.due_day != 11 ? "st" : e.due_day % 10 == 2 && e.due_day != 12 ? "nd" : e.due_day % 10 == 3 && e.due_day != 13 ? "rd" : "th") : "");
                p.text(cx[0] + 9, p.y, p.fit(name, cx[1] - cx[0] - 14, 9, false), 9, false);
                p.text(cx[1], p.y, freq_name(e.freq), 9, false, 240);
                p.text_right(cx[3] - 10, p.y, util::money(e.amount), 9, false);
                p.text_right(cx[4] - 10, p.y, util::money(e.monthly()), 9, false);
                p.text_right(Pdf::PW - M, p.y, util::money(e.yearly(), false), 9, false, 240);
                p.y -= 12;
            }
            p.y -= 3;
        }
    }

    // Income and taxes
    if (!b.earners.empty()) {
        p.heading("Income and taxes (" + std::to_string(datapack::active().tax_year) + " estimate)");
        auto row = [&](const std::string &label, double yearly, bool bold = false) {
            p.need(13);
            p.text(M, p.y, label, 9.5, bold);
            p.text_right(M + 380, p.y, util::money(yearly / 12), 9.5, bold);
            p.text_right(Pdf::PW - M, p.y, util::money(yearly), 9.5, bold);
            p.y -= 13;
        };
        p.text_right(M + 380, p.y, "Monthly", 8.5, true, 240);
        p.text_right(Pdf::PW - M, p.y, "Yearly", 8.5, true, 240);
        p.y -= 12;
        row("Gross pay", s.tax.gross + s.tax.untaxed);
        if (s.tax.pretax > 0) row("Pre-tax deductions (401k, health)", -s.tax.pretax);
        row("Federal income tax", -s.tax.federal);
        row("Social Security and Medicare", -(s.tax.social_security + s.tax.medicare + s.tax.self_employment));
        row("State income tax" + (s.tax.state_code.empty() ? "" : " (" + s.tax.state_code + ")"), -(s.tax.state + s.tax.state_payroll));
        if (s.tax.local > 0) row("Local income tax", -s.tax.local);
        if (s.tax.after_tax_deductions > 0) row("After-tax deductions", -s.tax.after_tax_deductions);
        p.hline(M, Pdf::PW - M, p.y + 9);
        row("Take-home pay", s.tax.net, true);
        for (const auto &e : s.tax.earners) {
            p.need(12);
            p.text(M, p.y, e.name + ": about " + util::money(e.per_check()) + " per paycheck, " + util::fixed(e.checks, 0) + " paychecks a year", 8.5, false, 240);
            p.y -= 12;
        }
    }

    // Loans
    auto ll = loan_lines(b);
    if (!ll.empty()) {
        p.heading("Loans");
        for (const auto &x : ll) {
            p.need(44);
            p.text(M, p.y, x.loan->name + "  (" + x.loan->kind + ")", 10, true);
            p.text_right(Pdf::PW - M, p.y, util::money(x.loan->balance) + " at " + util::fixed(x.loan->apr, 2) + "% APR", 10, false);
            p.y -= 13;
            p.text(M + 9, p.y, util::money(x.payment) + " a month: paid off in " + months_text(x.base) +
                                   (x.base.never ? "" : " (last payment " + x.loan->payment_month(x.base.months) + ")") + ", " +
                                   util::money(x.base.interest, false) + " in interest", 9, false);
            p.y -= 12;
            if (x.loan->extra > 0 || x.loan->lump > 0) {
                std::string what = x.loan->extra > 0 ? "+" + util::money(x.loan->extra, false) + "/month" : "";
                if (x.loan->lump > 0) what += (what.empty() ? "" : " and ") + util::money(x.loan->lump, false) + " now";
                p.text(M + 9, p.y, "With " + what + ": paid off in " + months_text(x.with_extra) + ", saving " +
                                       util::money(x.base.interest - x.with_extra.interest, false) + " in interest and " +
                                       std::to_string(x.base.months - x.with_extra.months) + " months", 9, true, 28);
                p.y -= 12;
            }
            // payoff progress bar: principal vs interest
            double total = x.loan->balance + x.base.interest;
            if (total > 0 && !x.base.never) {
                double w1 = W * x.loan->balance / total;
                p.rect(M + 9, p.y - 6, w1 - 9, 6, 33);
                p.rect(M + w1, p.y - 6, W - w1, 6, 167);
                p.y -= 12;
                p.text(M + 9, p.y - 4, "Blue: what you borrowed.  Red: interest you'll pay on top.", 7.5, false, 242);
                p.y -= 14;
            }
            p.y -= 4;
        }
    }

    // Wants
    if (!b.wants.empty()) {
        p.heading("Wants: is it worth it?");
        if (s.real_hourly > 0) p.para("At your real hourly pay of " + util::money(s.real_hourly) + " (take-home pay per hour worked):", 9, 240);
        for (const auto &w : b.wants) {
            auto m = budget::want_math(s, w.price);
            p.need(28);
            int c = m.mood == 0 ? 28 : m.mood == 1 ? 136 : 124;
            p.rect(M, p.y - 1, 8, 8, c);
            p.text(M + 13, p.y, w.name, 10, true);
            p.text_right(Pdf::PW - M, p.y, util::money(w.price), 10, true);
            p.y -= 12;
            for (const auto &x : util::wrap(m.verdict, 110)) {
                p.need(12);
                p.text(M + 13, p.y, x, 8.5, false, 240);
                p.y -= 11;
            }
            p.y -= 3;
        }
    }

    // Prices
    if (!s.price_month.empty() && !s.cats.empty()) {
        int region = s.region;
        p.heading("Prices: last 12 months (" + places::region_name(region) + ", through " + s.price_month + ")");
        p.para("Overall prices changed " + pct_signed(s.all_items_yoy) + ". If your categories keep changing like this, this budget would cost " +
               util::money(s.next_year_month, false) + " a month a year from now (" + util::money(s.next_year_month - s.spend_month, false) +
               " more).");
        p.y -= 4;
        for (const auto &c : s.cats) {
            if (!c.has_yoy) continue;
            p.need(14);
            p.rect(M, p.y - 1, 8, 8, c.color);
            p.text(M + 13, p.y, c.name, 9, false);
            double mid = M + 260, scale = 1500;  // 1% = 15pt
            double w = std::max(-120.0, std::min(120.0, c.yoy * scale));
            p.hline(mid, mid, p.y - 2);
            if (w >= 0) p.rect(mid, p.y - 1, std::max(1.0, w), 8, c.yoy > 0.03 ? 124 : 136);
            else p.rect(mid + w, p.y - 1, -w, 8, 28);
            p.text_right(Pdf::PW - M, p.y, pct_signed(c.yoy), 9, false);
            p.y -= 13;
        }
        p.y -= 4;
        p.para("Source: U.S. Bureau of Labor Statistics consumer price index. Price data as of " + datapack::active().date + ".", 8, 242);
    }

    std::string path = dated(folder, "Tally Budget", "pdf");
    if (!util::write_file_atomic(path, p.build(), true)) { error = "Couldn't write " + path; return ""; }
    return path;
}

// =============================================================================
// Printing
// =============================================================================

bool print_file(const std::string &path, std::string &message) {
#ifdef _WIN32
    HINSTANCE h = ShellExecuteA(nullptr, "print", path.c_str(), nullptr, nullptr, SW_HIDE);
    if ((INT_PTR)h > 32) { message = "Sent to your default printer."; return true; }
    util::open_with_system(path);
    message = "Opened the file; print it from there.";
    return true;
#else
    const char *prog = util::have_program("lp") ? "lp" : util::have_program("lpr") ? "lpr" : nullptr;
    if (!prog) {
        util::open_with_system(path);
        message = "No print command found (lp/lpr). Opened the file instead; print it from there.";
        return true;
    }
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp(prog, prog, path.c_str(), (char *)nullptr);
        _exit(127);
    }
    int st = 0;
    if (pid > 0) waitpid(pid, &st, 0);
    if (pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0) { message = "Sent to your default printer."; return true; }
    message = "The printer didn't accept it. The file is saved; open it and print from there.";
    return false;
#endif
}

}  // namespace report
