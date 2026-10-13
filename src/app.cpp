// The full-screen program: tabs, lists, forms, charts.
#include "app.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "budget.h"
#include "datapack.h"
#include "learn.h"
#include "loans.h"
#include "model.h"
#include "places.h"
#include "progupdate.h"
#include "report.h"
#include "server.h"
#include "term.h"
#include "update.h"
#include "util.h"
#include "version.h"

using namespace term;
namespace fs = std::filesystem;

namespace {

// Colors chosen to read on both dark and light terminal backgrounds.
const int C_ACCENT = 33, C_GOOD = 71, C_OKAY = 172, C_WATCH = 167, C_DIM = 244, C_BAR_BG = 24, C_BAR_FG = 231;
const int C_KEY = 75;

enum Tab { T_HOME, T_INCOME, T_EXPENSES, T_LOANS, T_WANTS, T_AREA, T_PRICES, T_LEARN, T_PRINT, T_SETTINGS, T_COUNT };
const char *TAB_NAMES[T_COUNT] = {"Home", "Income", "Expenses", "Loans", "Wants", "My Area", "Prices", "Learn", "Print", "Settings"};
const char *TAB_KEYS = "1234567890";
const char *TAB_ABOUT[T_COUNT] = {
    "Your month at a glance: what comes in, what goes out, what's left.",
    "Your pay before and after taxes. Press H for household details (ZIP, filing status).",
    "Every regular cost, turned into a monthly amount.",
    "How long your debts will take, and what paying extra saves.",
    "Thinking of buying something? See what it really costs you.",
    "How your budget compares with where you live.",
    "How fast prices are rising for each kind of spending.",
    "Short lessons on budgeting basics.",
    "Printable pages, spreadsheets, and files to share.",
    "Colors, categories, backups, and optional updates.",
};

struct CpiItem { const char *code, *name; };
const CpiItem CPI_ITEMS[] = {
    {"", "None (fixed cost, no inflation)"}, {"SA0", "All items (overall inflation)"}, {"SAF11", "Food at home (groceries)"},
    {"SEFV", "Food away from home"}, {"SAH1", "Shelter (rent and home costs)"}, {"SAH2", "Fuels and utilities"},
    {"SEHF01", "Electricity"}, {"SETB01", "Gasoline"}, {"SAT", "Transportation"}, {"SAM", "Medical care"},
    {"SAA", "Clothing"}, {"SAE", "Education and communication"}, {"SAR", "Recreation"},
};

const int PALETTE[] = {33, 39, 75, 37, 35, 70, 106, 142, 178, 214, 172, 209, 203, 167, 160, 125, 205, 213, 177, 141, 99, 63, 137, 108, 245};

std::string cpi_name(const std::string &code) {
    for (const auto &c : CPI_ITEMS)
        if (code == c.code) return c.name;
    return code;
}

struct Field {
    std::string label, value, hint;
    std::function<void()> edit;          // Enter
    std::function<void(int)> step;       // Left/Right (choices)
    bool info = false;                   // read-only line
};

struct Hit { int x, y, w; std::function<void()> fn; };

static std::string add_month(const std::string &ym, int delta) {
    int y = std::atoi(ym.c_str()), m = std::atoi(ym.c_str() + 5) - 1 + delta;
    y += m >= 0 ? m / 12 : (m - 11) / 12;
    m = ((m % 12) + 12) % 12;
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d", y, m + 1);
    return b;
}


class App {
public:
    explicit App(const std::string &file) : path(file) {}
    int run();

private:
    Budget b;
    std::string path;
    int tab = T_HOME;
    std::string msg;
    int msg_color = -1;
    int sel[T_COUNT] = {0};
    int scroll[T_COUNT] = {0};
    budget::Summary sum;
    std::vector<Expense> undo;
    int exp_sort = 0;                    // 0 category, 1 monthly, 2 name, 3 due day
    std::string exp_filter;
    bool bought_view = false;            // Expenses: B switches between the plan and what was bought
    bool cards_view = false;             // Expenses: C shows credit cards and the bills on them
    std::string bought_month;            // "2026-10"
    bool national = false;
    int learn_scroll = 0;
    std::string last_file;
    bool quit = false;
    std::vector<Hit> hits;

    // the form being edited (drawn instead of the tab)
    struct FormState {
        std::string title;
        std::function<std::vector<Field>()> fields;
        std::function<std::vector<std::pair<std::string, int>>()> preview;
        int sel = 0;
    };
    FormState *form_ = nullptr;

    // --- plumbing -----------------------------------------------------------
    void load();
    void save();
    void backup();
    void recalc() {
        sum = budget::summarize(b);
        // a trip's price is the sum of its parts (gas follows the latest regional price)
        for (auto &w : b.wants)
            if (w.trip) w.price = std::round(budget::trip_cost(w, sum.region).total * 100) / 100;
    }
    void say(const std::string &m, int color = -1) { msg = m; msg_color = color; }
    void apply_display() {
        set_color_mode((ColorMode)std::max(0, std::min(2, b.settings.color_mode)));
        set_ascii(b.settings.ascii);
    }
    int W() const { return width(); }
    int H() const { return height(); }
    int top() const { return 3; }
    int bottom() const { return H() - 2; }  // exclusive

    // --- drawing ------------------------------------------------------------
    void paint();
    void draw_frame(const std::string &keys);
    void draw_keys(const std::string &keys);
    int heading(int x, int y, const std::string &t, int w = 0);
    void hbar(int x, int y, int w, double frac, int color);
    void stacked(int x, int y, int w, const std::vector<std::pair<double, int>> &parts, double total, int rows = 1);
    void swatch(int x, int y, int color);
    std::string spark(const std::vector<double> &v);
    int wrap_text(int x, int y, int w, const std::string &t, int fg = -1, int attr = 0, int max_lines = 1000);

    void draw_home();
    void draw_income();
    void draw_expenses();
    void draw_bought();
    std::vector<int> bought_order() const;
    void draw_cards();
    void draw_loans();
    void draw_wants();
    void draw_area();
    void draw_prices();
    void draw_learn();
    void draw_print();
    void draw_settings();
    void draw_form();

    // --- modal helpers --------------------------------------------------------
    bool input(const std::string &label, std::string &value, const std::string &hint = "");
    bool input_number(const std::string &label, double &value, const std::string &hint = "", bool allow_blank_zero = true,
                      bool allow_negative = false);
    std::string input_error;             // shown inside the input box until the next try
    int choose(const std::string &title, const std::vector<std::string> &options, int current,
               const std::vector<int> &colors = {}, const std::vector<std::string> &hints = {});
    bool confirm(const std::string &question, bool default_yes = false);
    bool pick_date(const std::string &title, std::string &date, const std::string &hint, bool allow_clear = true);
    void show_text(const std::string &title, const std::string &body);
    void run_form(const std::string &title, std::function<std::vector<Field>()> fields,
                  std::function<std::vector<std::pair<std::string, int>>()> preview);

    // --- fields -----------------------------------------------------------------
    Field f_text(const std::string &label, std::string &ref, const std::string &hint);
    Field f_money(const std::string &label, double &ref, const std::string &hint, bool allow_negative = false);
    Field f_num(const std::string &label, double &ref, const std::string &suffix, const std::string &hint);
    Field f_int(const std::string &label, int &ref, int lo, int hi, const std::string &hint, const std::string &zero = "");
    Field f_choice(const std::string &label, int &ref, std::vector<std::string> opts, const std::string &hint,
                   std::vector<std::string> hints = {});
    Field f_bool(const std::string &label, bool &ref, const std::string &on, const std::string &off, const std::string &hint);
    Field f_freq(const std::string &label, Freq &ref, const std::string &hint);
    Field f_category(std::string &ref);

    // --- actions -----------------------------------------------------------------
    void handle(const Event &e);
    void handle_list_keys(const Event &e, int count);
    void edit_household();
    void edit_earner(size_t i);
    void edit_expense(size_t i);
    void edit_loan(size_t i);
    void edit_want(size_t i);
    void edit_trip(size_t i);
    void edit_card(size_t i);
    void pick_card_bills(size_t i);
    void handle_cards(const Event &e, int lower);
    Field f_date(const std::string &label, std::string &ref, const std::string &hint, std::function<void()> after = nullptr);
    void check_loan_twins(size_t loan);
    void edit_purchase(size_t i);
    // amounts that change from month to month
    bool enter_actual(Actuals &a, const std::string &month, const std::string &what, double planned, bool income);
    void actual_other_month(Actuals &a, const std::string &what, std::function<double(const std::string &)> planned, bool income);
    static std::string month_label(const std::string &ym);
    void handle_bought(const Event &e, int lower);
    void edit_server();
    void do_pull();
    void import_purchases();
    void add_from_template();
    void edit_categories();
    void edit_category(size_t i);
    void do_update();
    void do_program_update();
    void do_export(int which);
    void restore_backup();
    void welcome();
    // The Expenses list shows loans too (they're part of the budget). Ids: >= 0 an expense, < 0 loan -(id + 1).
    std::vector<int> expense_order() const;
    std::string row_name(int id) const;
    std::string row_category(int id) const;
    double row_monthly(int id) const;
    long row_due(int id) const;          // next due day number (LONG_MAX = not set)
    std::string row_due_text(int id) const;
};

// =============================================================================
// Loading, saving, backups
// =============================================================================

void App::load() {
    std::string text, err;
    if (util::read_file(path, text)) {
        if (!b.parse(text, err)) {
            // never overwrite a file we couldn't read: keep it, start fresh beside it
            std::string keep = path + ".unreadable";
            std::error_code ec;
            fs::copy_file(fs::u8path(path), fs::u8path(keep), fs::copy_options::overwrite_existing, ec);
            b = Budget();
            b.set_defaults();
            say("Couldn't read your budget file (" + err + "). It was kept as " + keep, C_WATCH);
        }
    } else {
        b.set_defaults();
    }
    budget::normalize(b);
}

void App::save() {
    util::make_dirs(fs::u8path(path).parent_path().u8string());
    if (!util::write_file_atomic(path, b.serialize(), true)) say("Couldn't save to " + path + ". Check the folder's permissions.", C_WATCH);
    recalc();
}

void App::backup() {
    if (!util::exists(path)) return;
    std::string dir = util::join(fs::u8path(path).parent_path().u8string(), "backups");
    util::make_dirs(dir);
    util::Date d = util::today();
    char name[64];
    std::snprintf(name, sizeof name, "tally-%04d-%02d-%02d.txt", d.y, d.m, d.d);
    std::string dest = util::join(dir, name);
    std::error_code ec;
    if (!util::exists(dest)) fs::copy_file(fs::u8path(path), fs::u8path(dest), ec);
    // keep the newest 30 days of backups
    std::vector<fs::path> all;
    for (auto &e : fs::directory_iterator(fs::u8path(dir), ec))
        if (e.path().filename().string().rfind("tally-", 0) == 0) all.push_back(e.path());
    std::sort(all.begin(), all.end());
    while (all.size() > 30) { fs::remove(all.front(), ec); all.erase(all.begin()); }
}

// =============================================================================
// Drawing helpers
// =============================================================================

int App::heading(int x, int y, const std::string &t, int w) {
    put(x, y, t, C_ACCENT, -1, A_BOLD);
    if (w > (int)util::width(t) + 1) {
        std::string line;
        for (int i = 0; i < w - (int)util::width(t) - 1; i++) line += g_hline();
        put(x + (int)util::width(t) + 1, y, line, C_DIM);
    }
    return y + 1;
}

void App::swatch(int x, int y, int color) { put(x, y, g_block() + g_block(), color); }

void App::hbar(int x, int y, int w, double frac, int color) {
    frac = std::max(0.0, std::min(1.0, frac));
    int eighths = (int)std::lround(frac * w * 8);
    std::string s;
    for (int i = 0; i < eighths / 8; i++) s += g_block();
    if (eighths % 8) s += g_partial(eighths % 8);
    if (eighths == 0 && frac > 0) s = g_partial(1);
    put(x, y, s, color);
}

void App::stacked(int x, int y, int w, const std::vector<std::pair<double, int>> &parts, double total, int rows) {
    if (total <= 0 || w <= 0) return;
    // largest-remainder rounding so the segments always fill exactly w cells
    std::vector<int> cells(parts.size(), 0);
    std::vector<std::pair<double, size_t>> rem;
    int used = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        double exact = std::max(0.0, parts[i].first) / total * w;
        cells[i] = (int)exact;
        used += cells[i];
        rem.push_back({exact - cells[i], i});
    }
    std::sort(rem.rbegin(), rem.rend());
    double sum_parts = 0;
    for (const auto &p : parts) sum_parts += std::max(0.0, p.first);
    int target = (int)std::lround(std::min(1.0, sum_parts / total) * w);
    for (size_t k = 0; k < rem.size() && used < target; k++) { cells[rem[k].second]++; used++; }
    static const char *ascii_fill = "#=%@*+o:x~";
    int cx = x;
    for (size_t i = 0; i < parts.size(); i++) {
        std::string seg;
        for (int k = 0; k < cells[i]; k++) seg += ascii() ? std::string(1, ascii_fill[i % 10]) : g_block();
        for (int r = 0; r < rows; r++) put(cx, y + r, seg, parts[i].second);
        cx += cells[i];
    }
    std::string rest;
    for (int k = cx; k < x + w; k++) rest += g_shade();
    for (int r = 0; r < rows; r++) put(cx, y + r, rest, C_DIM);
}

std::string App::spark(const std::vector<double> &v) {
    if (v.empty()) return "";
    double lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
    std::string s;
    for (double x : v) s += g_spark(hi > lo ? (int)std::lround((x - lo) / (hi - lo) * 7) : 3);
    return s;
}

int App::wrap_text(int x, int y, int w, const std::string &t, int fg, int attr, int max_lines) {
    int n = 0;
    for (const auto &line : util::wrap(t, (size_t)std::max(10, w))) {
        if (y >= bottom() || n >= max_lines) break;
        put(x, y++, line, fg, -1, attr);
        n++;
    }
    return y;
}

void App::draw_keys(const std::string &keys) {
    // "A add|Enter edit|..." -> key in color, words dim
    int x = 1, y = H() - 1;
    fill(0, y, W(), 1, -1);
    for (const auto &item : util::split(keys, '|')) {
        size_t sp = item.find(' ');
        std::string k = sp == std::string::npos ? item : item.substr(0, sp);
        std::string d = sp == std::string::npos ? "" : item.substr(sp);
        if (x + (int)util::width(item) + 2 > W()) break;
        put(x, y, k, C_KEY, -1, A_BOLD);
        put(x + (int)util::width(k), y, d, C_DIM);
        x += (int)util::width(item) + 3;
    }
}

void App::draw_frame(const std::string &keys) {
    hits.clear();
    clear();
    // Title bar with tabs (click or press the number)
    fill(0, 0, W(), 1, C_BAR_BG, " ");
    put(1, 0, "TALLY", C_BAR_FG, C_BAR_BG, A_BOLD);  // covered by tabs on very narrow screens
    // Fit the tabs: roomy, then tight, then short names
    static const char *SHORT[T_COUNT] = {"Home", "Pay", "Bills", "Loans", "Wants", "Area", "Prices", "Learn", "Print", "Setup"};
    auto need = [](const char *const *names, int gap) {
        int t = 8;
        for (int i = 0; i < T_COUNT; i++) t += (int)util::width(names[i]) + 4 + gap;
        return t;
    };
    const char *const *names = TAB_NAMES;
    int gap = 1;
    if (need(names, 1) > W()) gap = 0;
    if (need(names, 0) > W()) names = SHORT;
    bool tiny = need(names, 0) > W();   // very narrow: drop the title and the space after each number
    int x = tiny ? 0 : 8;
    if (tiny) fill(0, 0, 8, 1, C_BAR_BG, " ");
    for (int i = 0; i < T_COUNT; i++) {
        std::string label = std::string(1, TAB_KEYS[i]) + (tiny ? "" : " ") + names[i];
        int w = (int)util::width(label) + 2;
        if (x + w > W()) break;
        if (i == tab) put(x, 0, " " + label + " ", C_BAR_BG, C_BAR_FG, A_BOLD);
        else put(x, 0, " " + label + " ", C_BAR_FG, C_BAR_BG);
        int t = i;
        hits.push_back({x, 0, w, [this, t] { tab = t; form_ = nullptr; }});
        x += w + gap;
    }
    if (!form_) {
        put(1, 1, TAB_NAMES[tab], -1, -1, A_BOLD);
        put(2 + (int)util::width(TAB_NAMES[tab]), 1, "- " + std::string(TAB_ABOUT[tab]), C_DIM);
    }
    // status line
    if (!msg.empty()) put(1, H() - 2, util::cut(msg, (size_t)W() - 2), msg_color, -1, A_BOLD);
    bool sub = (tab == T_PRICES && bought_view) || (tab == T_EXPENSES && cards_view);
    draw_keys(form_ || tab == T_HOME ? keys : keys + (sub ? "|Esc back" : "|Esc home"));
}

void App::paint() {
    if (W() < 60 || H() < 18) {
        clear();
        put(0, 0, "Tally needs a bigger window", C_WATCH, -1, A_BOLD);
        put(0, 1, "(at least 60 x 18; now " + std::to_string(W()) + " x " + std::to_string(H()) + ")");
        return;
    }
    if (form_) { draw_form(); return; }
    switch (tab) {
        case T_HOME: draw_home(); break;
        case T_INCOME: draw_income(); break;
        case T_EXPENSES: draw_expenses(); break;
        case T_LOANS: draw_loans(); break;
        case T_WANTS: draw_wants(); break;
        case T_AREA: draw_area(); break;
        case T_PRICES: draw_prices(); break;
        case T_LEARN: draw_learn(); break;
        case T_PRINT: draw_print(); break;
        case T_SETTINGS: draw_settings(); break;
    }
}

// =============================================================================
// Home
// =============================================================================

void App::draw_home() {
    draw_frame("1-0 tabs|Tab next|C checkups|? explain|P print|Q quit");
    int y = top(), w = W();
    bool wide = w >= 100;
    int lw = wide ? (int)(w * 0.58) : w - 2;
    int x = 1;

    if (b.earners.empty() && b.expenses.empty() && b.loans.empty()) {
        y = heading(x, y, "WELCOME TO TALLY", w - 2) + 1;
        y = wrap_text(x, y, w - 4, "A budget is just a plan for your money. Three steps get you started:") + 1;
        const char *steps[] = {"Press 2 (Income) and A to add your job. Tally estimates your take-home pay.",
                               "Press 3 (Expenses) and T to pick common bills from a list and fill in amounts.",
                               "Come back here (press 1) to see where your money goes."};
        for (int i = 0; i < 3; i++) {
            put(x + 1, y, std::to_string(i + 1) + ".", C_ACCENT, -1, A_BOLD);
            y = wrap_text(x + 4, y, w - 8, steps[i]);
        }
        y++;
        y = wrap_text(x, y, w - 4, "New to budgeting? Press 8 for short lessons. Press ? on any screen for an explanation of it.", C_DIM);
        y++;
        wrap_text(x, y, w - 4, "Everything you type stays on this computer. Tally works with no internet.", C_GOOD);
        return;
    }

    // Three big numbers
    int tw = (lw - 2) / 3;
    struct Tile { std::string label; double v; int color; };
    Tile tiles[3] = {{"Take-home pay", sum.income_month, C_GOOD},
                     {"Spending", sum.spend_month, C_ACCENT},
                     {sum.left_month >= 0 ? "Left over" : "Short", sum.left_month, sum.left_month >= 0 ? C_GOOD : C_WATCH}};
    for (int i = 0; i < 3; i++) {
        int tx = x + i * (tw + 1);
        put(tx, y, tiles[i].label, C_DIM);
        put(tx, y + 1, util::money(tiles[i].v, false), tiles[i].color, -1, A_BOLD);
        put(tx + (int)util::width(util::money(tiles[i].v, false)), y + 1, " /month", C_DIM);
    }
    y += 3;

    // The month's pay, split by category
    if (sum.income_month > 0 || sum.spend_month > 0) {
        y = heading(x, y, "HOW EACH MONTH'S PAY IS SPLIT", lw);
        std::vector<std::pair<double, int>> parts;
        for (const auto &c : sum.cats) parts.push_back({c.monthly, c.color});
        double total = std::max(sum.income_month, sum.spend_month);
        stacked(x, y, lw, parts, total, 2);
        y += 2;
        if (sum.left_month > 0) put(x, y, g_shade() + g_shade() + " left over", C_DIM);
        else if (sum.left_month < 0) put(x, y, "Spending is more than take-home pay by " + util::money(-sum.left_month, false), C_WATCH, -1, A_BOLD);
        y += 2;
    }

    // Category bars
    if (!sum.cats.empty()) {
        y = heading(x, y, "WHERE THE MONEY GOES", lw);
        double maxv = sum.cats.front().monthly;
        int namew = 16, valw = 16, barw = std::max(6, lw - namew - valw - 4);
        for (const auto &c : sum.cats) {
            if (y >= bottom()) break;
            swatch(x, y, c.color);
            put(x + 3, y, util::fit(c.name, namew), -1);
            hbar(x + 3 + namew, y, barw, maxv > 0 ? c.monthly / maxv : 0, c.color);
            std::string v = util::money(c.monthly, false) + " " + util::pad_left(sum.spend_month > 0 ? util::percent(c.monthly / sum.spend_month, 0) : "", 4);
            put(x + lw - (int)util::width(v), y, v);
            y++;
        }
    }

    // Checkups
    if (b.settings.tips) {
        int cx = wide ? lw + 4 : x, cy = wide ? top() : y + 1, cw = wide ? w - lw - 5 : w - 3;
        if (!wide && cy + 4 > bottom() && !sum.tips.empty()) {
            // not enough room: one line that says what's waiting
            int counts[3] = {0, 0, 0};
            for (const auto &t : sum.tips) counts[t.mood]++;
            std::string line = "Checkups: " + std::to_string(counts[0]) + " ok, " + std::to_string(counts[1]) + " worth a look, " +
                               std::to_string(counts[2]) + " need attention. Press C to read them.";
            put(x, bottom() - 1, util::cut(line, (size_t)w - 2), counts[2] ? C_WATCH : counts[1] ? C_OKAY : C_GOOD, -1, A_BOLD);
        } else if (cy < bottom() - 2) {
            cy = heading(cx, cy, "CHECKUPS", cw);
            for (const auto &t : sum.tips) {
                if (cy >= bottom() - 1) break;
                int color = t.mood == budget::GOOD ? C_GOOD : t.mood == budget::OKAY ? C_OKAY : C_WATCH;
                std::string tag = t.mood == budget::GOOD ? " ok " : t.mood == budget::OKAY ? " -- " : " !! ";
                put(cx, cy, tag, C_BAR_FG, color, A_BOLD);
                cy = wrap_text(cx + 5, cy, cw - 5, t.title, -1, A_BOLD, 2);
                cy = wrap_text(cx + 5, cy, cw - 5, t.detail, C_DIM, 0, wide ? 4 : 2) + 1;
            }
        }
    }
}

// =============================================================================
// Income
// =============================================================================

void App::draw_income() {
    draw_frame("A add income|Enter edit|M received this month|D delete|H household|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    y = heading(x, y, "HOUSEHOLD  (press H to change)", w);
    const auto &pl = sum.place;
    std::string where = b.home.zip.empty() ? "No ZIP code yet" : "ZIP " + b.home.zip;
    if (pl.found) where += "  " + g_bullet() + "  " + pl.county + ", " + b.home.state + "  " + g_bullet() + "  " + places::region_name(sum.region) + " region";
    else if (!b.home.state.empty()) where += "  " + g_bullet() + "  " + places::state_name(b.home.state);
    put(x + 1, y++, where, b.home.zip.empty() ? C_OKAY : -1);
    std::string line2 = std::string(filing_name(b.home.filing)) + "  " + g_bullet() + "  kids under 17: " + std::to_string(b.home.kids) +
                        "  " + g_bullet() + "  other dependents: " + std::to_string(b.home.other_deps);
    if (b.home.local_tax > 0) line2 += "  " + g_bullet() + "  local tax " + util::fixed(b.home.local_tax, 2) + "%";
    put(x + 1, y++, util::cut(line2, w));
    hits.push_back({x, y - 2, w, [this] { edit_household(); }});
    y++;

    y = heading(x, y, "INCOME", w);
    if (b.earners.empty()) {
        put(x + 1, y++, "Nothing here yet. Press A to add your job or other income.", C_OKAY);
    } else {
        bool roomy = w >= 96;
        int nw = roomy ? 22 : 18, kw = roomy ? 22 : 15;
        std::string month = budget::this_month();
        std::string got_hdr = util::month_name(std::atoi(month.c_str() + 5), true) + " received";
        put(x + 3, y, util::fit("Name", nw) + util::fit("Type", kw) + util::pad_left("Gross / year", 14) + util::pad_left("Take-home / check", 19) +
                          (roomy ? "  " + util::fit("Paid", 14) + util::pad_left(got_hdr, 14) : ""), C_DIM);
        y++;
        for (size_t i = 0; i < b.earners.size(); i++) {
            const Earner &e = b.earners[i];
            const tax::PerEarner *p = i < sum.tax.earners.size() ? &sum.tax.earners[i] : nullptr;
            bool s = (int)i == sel[tab];
            std::string kind = kind_name(e.kind);
            if (!roomy) kind = kind.substr(0, kind.find(" ("));
            const Actual *got = e.actual.find(month);
            std::string row = util::fit(e.name, nw) + util::fit(kind, kw) + util::pad_left(util::money(e.gross_year(), false), 14) +
                              util::pad_left(p ? util::money(p->per_check()) : "", 19) +
                              (roomy ? "  " + util::fit(freq_name(e.pay), 14) + util::pad_left(got ? util::money(got->amount) : "", 14) : "");
            put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
            put(x + 3, y, util::fit(row, (size_t)w - 3), -1, -1, s ? A_REVERSE : 0);
            size_t idx = i;
            hits.push_back({x, y, w, [this, idx] { if (sel[tab] == (int)idx) edit_earner(idx); else sel[tab] = (int)idx; }});
            y++;
        }
    }
    y++;
    const tax::Result &t = sum.tax;
    if (t.gross + t.untaxed <= 0 || y >= bottom() - 3) return;
    y = heading(x, y, "WHERE YOUR PAY GOES (per year)", w);
    struct Part { const char *name; double v; int color; };
    std::vector<Part> parts = {{"Take-home pay", t.net, C_GOOD},
                               {"Federal income tax", std::max(0.0, t.federal), 33},
                               {"Social Security & Medicare", t.social_security + t.medicare + t.self_employment, 75},
                               {"State income tax", t.state + t.state_payroll, 141},
                               {"Local income tax", t.local, 176},
                               {"Pre-tax: 401(k), health", t.pretax, 214},
                               {"After-tax deductions", t.after_tax_deductions, 245}};
    std::vector<std::pair<double, int>> bars;
    double gross = t.gross + t.untaxed;
    for (const auto &p : parts) bars.push_back({p.v, p.color});
    stacked(x, y, w, bars, gross, 1);
    y += 2;
    for (const auto &p : parts) {
        if (p.v <= 0.5 || y >= bottom() - 2) continue;
        swatch(x + 1, y, p.color);
        put(x + 4, y, util::fit(p.name, 30));
        put(x + 34, y, util::pad_left(util::money(p.v, false), 12) + util::pad_left(util::money(p.v / 12, false) + "/mo", 13) +
                           util::pad_left(util::percent(p.v / gross, 1), 8), -1);
        y++;
    }
    if (t.federal < 0 && y < bottom()) put(x + 1, y++, "Federal: credits are larger than your tax, so you'd get " + util::money(-t.federal, false) + " back.", C_GOOD);
    y++;
    if (y < bottom()) {
        std::string facts = "Federal bracket " + util::percent(t.marginal, 0) + "  " + g_bullet() + "  all taxes " + util::percent(t.effective, 1) +
                            " of gross";
        if (sum.real_hourly > 0) facts += "  " + g_bullet() + "  real hourly pay " + util::money(sum.real_hourly);
        put(x + 1, y++, util::cut(facts, w), C_DIM);
    }
    for (const auto &n : t.notes)
        if (y < bottom()) put(x + 1, y++, util::cut(n, w), C_OKAY);
}

// =============================================================================
// Expenses
// =============================================================================

std::string App::row_name(int id) const { return id >= 0 ? b.expenses[(size_t)id].name : b.loans[(size_t)(-id - 1)].name; }

std::string App::row_category(int id) const {
    if (id >= 0) return b.expenses[(size_t)id].category;
    return b.loans[(size_t)(-id - 1)].kind == "Mortgage" ? "Housing" : "Debt & Loans";
}

double App::row_monthly(int id) const {
    if (id >= 0) return b.expenses[(size_t)id].monthly();
    const Loan &l = b.loans[(size_t)(-id - 1)];
    return l.monthly_payment() + l.extra;
}

long App::row_due(int id) const {
    util::Date t = util::today();
    long today_n = day_number(t.y, t.m, t.d);
    if (id >= 0) {
        long n = b.expenses[(size_t)id].next_due(today_n);
        return n < 0 ? LONG_MAX : n;
    }
    const Loan &l = b.loans[(size_t)(-id - 1)];
    if (!l.due_day()) return LONG_MAX;
    Expense e;   // a loan payment is a monthly bill on its due day
    e.due_day = l.due_day();
    long n = e.next_due(today_n);
    return n < 0 ? LONG_MAX : n;
}

std::string App::row_due_text(int id) const {
    if (id < 0) {
        int d = b.loans[(size_t)(-id - 1)].due_day();
        return d ? std::to_string(d) : "";
    }
    const Expense &e = b.expenses[(size_t)id];
    int y, m, d;
    bool has = parse_date(e.due_date, y, m, d);
    if (has && (e.freq == WEEKLY || e.freq == BIWEEKLY)) {
        static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        return wd[((day_number(y, m, d) + 4) % 7 + 7) % 7];
    }
    if (has && per_year(e.freq) < 12) {
        long n = row_due(id);
        if (n != LONG_MAX) from_day_number(n, y, m, d);
        return util::month_name(m, true) + " " + std::to_string(d);
    }
    return e.due_day ? std::to_string(e.due_day) : "";
}

std::vector<int> App::expense_order() const {
    std::vector<int> idx;
    for (int i = 0; i < (int)b.expenses.size(); i++) idx.push_back(i);
    // loans count in the budget, so they're listed here too (edited on the Loans tab)
    for (int i = 0; i < (int)b.loans.size(); i++)
        if (b.loans[i].in_budget && b.loans[i].balance > 0) idx.push_back(-i - 1);
    if (!exp_filter.empty()) idx.erase(std::remove_if(idx.begin(), idx.end(), [&](int id) { return row_category(id) != exp_filter; }), idx.end());
    std::map<std::string, int> cat_pos;
    for (size_t i = 0; i < b.categories.size(); i++) cat_pos[b.categories[i].name] = (int)i;
    auto cp = [&](const std::string &c) { auto it = cat_pos.find(c); return it == cat_pos.end() ? 999 : it->second; };
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) {
        switch (exp_sort) {
            case 1: return row_monthly(x) > row_monthly(y);
            case 2: return util::lower(row_name(x)) < util::lower(row_name(y));
            case 3: return row_due(x) < row_due(y);
            default: return cp(row_category(x)) < cp(row_category(y));
        }
    });
    return idx;
}

void App::draw_expenses() {
    if (cards_view) { draw_cards(); return; }
    draw_frame("A add|T common|Enter edit|M paid|D delete|U undo|S sort|F filter|C cards|? help");
    int y = top(), x = 1, w = W() - 2;
    static const char *sorts[] = {"category", "biggest first", "name", "next due"};
    std::string title = "EXPENSES  (sorted by " + std::string(sorts[exp_sort]) + (exp_filter.empty() ? "" : ", only " + exp_filter) + ")";
    y = heading(x, y, title, w);
    auto order = expense_order();
    if (order.empty()) {
        y++;
        y = wrap_text(x + 1, y, w - 2, exp_filter.empty() ? "No expenses yet. Press T to choose from common bills (rent, phone, groceries...) or A to add your own."
                                                          : "Nothing in " + exp_filter + ". Press F to show everything.", C_OKAY);
        y++;
        wrap_text(x + 1, y, w - 2, "Tip: start with the big, regular ones. You can add small stuff later. Loans you add on the Loans tab show up here on their own.", C_DIM);
        return;
    }
    std::string month = budget::this_month();
    auto mb = budget::month_bills(b, month);
    bool show_due = w >= 96, show_type = w >= 84, show_paid = w >= 104;
    int name_w = w - 3 - 17 - 12 - 15 - 12 - (show_type ? 8 : 0) - (show_due ? 8 : 0) - (show_paid ? 12 : 0);
    std::string paid_hdr = util::month_name(std::atoi(month.c_str() + 5), true) + " paid";
    std::string hdr = util::fit("Name", name_w) + util::fit("Category", 17) + util::pad_left("Amount", 12) + "  " + util::fit("How often", 13) +
                      util::pad_left("Monthly", 12) + (show_paid ? util::pad_left(paid_hdr, 12) : "") + (show_type ? "  Type  " : "") + (show_due ? util::pad_left("Due", 8) : "");
    put(x + 3, y++, hdr, C_DIM);

    // rows: headers between categories when sorted by category
    struct Row { int id; std::string header; bool is_header; };
    std::vector<Row> rows;
    std::string last;
    bool first = true;
    for (int id : order) {
        if (exp_sort == 0 && (first || row_category(id) != last)) {
            last = row_category(id);
            rows.push_back({0, last, true});
        }
        first = false;
        rows.push_back({id, "", false});
    }
    int count = (int)order.size();
    sel[tab] = std::max(0, std::min(sel[tab], count - 1));
    int sel_row = 0;
    for (int r = 0, k = 0; r < (int)rows.size(); r++)
        if (!rows[r].is_header) { if (k++ == sel[tab]) { sel_row = r; break; } }
    int vis = bottom() - y - 3;
    if (sel_row < scroll[tab]) scroll[tab] = std::max(0, sel_row - 1);
    if (sel_row >= scroll[tab] + vis) scroll[tab] = sel_row - vis + 1;
    int item_k = 0;
    bool any_avg = false;
    for (int id : order)
        if (id >= 0 && (b.expenses[(size_t)id].freq == WEEKLY || b.expenses[(size_t)id].freq == BIWEEKLY)) any_avg = true;
    for (int r = 0; r < (int)rows.size(); r++) {
        if (!rows[r].is_header) item_k++;
        if (r < scroll[tab]) continue;
        if (y >= bottom() - 3) break;
        if (rows[r].is_header) {
            const Category *c = b.category(rows[r].header);
            double sub = 0;
            for (int id : order) if (row_category(id) == rows[r].header) sub += row_monthly(id);
            swatch(x + 1, y, c ? c->color : 245);
            put(x + 4, y, rows[r].header, c ? c->color : -1, -1, A_BOLD);
            put(x + 3 + name_w + 17 + 12 + 15, y, util::pad_left(util::money(sub), 12), C_DIM, -1, A_BOLD);
            y++;
            continue;
        }
        int id = rows[r].id;
        bool s = item_k - 1 == sel[tab];
        std::string cat = row_category(id);
        const Category *c = b.category(cat);
        put(x, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        swatch(x + 1, y, c ? c->color : 245);
        std::string pre = exp_sort == 0 ? " " : "";
        std::string row, card_tag;   // the card's name is drawn dimmer than the bill's name
        int color = -1;
        if (id < 0) {
            const Loan &l = b.loans[(size_t)(-id - 1)];
            row = util::fit(pre + l.name + " (Loans tab)", name_w - 1) + " " + util::fit(cat, 17) + util::pad_left(util::money(row_monthly(id)), 12) + "  " +
                  util::fit("Monthly", 13) + util::pad_left(util::money(row_monthly(id)), 12) + (show_paid ? std::string(12, ' ') : "") +
                  (show_type ? "  loan  " : "") + (show_due ? util::pad_left(row_due_text(id), 8) : "");
            color = C_DIM;
        } else {
            const Expense &e = b.expenses[(size_t)id];
            const Actual *paid = e.actual.find(month);
            std::string paid_txt = paid ? util::money(paid->amount) : "";
            if (const Card *cd = budget::card_of(b, e)) {
                const auto &cm = mb.cards[(size_t)(cd - b.cards.data())];
                if (cm.entered) paid_txt = "on card";
            }
            bool twin = budget::loan_twin(b, e) >= 0;
            std::string avg = e.freq == WEEKLY || e.freq == BIWEEKLY ? "~" : "";
            if (!e.card.empty()) card_tag = " [" + e.card + "]";
            row = util::fit(pre + e.name + (e.card.empty() ? "" : " [" + e.card + "]"), name_w - 1) + " " + util::fit(e.category, 17) +
                  util::pad_left(util::money(e.amount), 12) + "  " + util::fit(freq_name(e.freq), 13) + util::pad_left(avg + util::money(e.monthly()), 12) +
                  (show_paid ? util::pad_left(paid_txt, 12) : "") + (show_type ? std::string(twin ? "  twice?" : e.need ? "  need  " : "  want  ") : "") +
                  (show_due ? util::pad_left(row_due_text(id), 8) : "");
            if (twin) color = C_WATCH;
        }
        put(x + 3, y, util::fit(row, (size_t)w - 3), color, -1, s ? A_REVERSE : 0);
        if (!card_tag.empty()) {
            size_t nw = util::width(pre + b.expenses[(size_t)id].name);
            if (nw + 2 < (size_t)name_w - 1)   // only when the name itself isn't cut off
                put(x + 3 + (int)nw, y, util::fit(card_tag, (size_t)name_w - 1 - nw), -1, -1, A_DIM | (s ? A_REVERSE : 0));
        }
        int k = item_k - 1;
        hits.push_back({x, y, w, [this, k, id] {
                            if (sel[tab] != k) { sel[tab] = k; return; }
                            if (id >= 0) edit_expense((size_t)id); else edit_loan((size_t)(-id - 1));
                        }});
        y++;
    }
    double total = 0, needs = 0;
    for (int id : order) {
        total += row_monthly(id);
        if (id < 0 || b.expenses[(size_t)id].need) needs += row_monthly(id);
    }
    std::string foot = std::to_string(count) + (count == 1 ? " item  " : " items  ") + g_bullet() + "  " + util::money(total) + " a month  " + g_bullet() + "  needs " +
                       util::money(needs, false) + ", wants " + util::money(total - needs, false);
    put(x + 1, bottom() - 1, util::cut(foot, w), C_DIM, -1, A_BOLD);
    if (any_avg)
        put(x + 1, bottom() - 3, util::cut("~ weekly bills show the monthly average (x 52 weeks / 12): most months have 4 payments, some have 5. Open one to see which.", (size_t)w), C_DIM);
    // bills entered for this month vs. their plan (card payments stand in for the bills on the card)
    if (mb.bills) {
        double diff = mb.paid - mb.plan;
        std::string line = util::month_name(std::atoi(month.c_str() + 5)) + ": " + std::to_string(mb.bills) + (mb.bills == 1 ? " bill" : " bills") +
                           " entered, paid " + util::money(mb.paid) + " vs " + util::money(mb.plan) + " planned" +
                           (std::fabs(diff) < 0.005 ? "" : " (" + std::string(diff > 0 ? "+" : "-") + util::money(std::fabs(diff)) + ")");
        put(x + 1, bottom() - 2, util::cut(line, w), diff > 0.005 ? C_OKAY : C_GOOD, -1, A_BOLD);
    }
}

// --- Credit cards: which bills are charged to which card ---------------------------

void App::draw_cards() {
    draw_frame("A add a card|Enter edit|M paid this month|Space pick bills|D delete|C back to plan|? explain");
    int y = top(), x = 1, w = W() - 2;
    std::string month = budget::this_month();
    y = heading(x, y, "CREDIT CARDS  (" + month_label(month) + ")", w);
    if (b.cards.empty()) {
        y++;
        y = wrap_text(x + 1, y, w - 2, "No cards yet. Press A and give a card a nickname, like 'Visa' or 'Store card'. Never the card number: Tally doesn't need it.", C_OKAY) + 1;
        y = wrap_text(x + 1, y, w - 2, "Then tick off the bills you charge to it (streaming, phone, gas...). When you pay the card, press M and type "
                                       "what you paid. That one payment covers those bills, so nothing is counted twice, and Tally shows "
                                       "how much of it was other spending.", C_DIM) + 1;
        wrap_text(x + 1, y, w - 2, "Carrying a balance? Add it on the Loans tab too, to see the interest and payoff date.", C_DIM);
        return;
    }
    auto mb = budget::month_bills(b, month);
    put(x + 3, y++, util::fit("Card", 22) + util::pad_left("Bills on it", 12) + util::pad_left("Expected", 12) + util::pad_left("Paid", 12) +
                        util::pad_left("Other spending", 16), C_DIM);
    int count = (int)b.cards.size();
    sel[tab] = std::max(0, std::min(sel[tab], count - 1));
    for (int i = 0; i < count && y < bottom() - 6; i++) {
        const auto &cm = mb.cards[(size_t)i];
        bool s = i == sel[tab];
        std::string other = !cm.entered ? "" : std::fabs(cm.other()) < 0.005 ? "none" : (cm.other() > 0 ? "+" : "-") + util::money(std::fabs(cm.other()));
        std::string row = util::fit(b.cards[(size_t)i].name, 22) + util::pad_left(std::to_string(cm.bills), 12) + util::pad_left(util::money(cm.expected), 12) +
                          util::pad_left(cm.entered ? util::money(cm.paid) : "-", 12) + util::pad_left(other, 16);
        put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 3, y, util::fit(row, (size_t)w - 3), cm.entered && cm.other() > 0.5 ? C_OKAY : -1, -1, s ? A_REVERSE : 0);
        hits.push_back({x, y, w, [this, i] { if (sel[tab] == i) edit_card((size_t)i); else sel[tab] = i; }});
        y++;
    }
    y++;
    const Card &c = b.cards[(size_t)sel[tab]];
    const auto &cm = mb.cards[(size_t)sel[tab]];
    y = heading(x, y, util::cut("ON " + c.name, 50), w);
    int shown = 0;
    for (const auto &e : b.expenses) {
        if (e.card != c.name) continue;
        if (y >= bottom() - 3) { put(x + 2, y++, "...", C_DIM); break; }
        const Actual *a = e.actual.find(month);
        double v = a ? a->amount : e.planned_in_month(month);
        put(x + 2, y++, util::fit(e.name, 30) + util::pad_left(util::money(v), 12) + (a ? "  (entered)" : ""), -1);
        shown++;
    }
    if (!shown) put(x + 2, y++, "No bills on this card yet. Press Space to pick them.", C_OKAY);
    y++;
    if (!cm.entered) put(x + 1, y, util::cut("Paid the card this month? Press M and type the amount. It replaces these bills in this month's total.", (size_t)w - 2), C_DIM);
    else if (cm.other() > 0.5)
        put(x + 1, y, util::cut("Paid " + util::money(cm.paid) + ": " + util::money(cm.expected) + " for the bills above, " + util::money(cm.other()) +
                                    " for other things charged to the card.", (size_t)w - 2), C_OKAY, -1, A_BOLD);
    else if (cm.other() < -0.5)
        put(x + 1, y, util::cut("Paid " + util::money(-cm.other()) + " less than the bills on it. What's left stays on the card and can grow with interest.", (size_t)w - 2), C_OKAY);
    else put(x + 1, y, "Paid exactly the bills on it. Nicely in check.", C_GOOD, -1, A_BOLD);
}

void App::handle_cards(const Event &e, int lower) {
    handle_list_keys(e, (int)b.cards.size());
    bool has = sel[tab] < (int)b.cards.size();
    std::string month = budget::this_month();
    if (lower == 'a') {
        Card c;
        c.name = "";
        if (!input("Card nickname", c.name, "Like 'Visa', 'Amex', or 'Target card'. Never the card number.") || util::trim(c.name).empty()) return;
        c.name = util::trim(c.name);
        for (const auto &o : b.cards) if (o.name == c.name) { say("You already have a card called " + c.name + ".", C_OKAY); return; }
        b.cards.push_back(c);
        sel[tab] = (int)b.cards.size() - 1;
        save();
        pick_card_bills(b.cards.size() - 1);
    } else if (e.key == K_ENTER && has) edit_card((size_t)sel[tab]);
    else if (e.key == ' ' && has) pick_card_bills((size_t)sel[tab]);
    else if (lower == 'm' && has) {
        auto mb = budget::month_bills(b, month);
        Card &c = b.cards[(size_t)sel[tab]];
        enter_actual(c.paid, month, c.name, mb.cards[(size_t)sel[tab]].expected, false);
    } else if ((lower == 'd' || e.key == K_DELETE) && has) {
        std::string name = b.cards[(size_t)sel[tab]].name;
        if (confirm("Remove the card " + name + "? Its bills stay; they just won't be marked as on a card.")) {
            for (auto &x : b.expenses) if (x.card == name) x.card.clear();
            b.cards.erase(b.cards.begin() + sel[tab]);
            save();
            say("Removed " + name + ".", C_DIM);
        }
    }
}

// Tick the bills charged to a card. Enter toggles one; "Done" (or Esc) finishes.
void App::pick_card_bills(size_t ci) {
    if (ci >= b.cards.size()) return;
    if (b.expenses.empty()) { say("Add some expenses first, then mark which ones go on " + b.cards[ci].name + ".", C_OKAY); return; }
    int cur = 0;
    for (;;) {
        const std::string &name = b.cards[ci].name;
        std::vector<std::string> opts;
        for (const auto &e : b.expenses) {
            std::string mark = e.card == name ? "[x] " : "[ ] ";
            std::string elsewhere = !e.card.empty() && e.card != name ? "  (on " + e.card + ")" : "";
            opts.push_back(mark + util::fit(e.name, 26) + util::pad_left(util::money(e.monthly()), 11) + "/mo" + elsewhere);
        }
        opts.push_back("Done");
        int k = choose("Bills charged to " + name + " (Enter = on/off)", opts, cur);
        if (k < 0 || k == (int)b.expenses.size()) break;
        Expense &e = b.expenses[(size_t)k];
        e.card = e.card == name ? "" : name;
        cur = k;
        save();
    }
    int n = 0;
    for (const auto &e : b.expenses) if (e.card == b.cards[ci].name) n++;
    say(b.cards[ci].name + ": " + std::to_string(n) + (n == 1 ? " bill" : " bills") + " on this card.", C_GOOD);
}

void App::edit_card(size_t i) {
    if (i >= b.cards.size()) return;
    run_form("Credit card",
        [this, i] {
            Card &c = b.cards[i];
            std::vector<Field> f;
            Field nm{"Nickname", c.name, "Like 'Visa' or 'Store card'. Never the card number: Tally doesn't need it and never asks.", nullptr, nullptr};
            nm.edit = [this, i] {
                std::string v = b.cards[i].name, old = v;
                if (!input("Nickname", v, "Like 'Visa' or 'Store card'.") || util::trim(v).empty()) return;
                v = util::trim(v);
                for (size_t k = 0; k < b.cards.size(); k++)
                    if (k != i && b.cards[k].name == v) { say("You already have a card called " + v + ".", C_OKAY); return; }
                for (auto &e : b.expenses) if (e.card == old) e.card = v;
                b.cards[i].name = v;
            };
            f.push_back(nm);
            int n = 0;
            double planned = 0;
            for (const auto &e : b.expenses) if (e.card == c.name) { n++; planned += e.monthly(); }
            Field bills{"Bills on this card", std::to_string(n) + (n == 1 ? " bill, " : " bills, ") + util::money(planned) + "/month",
                        "Tick off each bill you charge to this card (streaming, phone, gas...). Enter opens the list.", nullptr, nullptr};
            bills.edit = [this, i] { pick_card_bills(i); };
            f.push_back(bills);
            std::string month = budget::this_month();
            const Actual *a = c.paid.find(month);
            Field paid{"Paid in " + month_label(month), a ? util::money(a->amount) : "(not entered)",
                       "What you paid on the card this month. It stands in for the bills on it, so nothing is counted twice.", nullptr, nullptr};
            paid.edit = [this, i, month] {
                auto mb = budget::month_bills(b, month);
                enter_actual(b.cards[i].paid, month, b.cards[i].name, mb.cards[i].expected, false);
            };
            f.push_back(paid);
            Field other{"Other months...", c.paid.list.empty() ? "" : std::to_string(c.paid.list.size()) + " entered",
                        "Enter or fix what you paid on the card in an earlier month.", nullptr, nullptr};
            other.edit = [this, i] {
                actual_other_month(b.cards[i].paid, b.cards[i].name,
                                   [this, i](const std::string &m) { return budget::month_bills(b, m).cards[i].expected; }, false);
            };
            f.push_back(other);
            f.push_back(f_text("Notes", c.notes, "Anything to remember, like the due date or rewards. Never the card number."));
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            std::string month = budget::this_month();
            auto mb = budget::month_bills(b, month);
            const auto &cm = mb.cards[i];
            p.push_back({"Bills on it this month: " + util::money(cm.expected), -1});
            if (!cm.entered) p.push_back({"No payment entered yet for " + month_label(month) + ".", C_DIM});
            else {
                p.push_back({"You paid: " + util::money(cm.paid), -1});
                if (cm.other() > 0.5) p.push_back({util::money(cm.other()) + " of that was other spending on the card.", C_OKAY});
                else if (cm.other() < -0.5) p.push_back({util::money(-cm.other()) + " less than the bills: the rest stays on the card and can cost interest.", C_OKAY});
                else p.push_back({"Exactly the bills on it.", C_GOOD});
            }
            p.push_back({"", -1});
            p.push_back({"Why this helps: the bills on a card are already in your budget. Without this, paying the card "
                         "would look like a second, separate expense. Here the payment covers those bills, and only "
                         "the extra shows up as new spending.", C_DIM});
            return p;
        });
}

// --- What was bought (the plan vs. what really happened) ------------------------

std::vector<int> App::bought_order() const {
    std::vector<int> idx;
    for (int i = 0; i < (int)b.purchases.size(); i++)
        if (b.purchases[i].month() == bought_month) idx.push_back(i);
    // newest first; same day: most recently added first
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int c) {
        if (b.purchases[a].date != b.purchases[c].date) return b.purchases[a].date > b.purchases[c].date;
        return a > c;
    });
    return idx;
}

void App::draw_bought() {
    draw_frame(std::string("A add|Enter edit|D delete|Left/Right month|") + (b.link.on ? "G get from server|" : "") + "B back to prices|? explain");
    int y = top(), x = 1, w = W() - 2;
    if (bought_month.size() != 7) bought_month = budget::this_month();
    int yr = std::atoi(bought_month.c_str()), mo = std::atoi(bought_month.c_str() + 5);
    y = heading(x, y, "BOUGHT IN " + util::month_name(mo) + " " + std::to_string(yr) + "  (what really happened, next to the plan)", w);
    auto order = bought_order();
    if (order.empty()) {
        y++;
        y = wrap_text(x + 1, y, w - 2, "Nothing bought in this month yet. Press A to add something you bought, like a grocery trip.", C_OKAY) + 1;
        y = wrap_text(x + 1, y, w - 2, b.link.on ? "Or press G to get what you scanned on your server."
                                                 : "Run your own server? Settings > Server link lets you scan barcodes on your phone and bring them in here.", C_DIM) + 1;
        wrap_text(x + 1, y, w - 2, "Left/Right shows other months. B goes back to your plan.", C_DIM);
        return;
    }

    // each category: spent so far vs. the monthly plan
    std::map<std::string, double> spent;
    double total = 0;
    for (int i : order) { spent[b.purchases[i].category] += b.purchases[i].amount; total += b.purchases[i].amount; }
    int namew = 17, valw = 26, barw = std::max(6, std::min(40, w - namew - valw - 6));
    int cat_rows = 0, max_cat_rows = std::max(2, (bottom() - y) / 3);
    for (const auto &c : b.categories) {
        auto it = spent.find(c.name);
        if (it == spent.end()) continue;
        if (cat_rows++ >= max_cat_rows) break;
        double plan = 0;
        for (const auto &ct : sum.cats) if (ct.name == c.name) plan = ct.monthly;
        bool over = plan > 0 && it->second > plan;
        swatch(x + 1, y, c.color);
        put(x + 4, y, util::fit(c.name, (size_t)namew), -1);
        if (plan > 0) hbar(x + 4 + namew, y, barw, std::min(1.0, it->second / plan), over ? C_WATCH : c.color);
        std::string v = util::money(it->second, false) + (plan > 0 ? " of " + util::money(plan, false) + " planned" : "  (not in the plan)");
        put(x + 5 + namew + barw, y, util::cut(v, (size_t)valw), over ? C_WATCH : plan > 0 ? -1 : C_DIM, -1, over ? A_BOLD : 0);
        y++;
    }
    y++;

    int name_w = std::max(10, w - 3 - 12 - 17 - 12 - (w >= 90 ? 16 : 0));
    bool show_store = w >= 90;
    put(x + 3, y++, util::fit("Date", 12) + util::fit("What", (size_t)name_w) + util::fit("Category", 17) + (show_store ? util::fit("Store", 16) : "") +
                        util::pad_left("Price", 12), C_DIM);
    int count = (int)order.size();
    sel[tab] = std::max(0, std::min(sel[tab], count - 1));
    int vis = std::max(1, bottom() - y - 2);
    if (sel[tab] < scroll[tab]) scroll[tab] = sel[tab];
    if (sel[tab] >= scroll[tab] + vis) scroll[tab] = sel[tab] - vis + 1;
    for (int k = scroll[tab]; k < count && y < bottom() - 2; k++) {
        const Purchase &p = b.purchases[order[k]];
        bool s = k == sel[tab];
        const Category *c = b.category(p.category);
        int dm = std::atoi(p.date.c_str() + 5), dd = std::atoi(p.date.c_str() + 8);
        std::string when = util::month_name(dm, true) + " " + std::to_string(dd);
        put(x, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        swatch(x + 1, y, c ? c->color : 245);
        std::string row = util::fit(when, 12) + util::fit(p.name, (size_t)name_w) + util::fit(p.category, 17) + (show_store ? util::fit(p.store, 16) : "") +
                          util::pad_left(util::money(p.amount), 12);
        put(x + 3, y, util::fit(row, (size_t)w - 3), -1, -1, s ? A_REVERSE : 0);
        int pi = order[k];
        hits.push_back({x, y, w, [this, k, pi] { if (sel[tab] == k) edit_purchase((size_t)pi); else sel[tab] = k; }});
        y++;
    }
    std::string foot = std::to_string(count) + (count == 1 ? " purchase  " : " purchases  ") + g_bullet() + "  " + util::money(total) + " in all  " +
                       g_bullet() + "  for trends only: never added to your expenses or card totals";
    if (b.link.on) foot += "  " + g_bullet() + "  server checked " + (b.link.last_sync.empty() ? std::string("never") : b.link.last_sync);
    put(x + 1, bottom() - 1, util::cut(foot, (size_t)w), C_DIM, -1, A_BOLD);
}

// =============================================================================
// Loans
// =============================================================================

void App::draw_loans() {
    draw_frame("A add|Enter edit|+/- extra $25|L one-time|E schedule|D delete|? explain");
    int y = top(), x = 1, w = W() - 2;
    y = heading(x, y, "LOANS", w);
    if (b.loans.empty()) {
        y++;
        y = wrap_text(x + 1, y, w - 2, "No loans yet. Press A to add a car loan, student loan, mortgage, or credit card.", C_OKAY);
        y++;
        wrap_text(x + 1, y, w - 2, "You'll see when it's paid off, how much interest it costs in total, and how much you'd save by paying a little extra each month.", C_DIM);
        return;
    }
    put(x + 3, y++, util::fit("Name", 20) + util::fit("Kind", 12) + util::pad_left("Balance", 13) + util::pad_left("APR", 8) +
                        util::pad_left("Payment", 12) + util::pad_left("Last payment", 14) + util::pad_left("Interest", 12), C_DIM);
    int count = (int)b.loans.size();
    sel[tab] = std::max(0, std::min(sel[tab], count - 1));
    for (int i = 0; i < count && y < bottom() - 10; i++) {
        const Loan &l = b.loans[i];
        auto p = loans::simulate(l.balance, l.apr, l.monthly_payment(), l.extra, l.lump);
        bool s = i == sel[tab];
        std::string row = util::fit(l.name, 20) + util::fit(l.kind, 12) + util::pad_left(util::money(l.balance, false), 13) +
                          util::pad_left(util::fixed(l.apr, 2) + "%", 8) + util::pad_left(util::money(l.monthly_payment() + l.extra), 12) +
                          util::pad_left(p.never ? "never" : l.payment_month(p.months), 14) + util::pad_left(util::money(p.interest, false), 12);
        put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 3, y, util::fit(row, (size_t)w - 3), p.never ? C_WATCH : -1, -1, s ? A_REVERSE : 0);
        hits.push_back({x, y, w, [this, i] { if (sel[tab] == i) edit_loan((size_t)i); else sel[tab] = i; }});
        y++;
    }
    y++;
    const Loan &l = b.loans[sel[tab]];
    double pay = l.monthly_payment();
    auto base = loans::simulate(l.balance, l.apr, pay);
    auto with = loans::simulate(l.balance, l.apr, pay, l.extra, l.lump);
    y = heading(x, y, util::cut(l.name + " - what if?", 60), w);
    if (pay <= 0) {
        put(x + 1, y++, "Enter a monthly payment or the months left (Enter to edit) to see the payoff.", C_OKAY);
        return;
    }
    if (l.next_due.empty())
        put(x + 1, y++, util::cut("Tip: set 'Next payment due' (Enter) so the payoff month counts from your real due date.", (size_t)w - 2), C_OKAY);
    else {
        int dy, dm, dd;
        parse_date(l.next_due, dy, dm, dd);
        put(x + 1, y++, "Payment 1 of " + std::to_string(base.never ? 0 : base.months) + " is due " + l.payment_month(1) + " (on the " + std::to_string(dd) + util::ordinal(dd) + ").", C_DIM);
    }
    double first_interest = loans::interest_first_month(l.balance, l.apr);
    put(x + 1, y++, "Payment " + util::money(pay) + "/month. This month " + util::money(first_interest) + " of it is interest, " +
                        util::money(std::max(0.0, pay - first_interest)) + " pays down what you owe.", -1);
    if (base.never) {
        put(x + 1, y++, "This payment doesn't cover the interest, so the balance never goes down. Pay at least " +
                            util::money(first_interest + 1) + "/month.", C_WATCH, -1, A_BOLD);
    } else {
        put(x + 1, y, "As planned:  ", C_DIM);
        put(x + 14, y++, "last payment " + l.payment_month(base.months) + " (" + std::to_string(base.months) + " payments), " +
                             util::money(base.interest, false) + " in interest");
        std::string what = l.extra > 0 ? "+" + util::money(l.extra, false) + "/month" : "";
        if (l.lump > 0) what += (what.empty() ? "" : " + ") + util::money(l.lump, false) + " now";
        if (what.empty()) {
            put(x + 1, y++, "Press + to try paying $25 more each month.", C_OKAY);
        } else if (!with.never) {
            put(x + 1, y, "With " + what + ":  ", C_DIM);
            int off = 9 + (int)util::width(what);
            put(x + off, y++, "last payment " + l.payment_month(with.months) + " (" + std::to_string(with.months) + " payments), " +
                                  util::money(with.interest, false) + " in interest");
            put(x + 1, y++, "You'd save " + util::money(base.interest - with.interest, false) + " and " + std::to_string(base.months - with.months) +
                                " months of payments.", C_GOOD, -1, A_BOLD);
        }
        if (y < bottom() - 2) {
            double total = l.balance + base.interest;
            std::vector<std::pair<double, int>> parts = {{l.balance, 33}, {base.interest, 167}};
            stacked(x + 1, y, std::min(w - 2, 60), parts, total, 1);
            put(x + std::min(w - 2, 60) + 3, y++, "borrowed vs. interest", C_DIM);
        }
    }
    // all loans: avalanche vs snowball
    if (count > 1 && y < bottom() - 3) {
        y++;
        double extra = 0;
        for (const auto &k : b.loans) extra += k.extra;
        y = heading(x, y, "ALL LOANS: PAYOFF ORDER (with " + util::money(extra, false) + "/month extra in total)", w);
        auto av = loans::plan(b.loans, extra, loans::AVALANCHE), sn = loans::plan(b.loans, extra, loans::SNOWBALL);
        auto show = [&](const char *name, const loans::PlanResult &r, int color) {
            if (y >= bottom()) return;
            std::string t = r.never ? "never finishes with these payments"
                                    : "debt-free " + b.loans[0].payment_month(r.months) + ", " + util::money(r.interest, false) + " interest";
            put(x + 1, y, name, color, -1, A_BOLD);
            put(x + 13, y++, util::cut(t + (r.order.empty() ? "" : "  (first: " + r.order.front() + ")"), (size_t)w - 14));
        };
        show("Avalanche", av, C_GOOD);
        show("Snowball", sn, C_ACCENT);
        if (!av.never && !sn.never && y < bottom())
            put(x + 1, y++, sn.interest - av.interest > 1 ? "Avalanche saves " + util::money(sn.interest - av.interest, false) + "; snowball gives quicker wins. Press 8 > Learn to compare."
                                                           : "Both plans cost about the same here: pick whichever keeps you going.", C_DIM);
    }
}

// =============================================================================
// Wants
// =============================================================================

void App::draw_wants() {
    draw_frame("A add a want or trip|Enter edit|D delete|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    y = heading(x, y, "IS IT WORTH IT?", w);
    if (sum.real_hourly > 0) {
        put(x + 1, y, "Your real hourly pay: ", C_DIM);
        put(x + 23, y, util::money(sum.real_hourly), C_GOOD, -1, A_BOLD);
        put(x + 23 + (int)util::money(sum.real_hourly).size(), y++, "  (take-home pay / " + util::fixed(sum.work_hours_year / 52, 0) + " hours worked a week)", C_DIM);
    } else {
        put(x + 1, y++, "Add your job with hours per week on the Income tab to see costs in hours of work.", C_OKAY);
    }
    put(x + 1, y, "Left over each month: ", C_DIM);
    put(x + 23, y++, util::money(sum.left_month, false), sum.left_month > 0 ? C_GOOD : C_WATCH, -1, A_BOLD);
    y++;
    if (b.wants.empty()) {
        y = wrap_text(x + 1, y, w - 2, "Thinking about buying something, or planning a trip? Press A. For a trip, Tally adds up gas, lodging, food, and the rest.", C_OKAY);
        wrap_text(x + 1, y + 1, w - 2, "Tally shows how many hours of work it costs and how long it would take to save for it. "
                                       "It isn't here to say no. Some things are worth it!", C_DIM);
        return;
    }
    put(x + 3, y++, util::fit("Want", 28) + util::pad_left("Price", 12) + util::pad_left("Hours of work", 15) + util::pad_left("Save for", 12), C_DIM);
    int count = (int)b.wants.size();
    sel[tab] = std::max(0, std::min(sel[tab], count - 1));
    for (int i = 0; i < count && y < bottom() - 5; i++) {
        const Want &wt = b.wants[i];
        auto m = budget::want_math(sum, wt.price);
        bool s = i == sel[tab];
        std::string save = sum.left_month <= 0 ? "-" : m.weeks_to_save <= 4.33 ? "now" : util::fixed(std::ceil(m.weeks_to_save), 0) + " weeks";
        std::string row = util::fit((wt.trip ? "Trip: " : "") + wt.name, 28) + util::pad_left(util::money(wt.price), 12) +
                          util::pad_left(sum.real_hourly > 0 ? util::fixed(m.hours, m.hours < 10 ? 1 : 0) + " hrs" : "-", 15) + util::pad_left(save, 12);
        int color = m.mood == 0 ? C_GOOD : m.mood == 1 ? C_OKAY : C_WATCH;
        put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 3, y, util::fit(row, (size_t)w - 5), -1, -1, s ? A_REVERSE : 0);
        put(x + 3 + (int)std::min<size_t>(util::width(row), (size_t)w - 5), y, " " + g_bullet(), color, -1, A_BOLD);
        hits.push_back({x, y, w, [this, i] { if (sel[tab] == i) edit_want((size_t)i); else sel[tab] = i; }});
        y++;
    }
    y++;
    const Want &wt = b.wants[sel[tab]];
    auto m = budget::want_math(sum, wt.price);
    int color = m.mood == 0 ? C_GOOD : m.mood == 1 ? C_OKAY : C_WATCH;
    y = heading(x, y, util::cut(wt.name, 50), w);
    if (wt.trip && y < bottom() - 2) {
        auto t = budget::trip_cost(wt, sum.region);
        std::string parts;
        auto add = [&](const char *n, double v) { if (v > 0.004) parts += (parts.empty() ? "" : "  " + g_bullet() + "  ") + n + " " + util::money(v, false); };
        add("gas", t.gas); add("car wear", t.wear); add("lodging", t.lodging); add("food", t.food); add("tickets", t.tickets); add("fun", t.fun);
        if (!parts.empty()) y = wrap_text(x + 1, y, w - 2, parts, C_DIM);
        int months = budget::months_until(wt.save_by);
        if (months > 0 && wt.price > 0)
            y = wrap_text(x + 1, y, w - 2, "Set aside " + util::money(std::ceil(wt.price / months), false) + " a month for " + std::to_string(months) +
                                               (months == 1 ? " month" : " months") + " to pay cash. Enter to add it to your budget.", C_ACCENT);
    }
    y = wrap_text(x + 1, y, w - 2, m.verdict, color, A_BOLD);
    if (sum.real_hourly > 0 && m.hours >= 1 && y < bottom())
        y = wrap_text(x + 1, y, w - 2, "That's about " + util::fixed(m.hours / 8.0, 1) + " work days. Would you trade that time for it?", C_DIM);
}

// =============================================================================
// My Area
// =============================================================================

void App::draw_area() {
    draw_frame("Z set ZIP code|N U.S./region prices|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    const auto &pl = sum.place;
    if (!pl.found) {
        y = heading(x, y, "YOUR AREA", w);
        wrap_text(x + 1, y + 1, w - 2, b.home.zip.empty() ? "Press Z to enter your ZIP code. It's looked up in data built into Tally and never sent anywhere."
                                                         : "Tally doesn't know ZIP " + b.home.zip + ". Press Z to try another (your home ZIP, not a PO box).", C_OKAY);
        return;
    }
    y = heading(x, y, pl.county + ", " + pl.state_name + " " + pl.zip + (pl.exact ? "" : " (nearby ZIP)"), w);
    put(x + 1, y++, "Census " + std::to_string(places::acs_year() - 4) + "-" + std::to_string(places::acs_year()) + " estimates. Half of households are above the median, half below.", C_DIM);
    y++;
    int cw = 14;
    put(x + 1, y++, util::fit("", 22) + util::pad_left("This ZIP", cw) + util::pad_left("County", cw) + util::pad_left("State", cw) + util::pad_left("U.S.", cw), C_DIM);
    auto row = [&](const std::string &label, int a, int c2, int s, int u, const std::string &suffix) {
        auto v = [&](int n) { return n > 0 ? util::money(n, false) + suffix : std::string("-"); };
        put(x + 1, y++, util::fit(label, 22) + util::pad_left(v(a), cw) + util::pad_left(v(c2), cw) + util::pad_left(v(s), cw) + util::pad_left(v(u), cw));
    };
    row("Household income", pl.zip_m.income, pl.county_m.income, pl.state_m.income, pl.us_m.income, "");
    row("Rent (incl. utilities)", pl.zip_m.rent, pl.county_m.rent, pl.state_m.rent, pl.us_m.rent, "");
    row("Home value", pl.zip_m.home, pl.county_m.home, pl.state_m.home, pl.us_m.home, "");
    y++;
    // you vs. here
    int local_rent = pl.zip_m.rent ? pl.zip_m.rent : pl.county_m.rent;
    int local_income = pl.zip_m.income ? pl.zip_m.income : pl.county_m.income;
    y = heading(x, y, "YOU VS. HERE", w);
    double gross_year = sum.tax.gross + sum.tax.untaxed;
    if (local_income > 0 && gross_year > 0) {
        double r = gross_year / local_income;
        put(x + 1, y, util::fit("Your household income", 26));
        hbar(x + 27, y, 30, std::min(1.0, r / 2), r >= 1 ? C_GOOD : C_OKAY);
        put(x + 59, y++, util::percent(r, 0) + " of the local median", C_DIM);
    }
    if (local_rent > 0 && sum.housing > 0) {
        double r = sum.housing / local_rent;
        put(x + 1, y, util::fit("Your housing costs", 26));
        hbar(x + 27, y, 30, std::min(1.0, r / 2), r <= 1 ? C_GOOD : C_OKAY);
        put(x + 59, y++, util::percent(r, 0) + " of local median rent", C_DIM);
    }
    if (gross_year <= 0 && sum.housing <= 0) put(x + 1, y++, "Add income and housing costs to compare.", C_DIM);
    y++;
    if (y >= bottom() - 3) return;
    // average prices
    int region = national ? 0 : sum.region;
    y = heading(x, y, "EVERYDAY PRICES (" + places::region_name(region) + ")", w);
    struct Item { const char *id; const char *name; const char *unit; bool regional; };
    const Item items[] = {{"74714", "Gas, regular", "gallon", true}, {"72610", "Electricity", "kWh", true},
                          {"72620", "Natural gas (utility)", "therm", true}, {"708111", "Eggs, grade A large", "dozen", false},
                          {"709112", "Milk, whole", "gallon", false}, {"702111", "Bread, white", "pound", false},
                          {"703112", "Ground beef", "pound", false}, {"706111", "Chicken, whole", "pound", false}};
    for (const auto &it : items) {
        if (y >= bottom()) break;
        std::string area = it.regional ? "0" + std::to_string(region) + "00" : "0000";
        std::string id = "APU" + area + it.id;
        const auto *pts = budget::series(id);
        if (!pts) continue;
        double ch = 0;
        bool has = budget::yoy(id, ch);
        std::string price = "$" + util::fixed(pts->back().v, it.id[0] == '7' && std::string(it.id) == "72610" ? 3 : 2) + " / " + it.unit;
        put(x + 1, y, util::fit(it.name, 24) + util::pad_left(price, 18) + "  ");
        if (has) put(x + 45, y, util::pad_left((ch >= 0 ? g_arrow_up() + " " : g_arrow_down() + " ") + util::percent(std::fabs(ch), 1), 9),
                     ch > 0.03 ? C_WATCH : ch >= 0 ? C_OKAY : C_GOOD);
        put(x + 56, y, util::month_name(pts->back().m, true) + " " + std::to_string(pts->back().y) + (it.regional ? "" : ", U.S."), C_DIM);
        y++;
    }
    const auto *kwh = budget::series("APU0" + std::to_string(region) + "0072610");
    if (kwh && y < bottom())
        put(x + 1, y++, "A typical home uses about 860 kWh a month, so a typical electric bill here is about " + util::money(kwh->back().v * 863, false) + ".", C_DIM);
}

// =============================================================================
// Prices (inflation by category)
// =============================================================================

void App::draw_prices() {
    if (bought_view) { draw_bought(); return; }
    draw_frame("N region/U.S.|B groceries bought|0 Settings > Update for newer data|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    int region = national ? 0 : sum.region;
    double all = 0;
    std::string month;
    budget::region_yoy(region, "SA0", all, &month);
    y = heading(x, y, "PRICE CHANGES, LAST 12 MONTHS (" + places::region_name(region) + (month.empty() ? "" : ", through " + month) + ")", w);
    put(x + 1, y, "Overall: ", C_DIM);
    put(x + 10, y, (all >= 0 ? "+" : "") + util::percent(all, 1), all > 0.03 ? C_WATCH : C_OKAY, -1, A_BOLD);
    put(x + 18, y++, "  Prices on average are " + std::string(all >= 0 ? "up " : "down ") + util::percent(std::fabs(all), 1) + " from a year ago.", C_DIM);
    y++;
    int sw = std::max(12, std::min(24, w - 72));
    put(x + 3, y++, util::fit("Category", 18) + util::pad_left("12 months", 10) + "  " + util::fit("2-year trend", (size_t)sw) + "  " +
                        util::pad_left("You spend", 11) + util::pad_left("Next year", 11), C_DIM);
    std::map<std::string, double> mine;
    for (const auto &c : sum.cats) mine[c.name] = c.monthly;
    for (const auto &c : b.categories) {
        if (y >= bottom() - 2) break;
        if (c.cpi.empty()) continue;
        std::string id = budget::cpi_id(region, c.cpi);
        double ch = 0;
        if (!budget::yoy(id, ch)) { id = budget::cpi_id(0, c.cpi); if (!budget::yoy(id, ch)) continue; }
        const auto *pts = budget::series(id);
        std::vector<double> vals;
        for (size_t i = pts->size() > 24 ? pts->size() - 24 : 0; i < pts->size(); i++) vals.push_back((*pts)[i].v);
        swatch(x, y, c.color);
        put(x + 3, y, util::fit(c.name, 18));
        put(x + 21, y, util::pad_left((ch >= 0 ? "+" : "") + util::percent(ch, 1), 10), ch > 0.04 ? C_WATCH : ch > 0.02 ? C_OKAY : C_GOOD, -1, A_BOLD);
        put(x + 33, y, spark(vals), c.color);
        auto it = mine.find(c.name);
        if (it != mine.end()) {
            put(x + 35 + sw, y, util::pad_left(util::money(it->second, false), 11));
            put(x + 46 + sw, y, util::pad_left(util::money(it->second * (1 + ch), false), 11), C_DIM);
        }
        put(x + 58 + sw, y, util::fit(cpi_name(c.cpi), (size_t)std::max(0, w - 58 - sw)), C_DIM);
        y++;
    }
    y++;
    if (sum.spend_month > 0 && y < bottom())
        y = wrap_text(x + 1, y, w - 2, "If prices keep changing like this, your monthly spending of " + util::money(sum.spend_month, false) + " becomes about " +
                                       util::money(sum.next_year_month, false) + " a year from now.", -1, A_BOLD);
    if (y < bottom())
        put(x + 1, y, util::cut("Source: Bureau of Labor Statistics consumer price index. Data pack " + datapack::active().date +
                                    " (" + datapack::active().origin + ").", (size_t)w - 2), C_DIM);
}

// =============================================================================
// Learn
// =============================================================================

void App::draw_learn() {
    draw_frame("Up/Down pick a lesson|PgUp/PgDn scroll|? explain|Q quit");
    int y = top(), x = 1;
    const auto &ls = lessons();
    sel[tab] = std::max(0, std::min(sel[tab], (int)ls.size() - 1));
    int lw = std::min(38, W() / 3);
    for (int i = 0; i < (int)ls.size() && y < bottom(); i++) {
        bool s = i == sel[tab];
        put(x, y, util::fit((s ? "> " : "  ") + std::string(ls[i].title), (size_t)lw), s ? C_ACCENT : -1, -1, s ? A_BOLD | A_REVERSE : 0);
        hits.push_back({x, y, lw, [this, i] { sel[tab] = i; learn_scroll = 0; }});
        y++;
    }
    int cx = x + lw + 2, cw = std::min(W() - cx - 2, 76);
    int cy = heading(cx, top(), util::cut(ls[sel[tab]].title, (size_t)cw), cw) + 1;
    auto lines = util::wrap(ls[sel[tab]].body, (size_t)cw);
    learn_scroll = std::max(0, std::min(learn_scroll, (int)lines.size() - 1));
    for (int i = learn_scroll; i < (int)lines.size() && cy < bottom(); i++) {
        const std::string &l = lines[i];
        bool bullet = util::starts_with(l, "- ") || (l.size() > 2 && std::isdigit((unsigned char)l[0]) && l[1] == '.');
        put(cx, cy++, l, bullet ? C_ACCENT : -1);
    }
}

// =============================================================================
// Print & export
// =============================================================================

static const char *PRINT_ITEMS[] = {
    "Printable page (PDF)", "Spreadsheet (Excel .xlsx)", "Simple table (CSV)", "Plain text", "Print now",
    "Open the last saved file", "Open the export folder", "Change the export folder"};
static const char *PRINT_HINTS[] = {
    "A nicely laid out summary with color charts, every expense, taxes, loans, and prices. Best for printing or sharing.",
    "Opens in Excel, LibreOffice, Google Sheets, or Numbers. Real formulas: change an amount and totals update.",
    "A plain table of your expenses. Almost any program can open it, including bank and budgeting tools.",
    "A text file that opens and prints anywhere, even on very old computers.",
    "Makes the PDF and sends it straight to your default printer.",
    "Opens the file you saved most recently.",
    "Opens the folder where Tally saves your files.",
    "Pick a different folder for saved files (a USB stick works too)."};

void App::draw_print() {
    draw_frame("Up/Down choose|Enter do it|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    y = heading(x, y, "PRINT & EXPORT", w);
    sel[tab] = std::max(0, std::min(sel[tab], 7));
    for (int i = 0; i < 8; i++) {
        bool s = i == sel[tab];
        put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 3, y, util::fit(std::to_string(i + 1) + "  " + PRINT_ITEMS[i], 34), -1, -1, s ? A_REVERSE : 0);
        hits.push_back({x, y, 40, [this, i] { if (sel[tab] == i) do_export(i); else sel[tab] = i; }});
        y++;
    }
    y++;
    y = wrap_text(x + 1, y, std::min(w - 2, 80), PRINT_HINTS[sel[tab]], C_DIM) + 1;
    put(x + 1, y++, "Saves to: " + report::export_folder(b), -1);
    if (!last_file.empty()) put(x + 1, y++, util::cut("Last saved: " + last_file, (size_t)w - 2), C_GOOD);
}

void App::do_export(int which) {
    std::string folder = report::export_folder(b), err, out;
    if (which == 5) {
        if (last_file.empty()) say("Nothing saved yet this time.", C_OKAY);
        else util::open_with_system(last_file);
        return;
    }
    if (which == 6) { util::make_dirs(folder); util::open_with_system(folder); return; }
    if (which == 7) {
        std::string f = b.settings.export_dir.empty() ? folder : b.settings.export_dir;
        if (input("Save files to this folder", f, "Type a full folder path. Leave it blank for Documents/Tally.")) {
            b.settings.export_dir = util::trim(f);
            save();
            say("Files will be saved to " + report::export_folder(b), C_GOOD);
        }
        return;
    }
    say("Working...", C_DIM);
    paint();
    present();
    switch (which) {
        case 0: case 4: out = report::pdf(b, folder, err); break;
        case 1: out = report::xlsx(b, folder, err); break;
        case 2: out = report::csv(b, folder, err); break;
        case 3: out = report::txt(b, folder, err); break;
    }
    if (out.empty()) { say(err, C_WATCH); return; }
    last_file = out;
    if (which == 4) {
        std::string m;
        report::print_file(out, m);
        say(m, C_GOOD);
    } else {
        say("Saved " + out + "   (press 6 to open it)", C_GOOD);
    }
}

// =============================================================================
// Settings
// =============================================================================

void App::draw_settings() {
    draw_frame("Up/Down choose|Enter change|Left/Right switch|? explain|Q quit");
    int y = top(), x = 1, w = W() - 2;
    const DataPack &dp = datapack::active();
    std::string prices_through;
    if (const auto *p = budget::series("CUUR0000SA0")) prices_through = util::month_name(p->back().m, true) + " " + std::to_string(p->back().y);
    struct Row { std::string label, value, hint; };
    static const char *cm[] = {"Full color", "Basic 16 colors", "No color"};
    std::vector<Row> rows = {
        {"Colors", cm[std::max(0, std::min(2, b.settings.color_mode))], "Basic works on old terminals and the Linux text console. No color for screen readers or printing the screen."},
        {"Drawing", b.settings.ascii ? "Plain ASCII" : "Unicode blocks", "Plain ASCII draws charts with # - | if you see boxes or question marks."},
        {"Checkups on Home", b.settings.tips ? "On" : "Off", "Friendly rules of thumb based on your numbers."},
        {"Spending categories...", std::to_string(b.categories.size()) + " categories", "Rename, recolor, add, or link a category to the inflation it should follow."},
        {"Update Tally...", "version " TALLY_VERSION, "Get the newest Tally from GitHub and install it. Your budget isn't touched. Shows exactly what it sends first."},
        {"Update tax tables & prices...", "", "Optional. Shows exactly what it would download first, and nothing about you is sent."},
        {"Import a data pack file...", "", "No internet? Load tax tables and prices from a file (for example, from a USB stick)."},
        {"Save the data pack to a file...", "", "Share your current tax tables and prices with another computer running Tally."},
        {"Restore a backup...", "", "Tally keeps a dated copy of your budget each day you use it (the last 30)."},
        {"Update address", b.settings.update_url, "Where Update downloads tax tables from. Change it only if you know you need to."},
        {"Server link (optional)...", b.link.on ? "On: " + b.link.url : "Off",
         "Connect to a server you run yourself, like a phone page for scanning barcodes at the store. Tally only downloads from it; your budget is never uploaded."},
        {"About & privacy", "", "What Tally is, where your data lives, and how it stays private."},
    };
    y = heading(x, y, "SETTINGS", w);
    sel[tab] = std::max(0, std::min(sel[tab], (int)rows.size() - 1));
    for (int i = 0; i < (int)rows.size(); i++) {
        bool s = i == sel[tab];
        put(x + 1, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 3, y, util::fit(rows[i].label, 32), -1, -1, s ? A_REVERSE : 0);
        put(x + 37, y, util::cut(rows[i].value, (size_t)std::max(0, w - 38)), C_ACCENT);
        hits.push_back({x, y, w, [this, i] { if (sel[tab] == i) handle(Event{K_ENTER}); else sel[tab] = i; }});
        y++;
    }
    y++;
    y = wrap_text(x + 1, y, std::min(w - 2, 90), rows[sel[tab]].hint, C_DIM) + 1;
    if (y < bottom()) put(x + 1, y++, util::cut("Your budget file: " + path, (size_t)w - 2));
    if (y < bottom()) put(x + 1, y++, "Tax tables: " + std::to_string(dp.tax_year) + " (" + dp.origin + ", " + dp.date + ")" +
                                          (prices_through.empty() ? "" : "   Prices through: " + prices_through), C_DIM);
    if (y < bottom()) put(x + 1, y++, "ZIP data: Census " + std::to_string(places::acs_year()) + " (built in)", C_DIM);
}

// =============================================================================
// Forms
// =============================================================================

Field App::f_text(const std::string &label, std::string &ref, const std::string &hint) {
    Field f{label, ref.empty() ? "(blank)" : ref, hint, nullptr, nullptr};
    f.edit = [this, &ref, label, hint] { std::string v = ref; if (input(label, v, hint)) { ref = util::trim(v); } };
    return f;
}

Field App::f_money(const std::string &label, double &ref, const std::string &hint, bool allow_negative) {
    Field f{label, util::money(ref), hint, nullptr, nullptr};
    f.edit = [this, &ref, label, hint, allow_negative] { input_number(label, ref, hint, true, allow_negative); };
    return f;
}

Field App::f_num(const std::string &label, double &ref, const std::string &suffix, const std::string &hint) {
    std::string v = util::fixed(ref, 2);
    while (v.find('.') != std::string::npos && (v.back() == '0' || v.back() == '.')) { bool d = v.back() == '.'; v.pop_back(); if (d) break; }
    Field f{label, v + suffix, hint, nullptr, nullptr};
    f.edit = [this, &ref, label, hint] { input_number(label, ref, hint); };
    return f;
}

Field App::f_int(const std::string &label, int &ref, int lo, int hi, const std::string &hint, const std::string &zero) {
    Field f{label, ref == 0 && !zero.empty() ? zero : std::to_string(ref), hint, nullptr, nullptr};
    f.edit = [this, &ref, lo, hi, label, hint] {
        double v = ref;
        if (input_number(label, v, hint)) ref = std::max(lo, std::min(hi, (int)std::lround(v)));
    };
    f.step = [&ref, lo, hi](int d) { ref = std::max(lo, std::min(hi, ref + d)); };
    return f;
}

Field App::f_choice(const std::string &label, int &ref, std::vector<std::string> opts, const std::string &hint, std::vector<std::string> hints) {
    int cur = std::max(0, std::min((int)opts.size() - 1, ref));
    std::string h = hint;
    if (cur < (int)hints.size() && !hints[cur].empty()) h += (h.empty() ? "" : "\n") + hints[cur];
    Field f{label, opts[cur], h, nullptr, nullptr};
    f.edit = [this, &ref, opts, label, hints] { int c = choose(label, opts, ref, {}, hints); if (c >= 0) ref = c; };
    int n = (int)opts.size();
    f.step = [&ref, n](int d) { ref = ((ref + d) % n + n) % n; };
    return f;
}

Field App::f_bool(const std::string &label, bool &ref, const std::string &on, const std::string &off, const std::string &hint) {
    Field f{label, ref ? on : off, hint, nullptr, nullptr};
    f.edit = [&ref] { ref = !ref; };
    f.step = [&ref](int) { ref = !ref; };
    return f;
}

Field App::f_freq(const std::string &label, Freq &ref, const std::string &hint) {
    std::vector<std::string> opts;
    for (int i = 0; i < FREQ_COUNT; i++) opts.push_back(freq_name((Freq)i));
    Field f{label, freq_name(ref), hint, nullptr, nullptr};
    f.edit = [this, &ref, opts, label] { int c = choose(label, opts, ref); if (c >= 0) ref = (Freq)c; };
    f.step = [&ref](int d) { ref = (Freq)(((int)ref + d + FREQ_COUNT) % FREQ_COUNT); };
    return f;
}

Field App::f_category(std::string &ref) {
    const Category *c = b.category(ref);
    Field f{"Category", ref, c ? c->help : "", nullptr, nullptr};
    f.edit = [this, &ref] {
        std::vector<std::string> names, hints;
        std::vector<int> colors;
        int cur = 0;
        for (size_t i = 0; i < b.categories.size(); i++) {
            names.push_back(b.categories[i].name);
            colors.push_back(b.categories[i].color);
            hints.push_back(b.categories[i].help);
            if (b.categories[i].name == ref) cur = (int)i;
        }
        int k = choose("Category", names, cur, colors, hints);
        if (k >= 0) ref = names[k];
    };
    f.step = [this, &ref](int d) {
        int n = (int)b.categories.size(), cur = 0;
        for (int i = 0; i < n; i++) if (b.categories[i].name == ref) cur = i;
        ref = b.categories[((cur + d) % n + n) % n].name;
    };
    return f;
}

void App::run_form(const std::string &title, std::function<std::vector<Field>()> fields,
                   std::function<std::vector<std::pair<std::string, int>>()> preview) {
    FormState fs{title, fields, preview, 0};
    FormState *outer = form_;
    form_ = &fs;
    for (;;) {
        recalc();
        paint();
        present();
        Event e = read(-1);
        auto fl = fs.fields();
        int n = (int)fl.size();
        if (e.key == K_RESIZE) { invalidate(); continue; }
        if (e.key == K_ESC || e.key == 'q' || e.key == 'Q' || e.key == K_CLOSED || (e.key == K_ENTER && fs.sel == n)) break;
        if (e.key == K_UP || (e.key == K_TAB && false)) { do fs.sel = (fs.sel + n) % (n + 1); while (fs.sel < n && fl[fs.sel].info); }
        else if (e.key == K_DOWN || e.key == K_TAB) { do fs.sel = (fs.sel + 1) % (n + 1); while (fs.sel < n && fl[fs.sel].info); }
        else if (e.key == K_BACKTAB) { fs.sel = (fs.sel + n) % (n + 1); }
        else if (e.key == K_HOME) fs.sel = 0;
        else if (e.key == K_END) fs.sel = n;
        else if ((e.key == K_LEFT || e.key == K_RIGHT) && fs.sel < n && fl[fs.sel].step) fl[fs.sel].step(e.key == K_LEFT ? -1 : 1);
        else if (e.key == K_ENTER && fs.sel < n && fl[fs.sel].edit) fl[fs.sel].edit();
        else if (e.key == K_MOUSE && e.button == 0) {
            for (const auto &h : hits)
                if (e.my == h.y && e.mx >= h.x && e.mx < h.x + h.w) { h.fn(); break; }
            if (!form_) break;  // a tab was clicked
        } else if (e.key == K_MOUSE && (e.button == 64 || e.button == 65)) {
            fs.sel = std::max(0, std::min(n, fs.sel + (e.button == 64 ? -1 : 1)));
        }
        save();
        if (fs.sel > n) break;  // "Done" clicked, or the item was deleted
    }
    form_ = outer;
    save();
}

void App::draw_form() {
    FormState &fs = *form_;
    draw_frame("Up/Down choose|Enter change|Left/Right switch choices|Esc done");
    auto fl = fs.fields();
    int n = (int)fl.size();
    fs.sel = std::max(0, std::min(fs.sel, n));
    int x = 1, y = top() - 1, w = W() - 2;
    put(1, 1, fs.title, -1, -1, A_BOLD);
    bool wide = w >= 96;
    int fw = wide ? std::min(70, w * 6 / 10) : w;
    int lw = 33;
    for (int i = 0; i < n && y < bottom() - 4; i++) {
        const Field &f = fl[i];
        bool s = i == fs.sel;
        if (f.info) {
            put(x + 1, y++, util::cut(f.label + f.value, (size_t)fw - 2), C_DIM);
            continue;
        }
        put(x, y, s ? ">" : " ", C_ACCENT, -1, A_BOLD);
        put(x + 2, y, util::fit(f.label, (size_t)lw - 2), s ? -1 : C_DIM, -1, s ? A_BOLD : 0);
        std::string v = f.value;
        if (f.step && s) v = "< " + v + " >";
        put(x + lw, y, util::fit(v, (size_t)std::max(4, fw - lw)), -1, -1, s ? A_REVERSE : 0);
        int idx = i;
        hits.push_back({x, y, fw, [&fs, idx, fl] {
                            if (fs.sel == idx && fl[idx].edit) { fl[idx].edit(); }
                            fs.sel = idx;
                        }});
        y++;
    }
    y++;
    bool done_sel = fs.sel == n;
    put(x, y, done_sel ? ">" : " ", C_ACCENT, -1, A_BOLD);
    put(x + 2, y, " Done ", done_sel ? C_BAR_FG : C_GOOD, done_sel ? C_GOOD : -1, A_BOLD);
    put(x + 9, y, "(changes save as you go)", C_DIM);
    hits.push_back({x, y, 8, [this] { form_->sel = 1 << 20; }});
    y += 2;
    // what this field means (educational)
    if (fs.sel < n && !fl[fs.sel].hint.empty() && y < bottom())
        y = wrap_text(x + 2, y, fw - 4, fl[fs.sel].hint, C_DIM, 0, bottom() - y);
    // live preview
    if (fs.preview) {
        auto pv = fs.preview();
        int px = wide ? fw + 4 : x + 2, py = wide ? top() - 1 : y + 1, pw = wide ? w - fw - 4 : w - 4;
        if (!pv.empty() && py < bottom()) {
            py = heading(px, py, "WHAT THIS MEANS", pw);
            for (const auto &line : pv) {
                if (py >= bottom()) break;
                py = wrap_text(px, py, pw, line.first, line.second, line.second == C_GOOD || line.second == C_WATCH ? A_BOLD : 0);
            }
        }
    }
}

// =============================================================================
// Modal widgets
// =============================================================================

bool App::input(const std::string &label, std::string &value, const std::string &hint) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < value.size();) {
        unsigned char c = (unsigned char)value[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        chars.push_back(value.substr(i, len));
        i += len;
    }
    size_t cur = chars.size();
    for (;;) {
        paint();
        int y = H() - 4;
        fill(0, y, W(), 4, -1);
        std::string line;
        for (int i = 0; i < W(); i++) line += g_hline();
        put(0, y, line, C_ACCENT);
        // a problem with what was typed replaces the hint, right where you're looking
        if (!input_error.empty()) put(1, y + 1, util::cut(input_error, (size_t)W() - 2), C_WATCH, -1, A_BOLD);
        else if (!hint.empty()) put(1, y + 1, util::cut(hint, (size_t)W() - 2), C_DIM);
        std::string pre = label + " ] ";
        put(1, y + 2, pre, C_ACCENT, -1, A_BOLD);
        int avail = W() - 3 - (int)util::width(pre);
        size_t start = cur > (size_t)avail ? cur - avail : 0;
        std::string shown;
        for (size_t i = start; i < chars.size() && (int)(i - start) < avail; i++) shown += chars[i];
        put(1 + (int)util::width(pre), y + 2, shown);
        draw_keys("Enter OK|Esc cancel|Ctrl-U clear");
        cursor(1 + (int)util::width(pre) + (int)(cur - start), y + 2);
        present();
        Event e = read(-1);
        switch (e.key) {
            case K_ENTER: {
                input_error.clear();
                value.clear();
                for (auto &c : chars) value += c;
                cursor(-1, -1);
                return true;
            }
            case K_ESC: case K_CLOSED: cursor(-1, -1); input_error.clear(); return false;
            case K_BACKSPACE: if (cur > 0) chars.erase(chars.begin() + (long)--cur); break;
            case K_DELETE: if (cur < chars.size()) chars.erase(chars.begin() + (long)cur); break;
            case K_LEFT: if (cur > 0) cur--; break;
            case K_RIGHT: if (cur < chars.size()) cur++; break;
            case K_HOME: cur = 0; break;
            case K_END: cur = chars.size(); break;
            case K_RESIZE: invalidate(); break;
            default:
                if (e.ctrl && e.key == 'u') { chars.clear(); cur = 0; }
                else if (!e.ctrl && e.key >= 32 && e.key < 0x110000) {
                    std::string u;
                    int cp = e.key;
                    if (cp < 0x80) u += (char)cp;
                    else if (cp < 0x800) { u += (char)(0xC0 | (cp >> 6)); u += (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { u += (char)(0xE0 | (cp >> 12)); u += (char)(0x80 | ((cp >> 6) & 0x3F)); u += (char)(0x80 | (cp & 0x3F)); }
                    else { u += (char)(0xF0 | (cp >> 18)); u += (char)(0x80 | ((cp >> 12) & 0x3F)); u += (char)(0x80 | ((cp >> 6) & 0x3F)); u += (char)(0x80 | (cp & 0x3F)); }
                    chars.insert(chars.begin() + (long)cur++, u);
                }
        }
    }
}

bool App::input_number(const std::string &label, double &value, const std::string &hint, bool allow_blank_zero, bool allow_negative) {
    std::string s = value == 0 ? "" : util::fixed(value, 2);
    while (s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) { bool d = s.back() == '.'; s.pop_back(); if (d) break; }
    for (;;) {
        if (!input(label, s, hint)) return false;
        double v = 0;
        if (util::trim(s).empty() && allow_blank_zero) { value = 0; return true; }
        if (util::parse_number(s, v) && (v >= 0 || allow_negative)) { value = v; return true; }
        input_error = util::parse_number(s, v) ? "This can't be negative. Type an amount like 1250 or 12.50 (Esc to cancel)."
                                               : "That isn't a number. Type an amount like 1250 or 12.50 (Esc to cancel).";
    }
}

int App::choose(const std::string &title, const std::vector<std::string> &opts, int current, const std::vector<int> &colors,
                const std::vector<std::string> &hints) {
    int n = (int)opts.size(), s = std::max(0, std::min(n - 1, current)), off = 0;
    if (n == 0) return -1;
    for (;;) {
        paint();
        int bw = (int)util::width(title) + 6;
        for (const auto &o : opts) bw = std::max(bw, (int)util::width(o) + 8);
        bw = std::min(bw + 2, W() - 4);
        bool has_hints = !hints.empty();
        if (has_hints) bw = std::max(bw, std::min(W() - 4, 60));
        int hint_rows = has_hints ? 3 : 0;
        int bh = std::min(n, H() - 8 - hint_rows) + 2 + hint_rows;
        int vis = bh - 2 - hint_rows;
        int bx = (W() - bw) / 2, by = std::max(1, (H() - bh) / 2);
        if (s < off) off = s;
        if (s >= off + vis) off = s - vis + 1;
        fill(bx, by, bw, bh, -1);
        std::string hl;
        for (int i = 0; i < bw; i++) hl += g_hline();
        put(bx, by, hl, C_ACCENT);
        put(bx + 2, by, " " + title + " ", C_ACCENT, -1, A_BOLD);
        put(bx, by + bh - 1, hl, C_ACCENT);
        for (int r = 1; r < bh - 1; r++) { put(bx, by + r, g_vline(), C_ACCENT); put(bx + bw - 1, by + r, g_vline(), C_ACCENT); }
        hits.clear();
        for (int i = off; i < n && i < off + vis; i++) {
            int yy = by + 1 + i - off;
            bool sel_ = i == s;
            int tx = bx + 2;
            if (!colors.empty()) { swatch(tx, yy, colors[i]); tx += 3; }
            put(tx, yy, util::fit(opts[i], (size_t)(bx + bw - 2 - tx)), -1, -1, sel_ ? A_REVERSE | A_BOLD : 0);
            int k = i;
            hits.push_back({bx, yy, bw, [&s, k] { s = k; }});
        }
        if (has_hints && s < (int)hints.size()) {
            int yy = by + bh - 1 - hint_rows;
            for (const auto &l : util::wrap(hints[s], (size_t)bw - 4)) {
                if (yy >= by + bh - 1) break;
                put(bx + 2, yy++, l, C_DIM);
            }
        }
        draw_keys("Up/Down choose|Enter pick|Esc cancel");
        present();
        Event e = read(-1);
        if (e.key == K_ESC || e.key == 'q' || e.key == K_CLOSED) return -1;
        if (e.key == K_ENTER) return s;
        if (e.key == K_UP) s = (s + n - 1) % n;
        else if (e.key == K_DOWN) s = (s + 1) % n;
        else if (e.key == K_PGUP) s = std::max(0, s - vis);
        else if (e.key == K_PGDN) s = std::min(n - 1, s + vis);
        else if (e.key == K_HOME) s = 0;
        else if (e.key == K_END) s = n - 1;
        else if (e.key == K_RESIZE) invalidate();
        else if (e.key == K_MOUSE && e.button == 0) {
            int before = s;
            bool hit = false;
            for (const auto &h : hits)
                if (e.my == h.y && e.mx >= h.x && e.mx < h.x + h.w) { h.fn(); hit = true; break; }
            if (!hit) return -1;
            if (before == s) return s;
        } else if (e.key == K_MOUSE && (e.button == 64 || e.button == 65)) {
            s = std::max(0, std::min(n - 1, s + (e.button == 64 ? -1 : 1)));
        } else if (e.key > 32 && e.key < 127) {  // jump to the next option starting with that letter
            for (int k = 1; k <= n; k++) {
                int i = (s + k) % n;
                if (!opts[i].empty() && std::tolower((unsigned char)opts[i][0]) == std::tolower(e.key)) { s = i; break; }
            }
        }
    }
}

// A month calendar to pick a day. Arrows move a day or a week, PgUp/PgDn (or < >) a month,
// T jumps to today, Enter picks, Delete clears. Mouse: click a day, click again to pick.
bool App::pick_date(const std::string &title, std::string &date, const std::string &hint, bool allow_clear) {
    util::Date t = util::today();
    long today_n = day_number(t.y, t.m, t.d);
    int y, m, d;
    long cur = parse_date(date, y, m, d) ? day_number(y, m, d) : today_n;
    for (;;) {
        from_day_number(cur, y, m, d);
        paint();
        int bw = 36, bh = 15, bx = (W() - bw) / 2, by = std::max(1, (H() - bh) / 2);
        fill(bx, by, bw, bh, -1);
        std::string hl;
        for (int i = 0; i < bw; i++) hl += g_hline();
        put(bx, by, hl, C_ACCENT);
        put(bx + 2, by, " " + util::cut(title, (size_t)bw - 6) + " ", C_ACCENT, -1, A_BOLD);
        put(bx, by + bh - 1, hl, C_ACCENT);
        for (int r = 1; r < bh - 1; r++) { put(bx, by + r, g_vline(), C_ACCENT); put(bx + bw - 1, by + r, g_vline(), C_ACCENT); }
        hits.clear();
        std::string head = util::month_name(m) + " " + std::to_string(y);
        put(bx + 3, by + 1, "<", C_KEY, -1, A_BOLD);
        put(bx + (bw - (int)util::width(head)) / 2, by + 1, head, -1, -1, A_BOLD);
        put(bx + bw - 4, by + 1, ">", C_KEY, -1, A_BOLD);
        auto shift = [&cur](int months) {
            int yy, mm, dd;
            from_day_number(cur, yy, mm, dd);
            int tot = yy * 12 + (mm - 1) + months;
            yy = tot / 12; mm = tot % 12 + 1;
            cur = day_number(yy, mm, std::min(dd, util::days_in_month(yy, mm)));
        };
        hits.push_back({bx + 1, by + 1, 5, [&shift] { shift(-1); }});
        hits.push_back({bx + bw - 6, by + 1, 5, [&shift] { shift(1); }});
        static const char *wd[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        int gx = bx + (bw - 7 * 4) / 2 + 1;
        for (int i = 0; i < 7; i++) put(gx + i * 4, by + 3, wd[i], C_DIM);
        long first = day_number(y, m, 1);
        int lead = (int)(((first + 4) % 7 + 7) % 7);   // 1970-01-01 was a Thursday
        int dim = util::days_in_month(y, m);
        for (int day = 1; day <= dim; day++) {
            int cell = lead + day - 1, cx = gx + (cell % 7) * 4, cy = by + 4 + cell / 7;
            long n = first + day - 1;
            std::string txt = (day < 10 ? " " : "") + std::to_string(day);
            int attr = n == cur ? A_REVERSE | A_BOLD : 0;
            put(cx - 1, cy, " " + txt + " ", n == today_n ? C_ACCENT : -1, -1, attr | (n == today_n ? A_BOLD : 0));
            hits.push_back({cx - 1, cy, 4, [&cur, n] { cur = n; }});
        }
        static const char *wdl[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        put(bx + 2, by + 11, util::cut(std::string(wdl[(cur + 4) % 7]) + ", " + util::date_text({y, m, d}), (size_t)bw - 4), C_GOOD, -1, A_BOLD);
        auto lines = util::wrap(hint, (size_t)bw - 4);
        for (size_t i = 0; i < lines.size() && i < 2; i++) put(bx + 2, by + 12 + (int)i, lines[i], C_DIM);
        draw_keys(std::string("Arrows move|PgUp/PgDn month|T today|Enter pick|") + (allow_clear ? "Del clear|" : "") + "Esc cancel");
        present();
        Event e = read(-1);
        if (e.key == K_ESC || e.key == 'q' || e.key == K_CLOSED) return false;
        if (e.key == K_ENTER) { date = date_key(y, m, d); return true; }
        if ((e.key == K_DELETE || e.key == K_BACKSPACE) && allow_clear) { date.clear(); return true; }
        if (e.key == K_LEFT) cur--;
        else if (e.key == K_RIGHT) cur++;
        else if (e.key == K_UP) cur -= 7;
        else if (e.key == K_DOWN) cur += 7;
        else if (e.key == K_PGUP || e.key == '<' || e.key == ',') shift(-1);
        else if (e.key == K_PGDN || e.key == '>' || e.key == '.') shift(1);
        else if (e.key == 't' || e.key == 'T') cur = today_n;
        else if (e.key == K_RESIZE) invalidate();
        else if (e.key == K_MOUSE && (e.button == 64 || e.button == 65)) shift(e.button == 64 ? -1 : 1);
        else if (e.key == K_MOUSE && e.button == 0) {
            long before = cur;
            bool hit = false;
            for (const auto &h : std::vector<Hit>(hits))
                if (e.my == h.y && e.mx >= h.x && e.mx < h.x + h.w) { h.fn(); hit = true; break; }
            if (!hit) return false;
            if (before == cur && e.my >= by + 4) { from_day_number(cur, y, m, d); date = date_key(y, m, d); return true; }
        }
    }
}

Field App::f_date(const std::string &label, std::string &ref, const std::string &hint, std::function<void()> after) {
    int y, m, d;
    std::string shown = parse_date(ref, y, m, d) ? util::date_text({y, m, d}) : "(not set)";
    Field f{label, shown, hint + " Enter opens a calendar.", nullptr, nullptr};
    f.edit = [this, &ref, label, hint, after] { if (pick_date(label, ref, hint) && after) after(); };
    return f;
}

// A yes/no question as a tiny menu: Enter picks what's highlighted, Y/N and clicks work too.
// Deleting starts on "No" so a stray Enter never throws anything away.
bool App::confirm(const std::string &q, bool default_yes) {
    bool yes = default_yes;
    for (;;) {
        paint();
        auto lines = util::wrap(q, (size_t)W() - 2);
        if (lines.size() > 4) lines.resize(4);
        int n = (int)lines.size(), y = H() - 2 - n;
        fill(0, y, W(), n + 1, -1);
        for (const auto &l : lines) put(1, y++, l, C_OKAY, -1, A_BOLD);
        y--;
        hits.clear();
        int x = 3;
        for (int i = 0; i < 2; i++) {
            bool is_yes = i == 0, on = is_yes == yes;
            std::string label = is_yes ? "  Yes  " : "  No  ";
            put(x, y + 1, on ? ">" : " ", C_ACCENT, -1, A_BOLD);
            put(x + 1, y + 1, label, -1, -1, on ? A_REVERSE | A_BOLD : 0);
            hits.push_back({x, y + 1, (int)label.size() + 1, [&yes, is_yes] { yes = is_yes; }});
            x += (int)label.size() + 4;
        }
        draw_keys("Left/Right choose|Enter OK|Y yes|N no|Esc no");
        present();
        Event e = read(-1);
        if (e.key == 'y' || e.key == 'Y') return true;
        if (e.key == 'n' || e.key == 'N' || e.key == K_ESC || e.key == K_CLOSED) return false;
        if (e.key == K_ENTER) return yes;
        if (e.key == K_LEFT || e.key == K_RIGHT || e.key == K_TAB || e.key == K_UP || e.key == K_DOWN) yes = !yes;
        else if (e.key == K_RESIZE) invalidate();
        else if (e.key == K_MOUSE && e.button == 0) {
            for (const auto &h : hits)
                if (e.my == h.y && e.mx >= h.x && e.mx < h.x + h.w) { h.fn(); return yes; }
        }
    }
}

void App::show_text(const std::string &title, const std::string &body) {
    int off = 0;
    for (;;) {
        paint();
        int bw = std::min(W() - 4, 80), bx = (W() - bw) / 2, by = 2, bh = H() - 4;
        auto lines = util::wrap(body, (size_t)bw - 4);
        int vis = bh - 2;
        off = std::max(0, std::min(off, (int)lines.size() - vis));
        fill(bx, by, bw, bh, -1);
        std::string hl;
        for (int i = 0; i < bw; i++) hl += g_hline();
        put(bx, by, hl, C_ACCENT);
        put(bx + 2, by, " " + title + " ", C_ACCENT, -1, A_BOLD);
        put(bx, by + bh - 1, hl, C_ACCENT);
        for (int r = 1; r < bh - 1; r++) { put(bx, by + r, g_vline(), C_ACCENT); put(bx + bw - 1, by + r, g_vline(), C_ACCENT); }
        for (int i = 0; i < vis && off + i < (int)lines.size(); i++) put(bx + 2, by + 1 + i, lines[off + i]);
        if ((int)lines.size() > vis) put(bx + bw - 12, by + bh - 1, " more " + g_arrow_down() + " ", C_DIM);
        draw_keys("Up/Down scroll|Esc close");
        present();
        Event e = read(-1);
        if (e.key == K_UP || (e.key == K_MOUSE && e.button == 64)) off--;
        else if (e.key == K_DOWN || (e.key == K_MOUSE && e.button == 65)) off++;
        else if (e.key == K_PGUP) off -= vis;
        else if (e.key == K_PGDN || e.key == ' ') off += vis;
        else if (e.key == K_RESIZE) invalidate();
        else if (e.key == K_ESC || e.key == K_ENTER || e.key == 'q' || e.key == '?' || e.key == K_MOUSE || e.key == K_CLOSED) return;
    }
}

// =============================================================================
// Editors
// =============================================================================

void App::edit_household() {
    Household &h = b.home;
    run_form("Household (shared by everyone's taxes)",
        [this, &h] {
            std::vector<Field> f;
            Field zip{"ZIP code", h.zip.empty() ? "(not set)" : h.zip,
                      "Your home ZIP code. Sets your state for taxes and local costs. It's looked up in data built into Tally; it never leaves your computer.", nullptr, nullptr};
            zip.edit = [this, &h] {
                std::string z = h.zip;
                if (input("ZIP code", z, "5 digits. It's looked up on this computer only.")) {
                    h.zip = util::trim(z);
                    auto pl = places::lookup(h.zip);
                    if (pl.found) { h.state = pl.state; say("Found " + pl.county + ", " + pl.state, C_GOOD); }
                    else if (!h.zip.empty()) say("Tally doesn't know that ZIP. Pick your state below instead.", C_OKAY);
                }
            };
            f.push_back(zip);
            Field st{"State", h.state.empty() ? "(not set)" : places::state_name(h.state) + " (" + h.state + ")",
                     "Set automatically from your ZIP. Change it if you live in one state and work in another (use the state you file taxes in).", nullptr, nullptr};
            st.edit = [this, &h] {
                std::vector<std::string> codes = {"AL","AK","AZ","AR","CA","CO","CT","DE","DC","FL","GA","HI","ID","IL","IN","IA","KS","KY","LA","ME","MD","MA","MI","MN","MS","MO","MT","NE","NV","NH","NJ","NM","NY","NC","ND","OH","OK","OR","PA","RI","SC","SD","TN","TX","UT","VT","VA","WA","WV","WI","WY"};
                std::vector<std::string> names;
                int cur = 0;
                for (size_t i = 0; i < codes.size(); i++) { names.push_back(places::state_name(codes[i])); if (codes[i] == h.state) cur = (int)i; }
                int k = choose("State", names, cur);
                if (k >= 0) h.state = codes[k];
            };
            f.push_back(st);
            int filing = h.filing;
            Field fl = f_choice("Filing status", filing, {"Single", "Married filing jointly", "Head of household", "Married filing separately"},
                                "How you file your federal tax return.",
                                {"Not married, no one depending on you to keep up a home.",
                                 "Married couples filing one return together. Usually the lowest tax for couples.",
                                 "Not married and you pay more than half the cost of a home for a child or other dependent. Lower taxes than Single.",
                                 "Married but filing two returns. Usually costs more; mainly used in special situations."});
            auto inner = fl.edit;
            fl.edit = [inner, &h, this] { int v = h.filing; int c = choose("Filing status", {"Single", "Married filing jointly", "Head of household", "Married filing separately"}, v); if (c >= 0) h.filing = (Filing)c; (void)inner; };
            fl.step = [&h](int d) { h.filing = (Filing)(((int)h.filing + d + FILING_COUNT) % FILING_COUNT); };
            f.push_back(fl);
            f.push_back(f_int("Kids under 17", h.kids, 0, 20, "Each child under 17 can lower federal tax by up to $2,200 (the Child Tax Credit)."));
            f.push_back(f_int("Other dependents", h.other_deps, 0, 20, "Older kids, a parent you support, etc. Up to $500 each off federal tax."));
            f.push_back(f_num("Local income tax", h.local_tax, "%",
                              "Some cities and counties tax wages (often called an occupational license tax or earnings tax), for example 1% to 2.5%. Check your pay stub or your city's website. Leave 0 if there isn't one."));
            f.push_back(f_money("Savings now", h.savings, "Money you have set aside right now (emergency fund). Used for the emergency fund checkup on Home."));
            return f;
        },
        [this] {
            std::vector<std::pair<std::string, int>> p;
            const auto &pl = sum.place;
            if (pl.found) {
                p.push_back({pl.county + ", " + pl.state_name, -1});
                p.push_back({places::region_name(pl.region) + " region (for prices)", C_DIM});
            }
            const auto &dp = datapack::active();
            auto it = dp.states.find(b.home.state);
            if (it != dp.states.end()) {
                const auto &s = it->second;
                if (s.single.empty()) p.push_back({"No state income tax on wages.", C_GOOD});
                else if (s.single.size() == 1) p.push_back({"State income tax: flat " + util::percent(s.single[0].rate, 2), -1});
                else p.push_back({"State income tax: " + util::percent(s.single.front().rate, 1) + " to " + util::percent(s.single.back().rate, 1) + " in brackets", -1});
            }
            if (sum.tax.gross > 0) {
                p.push_back({"", -1});
                p.push_back({"Estimated take-home: " + util::money(sum.tax.net / 12, false) + "/month", C_GOOD});
            }
            return p;
        });
}

void App::edit_earner(size_t i) {
    if (i >= b.earners.size()) return;
    run_form("Income source",
        [this, i] {
            Earner &e = b.earners[i];
            std::vector<Field> f;
            f.push_back(f_text("Name", e.name, "A label, like 'My job' or 'Sam's job'."));
            int kind = e.kind;
            Field k = f_choice("Type", kind, {kind_name(JOB), kind_name(SELF_EMPLOYED), kind_name(UNTAXED)}, "",
                               {"A regular job: taxes come out of each paycheck (you get a W-2).",
                                "Freelance, gig, or business income (1099). You owe both halves of Social Security and Medicare and usually pay taxes yourself every quarter.",
                                "Money that isn't taxed: SSI, child support, most gifts, some benefits."});
            k.edit = [this, &e] { int c = choose("Type", {kind_name(JOB), kind_name(SELF_EMPLOYED), kind_name(UNTAXED)}, e.kind); if (c >= 0) e.kind = (IncomeKind)c; };
            k.step = [&e](int d) { e.kind = (IncomeKind)(((int)e.kind + d + KIND_COUNT) % KIND_COUNT); };
            f.push_back(k);
            Field basis{"I know my pay as", basis_name(e.basis), "Pick whichever number you know best. Tally works out the rest.", nullptr, nullptr};
            basis.edit = [this, &e] { int c = choose("I know my pay as", {basis_name(SALARY), basis_name(HOURLY), basis_name(PER_CHECK)}, e.basis); if (c >= 0) e.basis = (PayBasis)c; };
            basis.step = [&e](int d) { e.basis = (PayBasis)(((int)e.basis + d + BASIS_COUNT) % BASIS_COUNT); };
            f.push_back(basis);
            const char *amount_label = e.basis == SALARY ? "Yearly salary" : e.basis == HOURLY ? "Hourly wage" : "Gross per paycheck";
            const char *amount_hint = e.basis == SALARY ? "Before taxes, the number in your offer letter."
                                    : e.basis == HOURLY ? "What you earn per hour before taxes."
                                                        : "The biggest number on your pay stub (gross pay), before anything is taken out.";
            f.push_back(f_money(amount_label, e.amount, amount_hint));
            f.push_back(f_num("Hours per week", e.hours, " hrs",
                              "Hours you actually work in a typical week. Used for hourly pay and for your real hourly pay on the Wants tab."));
            f.push_back(f_freq("Paid how often", e.pay, "How often paychecks come. Every 2 weeks = 26 paychecks a year; twice a month = 24."));
            if (e.kind != UNTAXED) {
                f.push_back(f_num("401(k)/403(b), pre-tax", e.retire_pct, "% of pay",
                                  "Percent of pay going to a traditional retirement plan. It comes out before income tax, so it lowers your taxes now. If your job matches, try to get the full match."));
                f.back().edit = [this, &e] {
                    double v = e.retire_pct;
                    while (input_number("401(k)/403(b), pre-tax", v, "Percent of each paycheck, 0 to 100. Most people put in 3% to 15%.")) {
                        if (v <= 100) { e.retire_pct = v; break; }
                        input_error = "That's more than all of your pay. Type a percent from 0 to 100 (Esc to cancel).";
                    }
                };
                f.push_back(f_money("Health/dental/HSA per check", e.health,
                                    "Pre-tax insurance premiums and HSA/FSA per paycheck. Check your pay stub. These skip income tax and Social Security/Medicare."));
            }
            f.push_back(f_money("After-tax deductions per check", e.after_tax,
                                "Roth 401(k), union dues, garnishments, or other amounts taken after taxes. Usually $0."));
            {
                std::string month = budget::this_month();
                const Actual *a = e.actual.find(month);
                Field got{"Received in " + month_label(month), a ? util::money(a->amount) : "(not entered)",
                          "Pay that changes (overtime, tips, extra shifts)? Type the take-home that really came in this month. "
                          "Your estimate stays the same; Tally shows the difference.", nullptr, nullptr};
                got.edit = [this, i, month] {
                    double plan = i < sum.tax.earners.size() ? sum.tax.earners[i].net / 12 : 0;
                    enter_actual(b.earners[i].actual, month, b.earners[i].name, plan, true);
                };
                f.push_back(got);
                Field other{"Other months...", e.actual.list.empty() ? "" : std::to_string(e.actual.list.size()) + " entered",
                            "Enter or fix the take-home from an earlier month.", nullptr, nullptr};
                other.edit = [this, i] {
                    double plan = i < sum.tax.earners.size() ? sum.tax.earners[i].net / 12 : 0;
                    actual_other_month(b.earners[i].actual, b.earners[i].name, [plan](const std::string &) { return plan; }, true);
                };
                f.push_back(other);
            }
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            if (i >= sum.tax.earners.size() || i >= b.earners.size()) return p;
            const auto &t = sum.tax.earners[i];
            p.push_back({"Gross pay: " + util::money(t.gross, false) + " a year", -1});
            if (t.pretax > 0) p.push_back({"Pre-tax deductions: " + util::money(t.pretax, false), C_DIM});
            if (t.taxes > 0) p.push_back({"Estimated taxes: " + util::money(t.taxes, false) + " (" + util::percent(t.gross > 0 ? t.taxes / t.gross : 0, 0) + ")", C_DIM});
            p.push_back({"", -1});
            p.push_back({"Take-home: about " + util::money(t.per_check()) + " per paycheck", C_GOOD});
            p.push_back({"That's " + util::money(t.net / 12, false) + " a month, " + util::money(t.net, false) + " a year.", -1});
            if (b.earners[i].pay == BIWEEKLY)
                p.push_back({"Paid every 2 weeks: two months a year have 3 paychecks. Tally spreads them evenly across the year.", C_DIM});
            if (b.earners[i].hours > 0 && t.gross > 0)
                p.push_back({"Real hourly pay after taxes: " + util::money(t.net / (b.earners[i].hours * 52)), C_DIM});
            const Actuals &act = b.earners[i].actual;
            if (const Actual *a = act.find(budget::this_month())) {
                double diff = a->amount - t.net / 12;
                p.push_back({"", -1});
                p.push_back({"This month: received " + util::money(a->amount) + (std::fabs(diff) < 0.5 ? ", about as expected"
                             : ", " + util::money(std::fabs(diff)) + (diff > 0 ? " more than expected" : " less than expected")), diff < -0.5 ? C_OKAY : C_GOOD});
            }
            int n = 0;
            double avg = act.average(6, &n);
            if (n >= 2) p.push_back({"Your last " + std::to_string(n) + " months averaged " + util::money(avg) + " take-home.", C_DIM});
            return p;
        });
}

void App::edit_expense(size_t i) {
    if (i >= b.expenses.size()) return;
    run_form("Expense",
        [this, i] {
            Expense &e = b.expenses[i];
            std::vector<Field> f;
            f.push_back(f_text("Name", e.name, "What it is, like 'Rent' or 'Phone'."));
            f.push_back(f_money("Amount", e.amount, "How much each time you pay it. For bills that change, use a typical amount or average a few."));
            f.push_back(f_freq("How often", e.freq, "How often you pay this amount. Tally turns it into a monthly amount for you."));
            {
                std::string hint = e.freq == WEEKLY || e.freq == BIWEEKLY ? "Pick any day you pay it (a recent one is fine). Tally uses the weekday to know which months get an extra payment."
                                 : per_year(e.freq) < 12 ? "Pick the next date it's due. The month matters: Tally plans for it in the right months and reminds you to set money aside."
                                 : "Pick the next date it's due. For monthly bills only the day of the month matters.";
                f.push_back(f_date(e.freq == WEEKLY || e.freq == BIWEEKLY ? "A day you pay it" : "Next due date", e.due_date, hint, [this, i] {
                    int y, m, d;
                    Expense &x = b.expenses[i];
                    x.due_day = parse_date(x.due_date, y, m, d) ? d : 0;
                }));
            }
            {
                std::vector<std::string> names = {"Not on a card"};
                int cur = 0;
                for (size_t k = 0; k < b.cards.size(); k++) { names.push_back(b.cards[k].name); if (b.cards[k].name == e.card) cur = (int)k + 1; }
                Field cf{"Charged to", names[(size_t)cur], "If this bill or subscription is charged to a credit card, pick the card (nickname only). "
                         "When you enter the card payment, it covers this bill so it isn't counted twice.", nullptr, nullptr};
                cf.edit = [this, i, names, cur] {
                    std::vector<std::string> opts = names;
                    opts.push_back("+ Add a new card...");
                    int k = choose("Charged to", opts, cur);
                    if (k < 0) return;
                    if (k == (int)names.size()) {
                        std::string n;
                        if (!input("Card nickname", n, "Like 'Visa' or 'Grocery card'. Never the card number.") || util::trim(n).empty()) return;
                        n = util::trim(n);
                        bool have = false;
                        for (const auto &c : b.cards) if (c.name == n) have = true;
                        if (!have) { Card c; c.name = n; b.cards.push_back(c); }
                        b.expenses[i].card = n;
                        say("Added the card " + n + ". Pay it? Expenses > C, then M.", C_GOOD);
                        return;
                    }
                    b.expenses[i].card = k == 0 ? "" : names[(size_t)k];
                };
                cf.step = [this, i, names, cur](int d) { int n = (int)names.size(), k = ((cur + d) % n + n) % n; b.expenses[i].card = k == 0 ? "" : names[(size_t)k]; };
                f.push_back(cf);
            }
            {
                std::string month = budget::this_month();
                const Actual *a = e.actual.find(month);
                Field paid{"Paid in " + month_label(month), a ? util::money(a->amount) : "(not entered)",
                           "When the bill comes and it's different from the plan, type what you really paid. "
                           "The plan stays the same; Tally shows the difference.", nullptr, nullptr};
                paid.edit = [this, i, month] { Expense &x = b.expenses[i]; enter_actual(x.actual, month, x.name, x.planned_in_month(month), false); };
                f.push_back(paid);
                Field other{"Other months...", e.actual.list.empty() ? "" : std::to_string(e.actual.list.size()) + " entered",
                            "Enter or fix what you paid in an earlier month.", nullptr, nullptr};
                other.edit = [this, i] {
                    Expense &x = b.expenses[i];
                    actual_other_month(x.actual, x.name, [this, i](const std::string &m) { return b.expenses[i].planned_in_month(m); }, false);
                };
                f.push_back(other);
                int n = 0;
                double avg = e.actual.average(6, &n);
                if (n >= 2) {
                    // turn the average monthly total back into "amount each time"
                    double per = per_year(e.freq) >= 12 ? avg * 12.0 / per_year(e.freq) : avg;
                    Field use{"Use my average as the plan", util::money(per) + " (last " + std::to_string(n) + ")",
                              "For bills that go up and down: set the planned amount to what you've really paid on average.", nullptr, nullptr};
                    use.edit = [this, i, per] { b.expenses[i].amount = std::round(per * 100) / 100; say("The plan now uses your average.", C_GOOD); };
                    f.push_back(use);
                }
            }
            f.push_back(f_category(e.category));
            f.push_back(f_bool("Need or want", e.need, "Need", "Want", "Needs are what you must pay (housing, utilities, groceries, minimum payments). Wants could be cut if you had to."));
            f.push_back(f_text("Notes", e.notes, "Anything to remember: account, how it's paid, when the rate changes."));
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            const Expense &e = b.expenses[i];
            std::string month = budget::this_month();
            if (int li = budget::loan_twin(b, e); li >= 0) {
                p.push_back({"Counted twice? This looks like the payment for the loan '" + b.loans[(size_t)li].name +
                                 "', which already counts in your budget from the Loans tab. If it's the same, delete this one (D on the list).", C_WATCH});
                p.push_back({"", -1});
            }
            if (const Actual *a = e.actual.find(month)) {
                double diff = a->amount - e.planned_in_month(month);
                p.push_back({"This month: paid " + util::money(a->amount) + (std::fabs(diff) < 0.005 ? ", right on plan"
                             : ", " + util::money(std::fabs(diff)) + (diff > 0 ? " over the plan" : " under the plan")), diff > 0.005 ? C_OKAY : C_GOOD});
                p.push_back({"", -1});
            }
            p.push_back({util::money(e.amount) + " " + util::lower(freq_name(e.freq)) + " =", C_DIM});
            p.push_back({util::money(e.monthly()) + " a month" + (e.freq == WEEKLY || e.freq == BIWEEKLY ? " on average" : ""), C_GOOD});
            p.push_back({util::money(e.yearly(), false) + " a year", -1});
            if (e.freq == WEEKLY || e.freq == BIWEEKLY) {
                bool wk = e.freq == WEEKLY;
                int lo = wk ? 4 : 2;
                p.push_back({"", -1});
                p.push_back({std::string("Why not ") + util::money(e.amount * lo) + "? A year has " + (wk ? "52 weeks, not 48" : "26 two-week stretches, not 24") +
                                 ". Most months have " + std::to_string(lo) + " payments (" + util::money(e.amount * lo) + "); " + (wk ? "four" : "two") +
                                 " months a year have " + std::to_string(lo + 1) + " (" + util::money(e.amount * (lo + 1)) + "). " +
                                 util::money(e.monthly()) + " is the average that keeps the whole year covered.", -1});
                if (e.payments_in(month) >= 0) {
                    std::string line;
                    for (int k = 0; k < 4; k++) {
                        std::string mm = add_month(month, k);
                        int n = e.payments_in(mm);
                        line += (k ? ", " : "") + util::month_name(std::atoi(mm.c_str() + 5), true) + ": " + std::to_string(n) + " (" + util::money(e.amount * n, false) + ")";
                    }
                    p.push_back({"Payments by month: " + line, C_ACCENT});
                } else {
                    p.push_back({"Pick 'A day you pay it' and Tally shows which months get " + std::to_string(lo + 1) + ".", C_DIM});
                }
                p.push_back({"Billed a flat amount once a month instead? Set How often to Monthly.", C_DIM});
            } else if (per_year(e.freq) < 12) {
                util::Date t = util::today();
                long n = e.next_due(day_number(t.y, t.m, t.d));
                if (n >= 0) {
                    int y, m, d;
                    from_day_number(n, y, m, d);
                    long days = n - day_number(t.y, t.m, t.d);
                    p.push_back({"Next due " + util::date_text({y, m, d}) + (days == 0 ? " (today)" : " (in " + std::to_string(days) + (days == 1 ? " day)" : " days)")), C_ACCENT});
                } else {
                    p.push_back({"Pick 'Next due date' so Tally knows which month it lands in.", C_DIM});
                }
            }
            if (sum.income_month > 0) p.push_back({util::percent(e.monthly() / sum.income_month, 1) + " of your take-home pay", C_DIM});
            if (sum.real_hourly > 0) p.push_back({util::fixed(e.monthly() / sum.real_hourly, 1) + " hours of work a month", C_DIM});
            if (e.freq == YEARLY || e.freq == SEMIANNUAL || e.freq == QUARTERLY)
                p.push_back({"Set aside " + util::money(e.monthly()) + " each month so it's ready when it's due (a sinking fund).", C_ACCENT});
            // local context
            const auto &pl = sum.place;
            int region = sum.region;
            std::string n = util::lower(e.name);
            p.push_back({"", -1});
            if (e.category == "Housing" && pl.found) {
                int rent = pl.zip_m.rent ? pl.zip_m.rent : pl.county_m.rent;
                if (rent) p.push_back({"Median rent near you: " + util::money(rent, false) + "/month (Census, rent plus utilities).", -1});
            }
            if (n.find("electric") != std::string::npos) {
                if (const auto *s = budget::series("APU0" + std::to_string(region) + "0072610"))
                    p.push_back({"Typical electric bill in your region: about " + util::money(s->back().v * 863, false) + "/month (" +
                                     util::fixed(s->back().v * 100, 1) + " cents per kWh x 863 kWh).", -1});
            }
            if (e.category == "Gas & Fuel") {
                if (const auto *s = budget::series("APU0" + std::to_string(region) + "0074714"))
                    p.push_back({"Gas averages $" + util::fixed(s->back().v, 2) + "/gallon in your region. At 25 miles per gallon, that's about " +
                                     util::money(s->back().v / 25 * 100) + " per 100 miles.", -1});
            }
            const Category *c = b.category(e.category);
            double ch = 0;
            if (c && !c->cpi.empty() && budget::region_yoy(region, c->cpi, ch))
                p.push_back({"Prices for " + util::lower(cpi_name(c->cpi)) + " changed " + std::string(ch >= 0 ? "+" : "") + util::percent(ch, 1) +
                                 " in the last year: about " + util::money(e.monthly() * (1 + ch)) + "/month next year.", C_DIM});
            return p;
        });
}

void App::edit_loan(size_t i) {
    if (i >= b.loans.size()) return;
    run_form("Loan",
        [this, i] {
            Loan &l = b.loans[i];
            std::vector<Field> f;
            f.push_back(f_text("Name", l.name, "Like 'Car loan' or 'Visa'."));
            int kind = 0;
            const auto &kinds = loan_kinds();
            for (size_t k = 0; k < kinds.size(); k++) if (kinds[k] == l.kind) kind = (int)k;
            Field kf{"Kind", l.kind, "Mortgages count under Housing in your budget; other loans under Debt & Loans.", nullptr, nullptr};
            kf.edit = [this, &l, kinds, kind] { int c = choose("Kind", kinds, kind); if (c >= 0) l.kind = kinds[c]; };
            kf.step = [&l, kinds, kind](int d) { l.kind = kinds[(kind + d + (int)kinds.size()) % kinds.size()]; };
            f.push_back(kf);
            f.push_back(f_money("Balance owed now", l.balance, "What you still owe today (on your statement or the lender's website), not the original loan amount."));
            f.push_back(f_num("Interest rate (APR)", l.apr, "%", "The yearly interest rate. Credit cards are often 20-30%; car loans 5-12%; mortgages 5-8%."));
            f.push_back(f_money("Monthly payment", l.payment, "What you pay each month. Leave it at $0 and pick the last payment (or months left) to have Tally figure the payment."));
            f.push_back(f_date("Next payment due", l.next_due, "The date of your next payment, from your statement or the lender's website. "
                               "The payoff month counts from here."));
            if (l.payment <= 0) {
                f.push_back(f_int("Months left", l.months_left, 0, 600, "How many payments remain, counting the next one. Or pick the last payment date below.", "(not set)"));
                std::string last = l.months_left > 0 ? l.payment_month(l.months_left) : "(not set)";
                Field lp{"Last payment", last, "Pick the month of your final payment and Tally counts the payments for you (the next one counts as 1).", nullptr, nullptr};
                lp.edit = [this, i] {
                    Loan &x = b.loans[i];
                    int fy, fm;
                    x.first_payment(fy, fm);
                    std::string d;
                    if (x.months_left > 0) {   // start the calendar on the current last payment
                        int tot = fy * 12 + (fm - 1) + x.months_left - 1, y = tot / 12, m = tot % 12 + 1;
                        d = date_key(y, m, std::min(std::max(1, x.due_day()), util::days_in_month(y, m)));
                    }
                    if (!pick_date("Last payment", d, "The month of your final payment.", false)) return;
                    int ly, lm, ld;
                    parse_date(d, ly, lm, ld);
                    std::string first = x.payment_month(1);
                    int n = (ly * 12 + lm) - (fy * 12 + fm) + 1;
                    if (n < 1) { say("That's before your next payment (" + first + "). Pick a later month.", C_OKAY); return; }
                    x.months_left = n;
                    say(std::to_string(n) + (n == 1 ? " payment left: " : " payments left: ") + first + " through " + util::month_name(lm, true) + " " + std::to_string(ly) + ".", C_GOOD);
                };
                f.push_back(lp);
            }
            f.push_back(f_money("Extra each month", l.extra, "Try it: even $25 extra a month goes straight to the balance and can save a lot of interest."));
            f.push_back(f_money("One-time extra now", l.lump, "A what-if: a tax refund or bonus put toward this loan today."));
            f.push_back(f_bool("Count in my budget", l.in_budget, "Yes", "No", "Include the payment (and extra) in your monthly spending. "
                               "It shows on the Expenses list on its own, so don't add it there too."));
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            const Loan &l = b.loans[i];
            double pay = l.monthly_payment();
            if (pay <= 0 || l.balance <= 0) {
                p.push_back({"Enter the balance and either the monthly payment or months left.", C_OKAY});
                return p;
            }
            if (l.in_budget) p.push_back({"Counts in your budget: " + util::money(pay + l.extra) + "/month, listed on the Expenses tab under " +
                                          std::string(l.kind == "Mortgage" ? "Housing" : "Debt & Loans") + ".", C_GOOD});
            if (l.payment <= 0) p.push_back({"Payment to finish in " + std::to_string(l.months_left) + " payments (" + l.payment_month(1) + " through " +
                                             l.payment_month(l.months_left) + "): " + util::money(pay) + "/month", C_ACCENT});
            auto base = loans::simulate(l.balance, l.apr, pay);
            if (base.never) {
                p.push_back({"This payment doesn't cover the monthly interest (" + util::money(loans::interest_first_month(l.balance, l.apr)) + "). The balance would never go down.", C_WATCH});
                return p;
            }
            p.push_back({"Next payment " + l.payment_month(1) + (l.next_due.empty() ? " (assumed: set 'Next payment due')" : "") +
                         ", last payment " + l.payment_month(base.months) + " (" + std::to_string(base.months) + " payments)", -1});
            p.push_back({"Total interest: " + util::money(base.interest, false), -1});
            p.push_back({"Total paid: " + util::money(base.paid, false), C_DIM});
            if (l.extra > 0 || l.lump > 0) {
                auto w = loans::simulate(l.balance, l.apr, pay, l.extra, l.lump);
                p.push_back({"", -1});
                p.push_back({"With extra: last payment " + l.payment_month(w.months) + " (" + std::to_string(w.months) + " payments)", -1});
                p.push_back({"Saves " + util::money(base.interest - w.interest, false) + " in interest and " + std::to_string(base.months - w.months) + " months", C_GOOD});
            }
            return p;
        });
}

// After a loan is added or changed: an expense for the same payment would count it twice.
void App::check_loan_twins(size_t loan) {
    for (size_t k = 0; k < b.expenses.size(); k++) {
        if (budget::loan_twin(b, b.expenses[k]) != (int)loan) continue;
        const Loan &l = b.loans[loan];
        if (confirm("'" + b.expenses[k].name + "' on your Expenses list looks like the same payment as " + l.name +
                    ". Loans already count in your budget. Remove the expense so it isn't counted twice?", true)) {
            undo.push_back(b.expenses[k]);
            say("Removed " + b.expenses[k].name + " from Expenses (U on the Expenses tab brings it back). " + l.name + " counts once now.", C_GOOD);
            b.expenses.erase(b.expenses.begin() + (long)k);
            save();
        }
        return;
    }
}

void App::edit_want(size_t i) {
    if (i >= b.wants.size()) return;
    if (b.wants[i].trip) { edit_trip(i); return; }
    run_form("Want",
        [this, i] {
            Want &w = b.wants[i];
            return std::vector<Field>{f_text("What is it?", w.name, "Anything you're thinking about buying."),
                                      f_money("Price", w.price, "The full price, including tax and shipping if you know it.")};
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            auto m = budget::want_math(sum, b.wants[i].price);
            p.push_back({m.verdict, m.mood == 0 ? C_GOOD : m.mood == 1 ? C_OKAY : C_WATCH});
            return p;
        });
}

void App::edit_trip(size_t i) {
    if (i >= b.wants.size()) return;
    run_form("Trip or vacation",
        [this, i] {
            Want &w = b.wants[i];
            auto t = budget::trip_cost(w, sum.region);
            std::vector<Field> f;
            f.push_back(f_text("Where to?", w.name, "A name for the trip, like 'Beach week' or 'Grandma's for Thanksgiving'."));
            f.push_back({"GETTING THERE", "", "", nullptr, nullptr, true});
            f.push_back(f_num("Miles of driving", w.miles, " mi", "Round trip: there, back, and driving around once you're there. A map app shows the miles. 0 if you're not driving."));
            f.push_back(f_num("Car's miles per gallon", w.mpg, " mpg", "Your car's highway MPG (on the dashboard, or search the model). Most cars get 20-35."));
            Field gas = f_money("Gas price per gallon", w.gas_price, "Leave at $0 to use the average for your region from the built-in prices. Or type what you see at the pump.");
            if (w.gas_price <= 0) gas.value = t.gas_price > 0 ? "$" + util::fixed(t.gas_price, 2) + " (your region's average)" : "(not set)";
            f.push_back(gas);
            f.push_back(f_money("Car wear per mile", w.wear, "Oil, tires, and repairs that driving adds. About 10 cents a mile for most cars; 0 to leave it out."));
            f.push_back(f_money("Flights, tickets, parking", w.tickets, "Plane, train, or bus tickets, park passes, parking, tolls: everyone's total."));
            f.push_back({"STAYING", "", "", nullptr, nullptr, true});
            f.push_back(f_int("Nights", w.nights, 0, 365, "How many nights away. 0 for a day trip."));
            f.push_back(f_money("Lodging per night", w.lodging, "Hotel, rental, or campsite per night, with taxes and fees (they can add 15% or more). 0 if staying with family."));
            f.push_back({"EATING", "", "", nullptr, nullptr, true});
            f.push_back(f_int("People", w.people, 1, 50, "Everyone going (for meals)."));
            f.push_back(f_int("Meals out per day", w.meals, 0, 6, "Meals at restaurants each day. Packing breakfast and lunch is a big saver."));
            f.push_back(f_money("Cost per meal, each", w.meal_cost, "Per person, with tip. Fast food about $10-15; sit-down $20-35."));
            f.push_back({"EXTRAS", "", "", nullptr, nullptr, true});
            f.push_back(f_money("Activities and fun", w.fun, "Tours, admissions, souvenirs, and a cushion for surprises."));
            int y = 0, m = 0, d = 0;
            std::string when = w.save_by.size() == 7 ? w.save_by + "-01" : "";
            if (parse_date(when, y, m, d)) when = util::month_name(m) + " " + std::to_string(y);
            else when = "(not set)";
            Field by{"When is the trip?", when, "Pick the month you're going. Tally shows how much to set aside each month to pay cash.", nullptr, nullptr};
            by.edit = [this, i] {
                std::string d = b.wants[i].save_by.size() == 7 ? b.wants[i].save_by + "-01" : "";
                if (!pick_date("When is the trip?", d, "Any day in the month you're going.")) return;
                b.wants[i].save_by = d.empty() ? "" : d.substr(0, 7);
            };
            f.push_back(by);
            int months = budget::months_until(w.save_by);
            if (months > 0 && w.price > 0) {
                double per = std::ceil(w.price / months);
                std::string name = "Save for " + w.name;
                bool have = false;
                for (const auto &e : b.expenses) if (e.name == name) have = true;
                Field add{have ? "Update my savings bill" : "Add to my monthly budget", util::money(per, false) + "/month to Savings",
                          "Adds (or updates) a monthly Savings expense called '" + name + "', so the trip money is set aside "
                          "before it's spent. Delete it after the trip.", nullptr, nullptr};
                add.edit = [this, per, name] {
                    for (auto &e : b.expenses)
                        if (e.name == name) { e.amount = per; e.freq = MONTHLY; say("Updated " + name + ": " + util::money(per, false) + "/month.", C_GOOD); return; }
                    Expense e;
                    e.name = name;
                    e.amount = per;
                    e.freq = MONTHLY;
                    e.category = b.category("Savings") ? "Savings" : "Other";
                    e.need = false;
                    e.notes = "Trip savings, from the Wants tab.";
                    b.expenses.push_back(e);
                    say("Added " + name + " (" + util::money(per, false) + "/month) to your Expenses.", C_GOOD);
                };
                f.push_back(add);
            }
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> p;
            const Want &w = b.wants[i];
            auto t = budget::trip_cost(w, sum.region);
            auto line = [&](const std::string &what, double v, const std::string &how) {
                if (v > 0.004) p.push_back({what + ": " + util::money(v, false) + (how.empty() ? "" : "  (" + how + ")"), -1});
            };
            if (t.gallons > 0)
                line("Gas", t.gas, util::fixed(t.gallons, 1) + " gal x $" + util::fixed(t.gas_price, 2) + (t.local_gas ? ", region average" : ""));
            else if (w.miles > 0 && t.gas_price <= 0)
                p.push_back({"Type a gas price: no regional price is built in.", C_OKAY});
            line("Car wear", t.wear, util::fixed(w.miles, 0) + " mi x $" + util::fixed(w.wear, 2));
            line("Lodging", t.lodging, std::to_string(w.nights) + (w.nights == 1 ? " night" : " nights") + " x " + util::money(w.lodging, false));
            line("Food", t.food, std::to_string(t.days) + (t.days == 1 ? " day x " : " days x ") + std::to_string(w.meals) + " meals x " +
                                     std::to_string(w.people) + (w.people == 1 ? " person" : " people") + " x " + util::money(w.meal_cost, false));
            line("Tickets", t.tickets, "");
            line("Fun", t.fun, "");
            p.push_back({"Total: " + util::money(t.total, false), C_GOOD});
            if (t.total <= 0) { p.push_back({"Fill in the parts on the left to add up the trip.", C_OKAY}); return p; }
            p.push_back({"", -1});
            int months = budget::months_until(w.save_by);
            if (months > 0)
                p.push_back({"Set aside " + util::money(std::ceil(t.total / months), false) + " a month for " + std::to_string(months) +
                                 (months == 1 ? " month" : " months") + " and it's paid for in cash.", C_ACCENT});
            auto m = budget::want_math(sum, t.total);
            p.push_back({m.verdict, m.mood == 0 ? C_GOOD : m.mood == 1 ? C_OKAY : C_WATCH});
            if (w.people > 1) p.push_back({"Per person: " + util::money(t.total / w.people, false), C_DIM});
            return p;
        });
    recalc();
    save();
}

// --- Amounts that change month to month ----------------------------------------------

std::string App::month_label(const std::string &ym) {
    int m = ym.size() == 7 ? std::atoi(ym.c_str() + 5) : 0;
    return m >= 1 && m <= 12 ? util::month_name(m) + " " + ym.substr(0, 4) : ym;
}


// Asks what was really paid (or received) in one month. Blank clears it. Returns true if it changed.
bool App::enter_actual(Actuals &a, const std::string &month, const std::string &what, double planned, bool income) {
    const Actual *cur = a.find(month);
    double v = cur ? cur->amount : 0;
    std::string label = (income ? "Received in " : "Paid in ") + month_label(month) + " for " + what;
    std::string hint = income ? "The take-home that really landed in your account that month. The plan is about " + util::money(planned) +
                                    ". Leave blank to clear."
                              : "What you really paid that month. The plan is " + util::money(planned) +
                                    ". A credit or refund can be negative. Leave blank to clear.";
    if (!input_number(label, v, hint, true, !income)) return false;
    if (v == 0 && !cur) return false;
    if (v == 0) { a.clear(month); say("Cleared " + month_label(month) + ".", C_DIM); }
    else {
        a.set(month, v);
        double diff = v - planned;
        if (std::fabs(diff) < 0.005) say(month_label(month) + ": right on plan.", C_GOOD);
        else if (income) say(month_label(month) + ": " + util::money(std::fabs(diff)) + (diff > 0 ? " more than expected." : " less than expected."), diff > 0 ? C_GOOD : C_OKAY);
        else say(month_label(month) + ": " + util::money(std::fabs(diff)) + (diff > 0 ? " over the plan." : " under the plan."), diff > 0 ? C_OKAY : C_GOOD);
    }
    save();
    return true;
}

void App::actual_other_month(Actuals &a, const std::string &what, std::function<double(const std::string &)> planned, bool income) {
    std::string now = budget::this_month();
    std::vector<std::string> months, names;
    for (int i = 0; i < 13; i++) {
        std::string m = add_month(now, -i);
        const Actual *x = a.find(m);
        months.push_back(m);
        names.push_back(util::fit(month_label(m), 18) + (x ? util::money(x->amount) : "(not entered)"));
    }
    int k = choose(income ? "Which month's pay?" : "Which month's bill?", names, 0);
    if (k >= 0) enter_actual(a, months[(size_t)k], what, planned(months[(size_t)k]), income);
}

void App::edit_purchase(size_t i) {
    if (i >= b.purchases.size()) return;
    run_form("Something you bought",
        [this, i] {
            Purchase &p = b.purchases[i];
            std::vector<Field> f;
            f.push_back(f_text("What", p.name, "What you bought, like 'Groceries at Aldi' or 'Ground beef'."));
            f.push_back(f_money("Price", p.amount, "What you paid. A return or refund can be negative.", true));
            Field d{"Date", p.date, "When you bought it, as year-month-day: 2026-10-12.", nullptr, nullptr};
            d.edit = [this, &p] {
                std::string v = p.date;
                if (!input("Date", v, "Year-month-day, like " + server::today_iso() + ". Leave blank for today.")) return;
                v = util::trim(v);
                if (v.empty()) v = server::today_iso();
                if (server::valid_date(v)) p.date = v;
                else say("Type the date as year-month-day, like " + server::today_iso() + ".", C_WATCH);
            };
            f.push_back(d);
            f.push_back(f_category(p.category));
            f.push_back(f_text("Store", p.store, "Optional: where you bought it."));
            if (!p.id.empty()) f.push_back(Field{"From your server", "", "", nullptr, nullptr, true});
            return f;
        },
        [this, i] {
            std::vector<std::pair<std::string, int>> out;
            const Purchase &p = b.purchases[i];
            if (sum.real_hourly > 0 && p.amount > 0)
                out.push_back({"That's " + util::fixed(p.amount / sum.real_hourly, 1) + " hours of work at your real hourly pay.", C_DIM});
            double plan = 0, spent = 0;
            for (const auto &c : sum.cats) if (c.name == p.category) plan = c.monthly;
            for (const auto &q : b.purchases) if (q.category == p.category && q.month() == p.month()) spent += q.amount;
            if (plan > 0)
                out.push_back({p.category + " that month: " + util::money(spent, false) + " of " + util::money(plan, false) + " planned",
                               spent > plan ? C_WATCH : C_GOOD});
            return out;
        });
}

void App::handle_bought(const Event &e, int lower) {
    auto order = bought_order();
    handle_list_keys(e, (int)order.size());
    int cur = sel[tab] < (int)order.size() ? order[sel[tab]] : -1;
    int key = e.key;
    if (key == K_LEFT || key == K_RIGHT) {
        int y = std::atoi(bought_month.c_str()), m = std::atoi(bought_month.c_str() + 5) + (key == K_LEFT ? -1 : 1);
        if (m < 1) { m = 12; y--; }
        if (m > 12) { m = 1; y++; }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04d-%02d", y, m);
        bought_month = buf;
        sel[tab] = 0;
        scroll[tab] = 0;
    } else if (lower == 'a') {
        Purchase p;
        p.date = bought_month == budget::this_month() ? server::today_iso() : bought_month + "-01";
        p.name = "Groceries";
        b.purchases.push_back(p);
        edit_purchase(b.purchases.size() - 1);
        if (b.purchases.back().amount == 0) {   // left without a price: don't keep an empty row
            b.purchases.pop_back();
            save();
            say("Nothing added (no price).", C_DIM);
            return;
        }
        bought_month = b.purchases.back().month();
        order = bought_order();
        for (size_t j = 0; j < order.size(); j++) if (order[j] == (int)b.purchases.size() - 1) sel[tab] = (int)j;
    } else if (key == K_ENTER && cur >= 0) edit_purchase((size_t)cur);
    else if ((lower == 'd' || key == K_DELETE) && cur >= 0) {
        const Purchase &p = b.purchases[cur];
        std::string extra = p.id.empty() ? "" : " (It stays on your server; Tally won't bring it back.)";
        if (confirm("Delete " + p.name + " (" + util::money(p.amount) + ")?" + extra)) {
            b.purchases.erase(b.purchases.begin() + cur);
            save();
            say("Deleted.", C_DIM);
        }
    } else if (lower == 'g') {
        if (b.link.on) do_pull();
        else say("No server connected. Settings > Server link sets one up (it's optional).", C_DIM);
    }
}

void App::do_pull() {
    if (b.link.last_sync.empty()) {
        show_text("Before Tally connects", server::describe(b.link));
        if (!confirm("Connect to your server now?", true)) { say("Nothing was sent.", C_DIM); return; }
    }
    say("Asking your server for new purchases (gives up after a few seconds if it is off)...", C_DIM);
    paint();
    present();
    auto r = server::pull(b);
    save();
    say(r.message, r.ok ? (r.added ? C_GOOD : C_DIM) : C_WATCH);
}

void App::import_purchases() {
    std::string p;
    if (!input("Purchases file to import", p, "The full path to a .json file your server exported (for example on a USB stick).")) return;
    auto r = server::import_file(b, util::trim(p));
    save();
    say(r.message, r.ok ? C_GOOD : C_WATCH);
}

void App::edit_server() {
    run_form("Server link (optional)",
        [this] {
            ServerLink &l = b.link;
            std::vector<Field> f;
            f.push_back(f_bool("Server link", l.on, "On", "Off",
                               "Off: Tally never contacts a server. On: Tally checks your server only when you press G on "
                               "Prices > Bought, or choose 'Get new purchases now' here. Never by itself."));
            Field addr{"Server address", l.url.empty() ? "(not set)" : l.url,
                       "Shown on your server's Settings page, like http://192.168.1.20:8484. Plain http:// works only inside your home network; "
                       "anywhere else needs https://.", nullptr, nullptr};
            addr.edit = [this, &l] {
                std::string v = l.url;
                if (!input("Server address", v, "Like http://192.168.1.20:8484 (home network) or https://myserver.example.com")) return;
                v = util::trim(v);
                while (!v.empty() && v.back() == '/') v.pop_back();
                std::string problem = server::address_problem(v);
                if (!problem.empty()) { say(problem, C_WATCH); return; }
                if (v != l.url) { l.cursor.clear(); l.last_sync.clear(); }   // a different server starts from the beginning
                l.url = v;
            };
            f.push_back(addr);
            Field code{"Pairing code", l.code.empty() ? "(not set)" : l.code.substr(0, 4) + std::string(l.code.size() > 4 ? "..." : ""),
                       "From your server's Settings page. It's kept in your budget file and lets this copy of Tally read your purchases.",
                       nullptr, nullptr};
            code.edit = [this, &l] {
                std::string v;
                if (!input("Pairing code", v, "Letters, numbers, and dashes, exactly as the server shows it.")) return;
                v = util::trim(v);
                if (v.empty()) return;
                if (!server::code_ok(v)) { say("That doesn't look like a pairing code (letters, numbers, and dashes only).", C_WATCH); return; }
                l.code = v;
            };
            f.push_back(code);
            f.push_back(Field{"Last checked: ", l.last_sync.empty() ? "never" : l.last_sync, "", nullptr, nullptr, true});
            Field test{"Check the connection", "", "Asks the server 'are you there?' with your pairing code. Nothing else is sent.", nullptr, nullptr};
            test.edit = [this, &l] {
                say("Checking...", C_DIM);
                paint();
                present();
                auto r = server::check(l);
                say(r.message, r.ok ? C_GOOD : C_WATCH);
            };
            f.push_back(test);
            Field get{"Get new purchases now", "", "Downloads what you've added on the server since last time.", nullptr, nullptr};
            get.edit = [this] { do_pull(); };
            f.push_back(get);
            Field file{"Import purchases from a file...", "", "No network? Load the .json file your server exports, from a USB stick or a download.", nullptr, nullptr};
            file.edit = [this] { import_purchases(); };
            f.push_back(file);
            Field what{"What gets sent?", "", "Exactly what Tally sends and receives.", nullptr, nullptr};
            what.edit = [this, &l] { show_text("What the server link sends", server::describe(l)); };
            f.push_back(what);
            return f;
        },
        [] {
            std::vector<std::pair<std::string, int>> p;
            p.push_back({"Tally only downloads from your server.", C_GOOD});
            p.push_back({"It sends your pairing code and a bookmark number. Your budget is never uploaded.", C_DIM});
            p.push_back({"", -1});
            p.push_back({"Purchases show up on Prices: press B for 'Bought'.", -1});
            p.push_back({"Food totals (expired vs. saved by freezing) show as a checkup on Home.", -1});
            return p;
        });
}

void App::add_from_template() {
    const auto &ts = expense_templates();
    std::vector<std::string> names, hints;
    std::vector<int> colors;
    for (const auto &t : ts) {
        names.push_back(std::string(t.name) + "  (" + freq_name(t.freq) + ")");
        const Category *c = b.category(t.category);
        colors.push_back(c ? c->color : 245);
        hints.push_back(std::string(t.category) + ". " + t.hint);
    }
    int k = choose("Common bills", names, 0, colors, hints);
    if (k < 0) return;
    const Template &t = ts[k];
    Expense e;
    e.name = t.name;
    e.category = t.category;
    e.freq = t.freq;
    e.need = t.need;
    double v = 0;
    std::string hint = std::string("How much ") + util::lower(freq_name(t.freq)) + "? " + t.hint;
    if (std::string(t.category) == "Housing" && sum.place.found) {
        int rent = sum.place.zip_m.rent ? sum.place.zip_m.rent : sum.place.county_m.rent;
        if (rent && std::string(t.name) == "Rent") hint += " (Median near you: " + util::money(rent, false) + ")";
    }
    if (!input_number(t.name, v, hint)) return;
    e.amount = v;
    b.expenses.push_back(e);
    save();
    // select the new item
    auto order = expense_order();
    for (size_t j = 0; j < order.size(); j++) if (order[j] == (int)b.expenses.size() - 1) sel[T_EXPENSES] = (int)j;
    say("Added " + e.name + ": " + util::money(e.monthly()) + " a month. Press T to add another.", C_GOOD);
}

void App::edit_categories() {
    int s = 0;
    for (;;) {
        std::vector<std::string> names;
        std::vector<int> colors;
        std::vector<std::string> hints;
        for (const auto &c : b.categories) {
            names.push_back(c.name);
            colors.push_back(c.color);
            hints.push_back((c.cpi.empty() ? std::string("No inflation. ") : "Follows: " + cpi_name(c.cpi) + ". ") + c.help);
        }
        names.push_back("+ Add a category");
        colors.push_back(-1);
        hints.push_back("Make your own, like 'Kids' sports' or 'Hobby'.");
        int k = choose("Spending categories", names, s, colors, hints);
        if (k < 0) return;
        s = k;
        if (k == (int)b.categories.size()) {
            std::string name;
            if (!input("New category name", name, "") || util::trim(name).empty()) continue;
            b.categories.push_back({util::trim(name), PALETTE[b.categories.size() % (sizeof PALETTE / sizeof PALETTE[0])], "SA0", ""});
            save();
        }
        edit_category((size_t)k);
    }
}

void App::edit_category(size_t i) {
    if (i >= b.categories.size()) return;
    std::string old = b.categories[i].name;
    run_form("Category",
        [this, i] {
            Category &c = b.categories[i];
            std::vector<Field> f;
            f.push_back(f_text("Name", c.name, "Renaming moves every expense in this category with it."));
            Field col{"Color", "", "Pick a color that's easy to tell apart from the others. Charts always print the name too.", nullptr, nullptr};
            col.value = "color " + std::to_string(c.color);
            col.edit = [this, &c] {
                std::vector<std::string> names;
                std::vector<int> colors(std::begin(PALETTE), std::end(PALETTE));
                int cur = 0;
                for (size_t k = 0; k < colors.size(); k++) { names.push_back("Color " + std::to_string(colors[k])); if (colors[k] == c.color) cur = (int)k; }
                int k = choose("Color", names, cur, colors);
                if (k >= 0) c.color = colors[k];
            };
            col.step = [&c](int d) {
                int n = (int)(sizeof PALETTE / sizeof PALETTE[0]), cur = 0;
                for (int k = 0; k < n; k++) if (PALETTE[k] == c.color) cur = k;
                c.color = PALETTE[(cur + d + n) % n];
            };
            f.push_back(col);
            Field inf{"Inflation follows", cpi_name(c.cpi), "Which price index this spending tends to follow, for the price outlook. Debt payments and savings don't change with inflation.", nullptr, nullptr};
            inf.edit = [this, &c] {
                std::vector<std::string> names;
                int cur = 0;
                for (size_t k = 0; k < sizeof CPI_ITEMS / sizeof CPI_ITEMS[0]; k++) { names.push_back(CPI_ITEMS[k].name); if (c.cpi == CPI_ITEMS[k].code) cur = (int)k; }
                int k = choose("Inflation follows", names, cur);
                if (k >= 0) c.cpi = CPI_ITEMS[k].code;
            };
            f.push_back(inf);
            f.push_back(f_text("Description", c.help, "A short note on what belongs here (shown when picking a category)."));
            Field del{"Delete this category", "", "Its expenses move to Other.", nullptr, nullptr};
            del.edit = [this, i] {
                if (b.categories[i].name == "Other") { say("Other can't be deleted: it's where everything else goes.", C_OKAY); return; }
                if (!confirm("Delete the category " + b.categories[i].name + "?")) return;
                for (auto &e : b.expenses) if (e.category == b.categories[i].name) e.category = "Other";
                b.categories.erase(b.categories.begin() + (long)i);
                save();
                form_->sel = 1 << 20;  // close the form
            };
            f.push_back(del);
            return f;
        },
        nullptr);
    if (i < b.categories.size() && b.categories[i].name != old) {
        if (b.categories[i].name.empty()) b.categories[i].name = old;
        for (auto &e : b.expenses) if (e.category == old) e.category = b.categories[i].name;
        save();
    }
}

void App::do_update() {
    std::string url = b.settings.update_url.empty() ? DEFAULT_UPDATE_URL : b.settings.update_url;
    std::string about = "Update is optional. Tally works fine with the data built into it.\n\n"
                        "If you continue, Tally will download:\n\n" + update::describe_requests(url) +
                        "\n\nIt uses the curl program on your computer" + std::string(update::curl_available() ? "." : " (not found, so downloading won't work here).") +
                        "\n\nNo internet? Use 'Import a data pack file' with a file from another computer.";
    show_text("Update tax tables & prices", about);
    int k = choose("What would you like to download?", {"Prices and inflation (BLS)", "Tax tables (data pack)", "Both", "Nothing, go back"}, 3);
    if (k < 0 || k == 3) { say("Nothing was downloaded.", C_DIM); return; }
    std::string result;
    int color = C_GOOD;
    if (k == 0 || k == 2) {
        say("Downloading prices (up to a minute; it stops by itself if there is no internet)...", C_DIM);
        paint();
        present();
        auto r = update::update_prices();
        result += r.message;
        if (!r.ok) color = C_WATCH;
    }
    if (k == 1 || k == 2) {
        say("Downloading tax tables (up to a minute)...", C_DIM);
        paint();
        present();
        auto r = update::update_pack(url);
        result += (result.empty() ? "" : "  ") + r.message;
        if (!r.ok) color = C_WATCH;
    }
    recalc();
    say(result, color);
}

void App::do_program_update() {
    show_text("Update Tally", progupdate::describe());
    if (!confirm("Check GitHub for a newer Tally? (You have " TALLY_VERSION ".)", true)) return;
    say("Asking GitHub for the newest version...", C_DIM);
    paint();
    present();
    auto latest = progupdate::check();
    if (!latest.ok) { say(latest.message, C_WATCH); return; }
    if (!progupdate::newer(latest.version, TALLY_VERSION)) { say("You have the newest Tally (" TALLY_VERSION ").", C_GOOD); return; }
    if (!confirm("Tally " + latest.version + " is ready (you have " TALLY_VERSION "). Download and install it? Your budget isn't touched.", true)) return;
    say("Downloading Tally " + latest.version + " and checking it...", C_DIM);
    paint();
    present();
    auto r = progupdate::download_and_install(latest);
    if (r.kind == progupdate::Installed::FAILED) { say(r.message, C_WATCH); return; }
    if (r.kind == progupdate::Installed::NEEDS_ADMIN) {
        std::string cmd = "sudo install -m 755 '" + r.new_file + "' '" + r.target + "'";
        if (!util::have_program("sudo")) {
            show_text("One more step", r.message + " The new version is downloaded and checked. To finish, run this in a terminal "
                      "as an administrator, then reopen Tally:\n\n" + cmd);
            return;
        }
        if (!confirm(r.message + " Type it in the next screen to finish?", true)) {
            show_text("Finish later", "The new version is downloaded and checked. To finish, run this in a terminal, then reopen Tally:\n\n" + cmd);
            return;
        }
        save();
        stop();
        std::printf("\nInstalling Tally %s into %s\n(sudo will ask for your password)\n\n", latest.version.c_str(), r.target.c_str());
        std::fflush(stdout);
        bool ok = progupdate::install_with_sudo(r);
        if (!ok) {
            std::printf("\nThat didn't work, so nothing changed. Press Enter to go back to Tally.");
            std::fflush(stdout);
            int c;
            while ((c = std::getchar()) != '\n' && c != EOF) {}
            start();
            apply_display();
            invalidate();
            say("Tally wasn't updated (the password step didn't finish).", C_WATCH);
            return;
        }
        r.kind = progupdate::Installed::DONE;
        start();
        apply_display();
        invalidate();
    }
#ifdef _WIN32
    say("Updated to Tally " + latest.version + ". Close Tally and open it again to use the new version.", C_GOOD);
#else
    if (confirm("Updated to Tally " + latest.version + ". Restart Tally now to use it?", true)) {
        save();
        stop();
        std::string exe = r.target;
        execl(exe.c_str(), exe.c_str(), "--file", path.c_str(), (char *)nullptr);
        start();   // only reached if the restart failed
        apply_display();
        invalidate();
        say("Updated, but Tally couldn't restart itself. Close it and open it again.", C_OKAY);
    } else {
        say("Updated to Tally " + latest.version + ". The new version starts the next time you open Tally.", C_GOOD);
    }
#endif
}

void App::restore_backup() {
    std::string dir = util::join(fs::u8path(path).parent_path().u8string(), "backups");
    std::vector<std::string> files;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(fs::u8path(dir), ec)) files.push_back(e.path().filename().u8string());
    std::sort(files.rbegin(), files.rend());
    if (files.empty()) { say("No backups yet. Tally makes one each day you use it.", C_OKAY); return; }
    int k = choose("Restore which backup?", files, 0);
    if (k < 0) return;
    std::string text, err;
    Budget nb;
    if (!util::read_file(util::join(dir, files[k]), text) || !nb.parse(text, err)) { say("That backup couldn't be read.", C_WATCH); return; }
    if (!confirm("Replace your current budget with " + files[k] + "? (Today's version stays in backups.)")) return;
    backup();
    b = nb;
    save();
    apply_display();
    say("Restored " + files[k], C_GOOD);
}

void App::welcome() {
    show_text("Welcome to Tally",
              "Tally helps you plan where your money goes each month.\n\n"
              "- It estimates your take-home pay after taxes.\n"
              "- It turns every bill (weekly, monthly, yearly...) into one monthly picture.\n"
              "- It shows color charts, price trends for your area, loan payoff math, and friendly checkups.\n"
              "- It prints and exports to PDF and spreadsheets.\n\n"
              "Your privacy: everything stays on this computer in one plain text file. No account, no tracking, and it "
              "works with no internet at all.\n\n"
              "Getting around: number keys 1-0 switch tabs (or click them). Press ? on any screen for help. Arrow keys "
              "move, Enter changes things, Esc goes back.\n\n"
              "Press Enter to set up the basics. It takes about a minute.");
    std::string z;
    if (input("Your ZIP code", z, "Optional, but it sets your state taxes and local costs. Looked up on this computer only. Press Enter to skip.")) {
        b.home.zip = util::trim(z);
        auto pl = places::lookup(b.home.zip);
        if (pl.found) b.home.state = pl.state;
    }
    int f = choose("How do you file taxes?", {"Single", "Married filing jointly", "Head of household", "Married filing separately"}, 0, {},
                   {"Not married.", "Married, one return together.", "Not married, and you support a child or other dependent at home.", "Married, separate returns."});
    if (f >= 0) b.home.filing = (Filing)f;
    b.settings.welcomed = true;
    save();
    if (confirm("Add your job or other income now?", true)) {
        b.earners.push_back(Earner());
        edit_earner(b.earners.size() - 1);
        tab = T_HOME;
        say("Next: press 3 for Expenses, then T to add common bills.", C_ACCENT);
    } else {
        say("Whenever you're ready: press 2 for Income.", C_ACCENT);
    }
}

// =============================================================================
// Keys
// =============================================================================

void App::handle_list_keys(const Event &e, int count) {
    int &s = sel[tab];
    int page = std::max(1, bottom() - top() - 4);
    if (e.key == K_UP && s > 0) s--;
    else if (e.key == K_DOWN && s < count - 1) s++;
    else if (e.key == K_PGUP) s = std::max(0, s - page);
    else if (e.key == K_PGDN) s = std::max(0, std::min(count - 1, s + page));
    else if (e.key == K_HOME) s = 0;
    else if (e.key == K_END) s = std::max(0, count - 1);
    else if (e.key == K_MOUSE && e.button == 64 && s > 0) s--;
    else if (e.key == K_MOUSE && e.button == 65 && s < count - 1) s++;
}

void App::handle(const Event &e) {
    int key = e.key;
    int lower = (key < 128 && !e.ctrl) ? std::tolower(key) : key;
    if (key == K_RESIZE) { invalidate(); return; }
    if (key == K_MOUSE && e.button == 0) {
        for (const auto &h : std::vector<Hit>(hits))
            if (e.my == h.y && e.mx >= h.x && e.mx < h.x + h.w) { msg.clear(); h.fn(); return; }
        return;
    }
    if (e.ctrl && key == 'c') { quit = true; return; }
    if (lower == 'q') { quit = true; return; }
    if (key == K_ESC) {   // back out: a sub-view first, then to Home
        msg.clear();
        if (tab == T_EXPENSES && cards_view) { cards_view = false; sel[tab] = 0; scroll[tab] = 0; }
        else if (tab == T_PRICES && bought_view) { bought_view = false; sel[tab] = 0; scroll[tab] = 0; }
        else tab = T_HOME;
        return;
    }
    if (key == '?' || key == K_F1) { show_text(std::string(TAB_NAMES[tab]) + " - help", screen_help(tab)); return; }
    for (int i = 0; i < T_COUNT; i++)
        if (key == TAB_KEYS[i] && !(tab == T_PRINT && key >= '1' && key <= '8')) { tab = i; msg.clear(); return; }
    if (key == K_TAB) { tab = (tab + 1) % T_COUNT; msg.clear(); return; }
    if (key == K_BACKTAB) { tab = (tab + T_COUNT - 1) % T_COUNT; msg.clear(); return; }

    switch (tab) {
        case T_HOME:
            if (lower == 'p') { tab = T_PRINT; }
            else if (lower == 'c') {
                std::string body;
                for (const auto &t : sum.tips)
                    body += std::string(t.mood == budget::GOOD ? "[ok] " : t.mood == budget::OKAY ? "[--] " : "[!!] ") + t.title + "\n" + t.detail + "\n\n";
                show_text("Checkups", body.empty() ? "Add income and expenses to see checkups." : body);
            }
            break;
        case T_INCOME:
            handle_list_keys(e, (int)b.earners.size());
            if (lower == 'h') edit_household();
            else if (lower == 'a') {
                b.earners.push_back(Earner());
                if (b.earners.size() > 1) b.earners.back().name = "Job " + std::to_string(b.earners.size());
                sel[tab] = (int)b.earners.size() - 1;
                edit_earner(b.earners.size() - 1);
            } else if (key == K_ENTER) edit_earner((size_t)sel[tab]);
            else if (lower == 'm' && sel[tab] < (int)b.earners.size()) {
                size_t i = (size_t)sel[tab];
                double plan = i < sum.tax.earners.size() ? sum.tax.earners[i].net / 12 : 0;
                enter_actual(b.earners[i].actual, budget::this_month(), b.earners[i].name, plan, true);
            }
            else if ((lower == 'd' || key == K_DELETE) && sel[tab] < (int)b.earners.size()) {
                if (confirm("Delete " + b.earners[sel[tab]].name + "?")) { b.earners.erase(b.earners.begin() + sel[tab]); save(); say("Deleted.", C_DIM); }
            }
            break;
        case T_EXPENSES: {
            if (lower == 'c') {
                cards_view = !cards_view;
                sel[tab] = 0;
                scroll[tab] = 0;
                break;
            }
            if (cards_view) { handle_cards(e, lower); break; }
            auto order = expense_order();
            handle_list_keys(e, (int)order.size());
            bool has = sel[tab] < (int)order.size();
            int id = has ? order[(size_t)sel[tab]] : 0;
            int cur = has && id >= 0 ? id : -1;
            if (has && id < 0) {   // a loan row: it lives on the Loans tab
                size_t li = (size_t)(-id - 1);
                if (key == K_ENTER) { edit_loan(li); break; }
                if (lower == 'd' || key == K_DELETE || lower == 'm') {
                    say(b.loans[li].name + " comes from the Loans tab (press 4). Change or delete it there; Enter opens it here.", C_OKAY);
                    break;
                }
            }
            if (lower == 'a') {
                Expense ex;
                ex.name = "New expense";
                if (!exp_filter.empty()) ex.category = exp_filter;
                b.expenses.push_back(ex);
                edit_expense(b.expenses.size() - 1);
                order = expense_order();
                for (size_t j = 0; j < order.size(); j++) if (order[j] == (int)b.expenses.size() - 1) sel[tab] = (int)j;
            } else if (lower == 't') add_from_template();
            else if (lower == 'm' && cur >= 0) {
                Expense &x = b.expenses[cur];
                std::string month = budget::this_month();
                enter_actual(x.actual, month, x.name, x.planned_in_month(month), false);
            } else if (key == K_ENTER && cur >= 0) edit_expense((size_t)cur);
            else if ((lower == 'd' || key == K_DELETE) && cur >= 0) {
                if (confirm("Delete " + b.expenses[cur].name + "?")) {
                    undo.push_back(b.expenses[cur]);
                    b.expenses.erase(b.expenses.begin() + cur);
                    save();
                    say("Deleted. Press U to undo.", C_DIM);
                }
            } else if (lower == 'u') {
                if (undo.empty()) say("Nothing to undo.", C_DIM);
                else { b.expenses.push_back(undo.back()); undo.pop_back(); save(); say("Brought back " + b.expenses.back().name + ".", C_GOOD); }
            } else if (lower == 's') { exp_sort = (exp_sort + 1) % 4; scroll[tab] = 0; }
            else if (lower == 'f') {
                std::vector<std::string> names = {"Show everything"};
                std::vector<int> colors = {-1};
                for (const auto &c : b.categories) { names.push_back(c.name); colors.push_back(c.color); }
                int k = choose("Show only", names, 0, colors);
                if (k == 0) exp_filter.clear();
                else if (k > 0) exp_filter = names[k];
                sel[tab] = 0;
                scroll[tab] = 0;
            }
            break;
        }
        case T_LOANS: {
            handle_list_keys(e, (int)b.loans.size());
            bool has = sel[tab] < (int)b.loans.size();
            if (lower == 'a') {
                Loan l;
                l.name = "Loan " + std::to_string(b.loans.size() + 1);
                b.loans.push_back(l);
                sel[tab] = (int)b.loans.size() - 1;
                edit_loan(b.loans.size() - 1);
            } else if (key == K_ENTER && has) edit_loan((size_t)sel[tab]);
            if (lower == 'a' || (key == K_ENTER && has)) {
                say(b.loans[(size_t)sel[tab]].in_budget ? b.loans[(size_t)sel[tab]].name + " counts in your monthly budget and shows on the Expenses list." : "", C_GOOD);
                check_loan_twins((size_t)sel[tab]);
            }
            else if ((key == '+' || key == '=') && has) { b.loans[sel[tab]].extra += 25; save(); }
            else if ((key == '-' || key == '_') && has) { b.loans[sel[tab]].extra = std::max(0.0, b.loans[sel[tab]].extra - 25); save(); }
            else if (lower == 'l' && has) {
                input_number("One-time extra payment", b.loans[sel[tab]].lump, "What if you put a tax refund or bonus toward this loan today? 0 to clear.");
                save();
            } else if (lower == 'e' && has) {
                std::string err, out = report::loan_schedule_csv(b, (size_t)sel[tab], report::export_folder(b), err);
                if (out.empty()) say(err, C_WATCH);
                else { last_file = out; say("Saved the payment schedule: " + out, C_GOOD); }
            } else if ((lower == 'd' || key == K_DELETE) && has) {
                if (confirm("Delete " + b.loans[sel[tab]].name + "?")) { b.loans.erase(b.loans.begin() + sel[tab]); save(); say("Deleted.", C_DIM); }
            }
            break;
        }
        case T_WANTS: {
            handle_list_keys(e, (int)b.wants.size());
            bool has = sel[tab] < (int)b.wants.size();
            if (lower == 'a') {
                Want w;
                int kind = choose("Add a want", {"Something to buy", "A trip or vacation (gas, lodging, food...)"}, 0);
                if (kind < 0) return;
                if (kind == 1) {
                    if (!input("Where to?", w.name, "A name for the trip, like 'Beach week'.") || util::trim(w.name).empty()) return;
                    w.name = util::trim(w.name);
                    w.trip = true;
                    b.wants.push_back(w);
                    sel[tab] = (int)b.wants.size() - 1;
                    save();
                    edit_trip(b.wants.size() - 1);
                    return;
                }
                if (!input("What is it?", w.name, "Anything you're thinking about buying.") || util::trim(w.name).empty()) return;
                if (!input_number("Price of " + w.name, w.price, "The full price, including tax and shipping.")) return;
                b.wants.push_back(w);
                sel[tab] = (int)b.wants.size() - 1;
                save();
            } else if (key == K_ENTER && has) edit_want((size_t)sel[tab]);
            else if ((lower == 'd' || key == K_DELETE) && has) {
                if (confirm("Remove " + b.wants[sel[tab]].name + " from the list?")) { b.wants.erase(b.wants.begin() + sel[tab]); save(); }
            }
            break;
        }
        case T_AREA:
            if (lower == 'z') {
                std::string z = b.home.zip;
                if (input("ZIP code", z, "Looked up on this computer only.")) {
                    b.home.zip = util::trim(z);
                    auto pl = places::lookup(b.home.zip);
                    if (pl.found) b.home.state = pl.state;
                    save();
                }
            } else if (lower == 'n') national = !national;
            break;
        case T_PRICES:
            if (lower == 'b') {   // groceries bought (typed in or from your own server): trends only, never part of the budget
                bought_view = !bought_view;
                if (bought_view) bought_month = budget::this_month();
                sel[tab] = 0;
                scroll[tab] = 0;
            } else if (bought_view) handle_bought(e, lower);
            else if (lower == 'n') national = !national;
            break;
        case T_LEARN:
            if (key == K_UP && sel[tab] > 0) { sel[tab]--; learn_scroll = 0; }
            else if (key == K_DOWN && sel[tab] < (int)lessons().size() - 1) { sel[tab]++; learn_scroll = 0; }
            else if (key == K_PGDN || key == ' ' || (key == K_MOUSE && e.button == 65)) learn_scroll += key == K_MOUSE ? 3 : 10;
            else if (key == K_PGUP || (key == K_MOUSE && e.button == 64)) learn_scroll = std::max(0, learn_scroll - (key == K_MOUSE ? 3 : 10));
            break;
        case T_PRINT:
            handle_list_keys(e, 8);
            if (key >= '1' && key <= '8') { sel[tab] = key - '1'; do_export(key - '1'); }
            else if (key == K_ENTER) do_export(sel[tab]);
            break;
        case T_SETTINGS: {
            handle_list_keys(e, 12);
            int s = sel[tab];
            bool enter = key == K_ENTER, lr = key == K_LEFT || key == K_RIGHT;
            int d = key == K_LEFT ? -1 : 1;
            if (s == 0 && (enter || lr)) { b.settings.color_mode = (b.settings.color_mode + (enter ? 1 : d) + 3) % 3; apply_display(); save(); }
            else if (s == 1 && (enter || lr)) { b.settings.ascii = !b.settings.ascii; apply_display(); save(); }
            else if (s == 2 && (enter || lr)) { b.settings.tips = !b.settings.tips; save(); }
            else if (s == 3 && enter) edit_categories();
            else if (s == 4 && enter) do_program_update();
            else if (s == 5 && enter) do_update();
            else if (s == 6 && enter) {
                std::string p;
                if (input("Data pack file to import", p, "The full path to a tally-datapack.txt file (for example on a USB stick).")) {
                    auto r = update::import_pack(util::trim(p));
                    recalc();
                    say(r.message, r.ok ? C_GOOD : C_WATCH);
                }
            } else if (s == 7 && enter) {
                std::string p = util::join(report::export_folder(b), "tally-datapack.txt");
                if (input("Save the data pack as", p, "Another computer can load this with 'Import a data pack file'.")) {
                    util::make_dirs(fs::u8path(util::trim(p)).parent_path().u8string());
                    auto r = update::export_pack(util::trim(p));
                    say(r.message, r.ok ? C_GOOD : C_WATCH);
                }
            } else if (s == 8 && enter) restore_backup();
            else if (s == 9 && enter) {
                std::string u = b.settings.update_url;
                if (input("Update address", u, "Must start with https://. Leave blank for the default.")) {
                    b.settings.update_url = util::trim(u).empty() ? DEFAULT_UPDATE_URL : util::trim(u);
                    save();
                }
            } else if (s == 10 && enter) edit_server();
            else if (s == 11 && enter) {
                show_text("About Tally",
                          "Tally is a free, open-source budget planner (GPL-3.0). It runs in a terminal on Linux, macOS, and Windows "
                          "and needs nothing else installed.\n\n"
                          "Your data: one plain text file, " + path + ", readable in any text editor. Tally saves a dated backup "
                          "beside it each day you use it.\n\n"
                          "Privacy: Tally has no account, no analytics, and no ads. It never connects to the internet on its own. "
                          "Update Tally and Update tax tables & prices only run when you choose them, send the same fixed "
                          "requests every Tally user sends, and never include your ZIP code, income, expenses, or anything "
                          "else you typed. A new program is checked against its SHA-256 fingerprint before it's installed.\n\n"
                          "Server link (off unless you turn it on): Tally can download purchases from a server you run "
                          "yourself. It sends only your pairing code and a bookmark number, and never uploads your budget.\n\n"
                          "Built-in data:\n- ZIP codes and local medians: U.S. Census Bureau (2020 ZIP-county file; American "
                          "Community Survey 5-year estimates)\n- Federal taxes: IRS Revenue Procedure 2025-32 and Social Security "
                          "Administration\n- State taxes: Tax Foundation, State Individual Income Tax Rates and Brackets\n- Prices: "
                          "U.S. Bureau of Labor Statistics consumer price index and average prices\n\n"
                          "Tax figures are estimates for budgeting, not tax advice.");
            }
            break;
        }
    }
}

int App::run() {
    load();
    backup();
    recalc();
    if (!start()) return 1;
    apply_display();
    if (!b.settings.welcomed && b.earners.empty() && b.expenses.empty()) welcome();
    while (!quit) {
        recalc();
        paint();
        present();
        Event e = read(-1);
        if (e.key == K_NONE) continue;
        if (e.key == K_CLOSED) break;   // terminal closed: save (below) and leave
        if (e.key != K_MOUSE && e.key != K_RESIZE) msg.clear();
        handle(e);
    }
    save();
    stop();
    return 0;
}

}  // namespace

int run_app(const std::string &file) {
    App app(file);
    return app.run();
}
