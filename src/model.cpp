#include "model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "loans.h"
#include "util.h"

const char *DEFAULT_UPDATE_URL = "https://raw.githubusercontent.com/themisterdragon/tally/main/data/tally-datapack.txt";

double per_year(Freq f) {
    static const double n[] = {365, 52, 26, 24, 12, 6, 4, 2, 1};
    return f < FREQ_COUNT ? n[f] : 12;
}
const char *freq_name(Freq f) {
    static const char *n[] = {"Daily", "Weekly", "Every 2 weeks", "Twice a month", "Monthly",
                              "Every 2 months", "Every 3 months", "Twice a year", "Yearly"};
    return f < FREQ_COUNT ? n[f] : "Monthly";
}
const char *freq_key(Freq f) {
    static const char *n[] = {"daily", "weekly", "biweekly", "semimonthly", "monthly",
                              "bimonthly", "quarterly", "semiannual", "yearly"};
    return f < FREQ_COUNT ? n[f] : "monthly";
}
Freq freq_from_key(const std::string &k) {
    for (int i = 0; i < FREQ_COUNT; i++)
        if (k == freq_key((Freq)i)) return (Freq)i;
    return MONTHLY;
}

bool parse_date(const std::string &in, int &y, int &m, int &d) {
    std::string s = util::trim(in);
    y = m = d = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 1000) {
        y = m = d = 0;
        if (std::sscanf(s.c_str(), "%d/%d/%d", &m, &d, &y) != 3) return false;
        if (y < 100) y += 2000;
    }
    return y >= 1900 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= util::days_in_month(y, m);
}

long day_number(int y, int m, int d) {
    // days from civil (proleptic Gregorian), 1970-01-01 = 0
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static long floor_div(long a, long b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

void from_day_number(long z, int &y, int &m, int &d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

std::string date_key(int y, int m, int d) {
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
}

std::string month_year_text(int y, int m, int add) {
    int total = y * 12 + (m - 1) + add;
    return util::month_name(total % 12 + 1, true) + " " + std::to_string(total / 12);
}

// months between bills that come less often than monthly (0 = monthly or more often)
static int month_step(Freq f) {
    switch (f) {
        case BIMONTHLY: return 2;
        case QUARTERLY: return 3;
        case SEMIANNUAL: return 6;
        case YEARLY: return 12;
        default: return 0;
    }
}

int Expense::payments_in(const std::string &month) const {
    int y = month.size() == 7 ? std::atoi(month.c_str()) : 0, m = month.size() == 7 ? std::atoi(month.c_str() + 5) : 0;
    if (y < 1900 || m < 1 || m > 12) return -1;
    if (freq == DAILY) return util::days_in_month(y, m);
    if (freq == MONTHLY) return 1;
    if (freq == SEMIMONTHLY) return 2;
    int ay, am, ad;
    if (!parse_date(due_date, ay, am, ad)) return -1;
    if (int step = month_step(freq)) {
        int diff = (y * 12 + m) - (ay * 12 + am);
        return ((diff % step) + step) % step == 0 ? 1 : 0;
    }
    long step = freq == WEEKLY ? 7 : 14, a = day_number(ay, am, ad);
    long first = day_number(y, m, 1) - a, last = day_number(y, m, util::days_in_month(y, m)) - a;
    return (int)(floor_div(last, step) - (-floor_div(-first, step)) + 1);
}

long Expense::next_due(long from) const {
    int ay, am, ad;
    bool has = parse_date(due_date, ay, am, ad);
    if (freq == DAILY) return from;
    if (freq == WEEKLY || freq == BIWEEKLY) {
        if (!has) return -1;
        long step = freq == WEEKLY ? 7 : 14, a = day_number(ay, am, ad);
        return a + (-floor_div(-(from - a), step)) * step;
    }
    int day = has ? ad : due_day;
    if (day <= 0) return -1;
    int fy, fm, fd;
    from_day_number(from, fy, fm, fd);
    int step = month_step(freq);
    if (step && !has) return -1;
    for (int k = 0; k <= 24; k++) {
        int t = fy * 12 + (fm - 1) + k, y = t / 12, m = t % 12 + 1;
        if (step && ((((y * 12 + m) - (ay * 12 + am)) % step) + step) % step != 0) continue;
        int dim = util::days_in_month(y, m);
        int days[2] = {day, 0};
        if (freq == SEMIMONTHLY) {   // twice a month: the due day and about half a month away from it
            days[0] = std::min(day, day > 15 ? day - 15 : day);
            days[1] = day > 15 ? day : day + 15;
        }
        for (int dd : days) {
            if (dd <= 0) continue;
            long n = day_number(y, m, std::min(dd, dim));
            if (n >= from) return n;
        }
    }
    return -1;
}

int Loan::due_day() const {
    int y, m, d;
    return parse_date(next_due, y, m, d) ? d : 0;
}

void Loan::first_payment(int &y, int &m) const {
    util::Date t = util::today();
    int d;
    if (!parse_date(next_due, y, m, d)) {   // not set: the next payment is next month
        y = t.y + (t.m == 12);
        m = t.m % 12 + 1;
        return;
    }
    // a date that has gone by: the next payment is the next time that day comes around
    if (day_number(y, m, d) < day_number(t.y, t.m, t.d)) {
        y = t.y;
        m = t.m;
        if (std::min(d, util::days_in_month(t.y, t.m)) < t.d && ++m > 12) { m = 1; y++; }
    }
}

std::string Loan::payment_month(int k) const {
    int y, m;
    first_payment(y, m);
    return month_year_text(y, m, k - 1);
}

const char *filing_name(Filing f) {
    static const char *n[] = {"Single", "Married filing jointly", "Head of household", "Married filing separately"};
    return n[f < FILING_COUNT ? f : 0];
}
const char *filing_key(Filing f) {
    static const char *n[] = {"single", "joint", "hoh", "separate"};
    return n[f < FILING_COUNT ? f : 0];
}
const char *kind_name(IncomeKind k) {
    static const char *n[] = {"Job (taxes withheld)", "Self-employed / gig", "Untaxed (benefits, support)"};
    return n[k < KIND_COUNT ? k : 0];
}
const char *basis_name(PayBasis b) {
    static const char *n[] = {"Yearly salary", "Hourly wage", "Gross per paycheck"};
    return n[b < BASIS_COUNT ? b : 0];
}

double Earner::gross_year() const {
    switch (basis) {
        case SALARY: return amount;
        case HOURLY: return amount * hours * 52.0;
        case PER_CHECK: return amount * per_year(pay);
        default: return 0;
    }
}

double Loan::monthly_payment() const {
    if (payment > 0) return payment;
    if (months_left > 0) return loans::payment_for(balance, apr, months_left);
    return 0;
}

const Actual *Actuals::find(const std::string &month) const {
    for (const auto &a : list)
        if (a.month == month) return &a;
    return nullptr;
}

void Actuals::set(const std::string &month, double amount) {
    for (auto &a : list)
        if (a.month == month) { a.amount = amount; return; }
    list.push_back({month, amount});
    std::sort(list.begin(), list.end(), [](const Actual &x, const Actual &y) { return x.month < y.month; });
}

void Actuals::clear(const std::string &month) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Actual &a) { return a.month == month; }), list.end());
}

double Actuals::average(int last_n, int *count) const {
    int n = 0;
    double total = 0;
    for (auto it = list.rbegin(); it != list.rend() && n < last_n; ++it, ++n) total += it->amount;
    if (count) *count = n;
    return n ? total / n : 0;
}

static bool valid_month(const std::string &m) {
    return m.size() == 7 && m[4] == '-' && std::isdigit((unsigned char)m[0]) && std::isdigit((unsigned char)m[5]) && std::isdigit((unsigned char)m[6]);
}

std::vector<Category> default_categories() {
    // Colors are picked to stay readable on both dark and light terminals.
    // Every chart prints the category name beside its color, so color is never the only clue.
    return {
        {"Housing", 33, "SAH1", "Rent or mortgage, property tax, HOA, renter's or home insurance."},
        {"Utilities", 214, "SAH2", "Electric, gas/heating, water, sewer, trash."},
        {"Groceries", 70, "SAF11", "Food and household basics from the store."},
        {"Dining Out", 209, "SEFV", "Restaurants, takeout, coffee runs, delivery."},
        {"Transportation", 75, "SAT", "Car maintenance, registration, parking, bus fare, rideshare."},
        {"Gas & Fuel", 172, "SETB01", "Gas for the car (heating fuel goes in Utilities)."},
        {"Insurance", 141, "SA0", "Car, life, or disability insurance you pay yourself."},
        {"Health", 167, "SAM", "Copays, prescriptions, glasses, insurance you pay outside work."},
        {"Debt & Loans", 160, "", "Loan and credit card payments. These don't rise with inflation."},
        {"Kids & School", 179, "SAE", "Childcare, tuition, school supplies, activities."},
        {"Phone & Internet", 37, "SAE", "Cell phone, home internet."},
        {"Subscriptions", 99, "SAR", "Streaming, apps, memberships, gym."},
        {"Fun", 205, "SAR", "Hobbies, outings, games, travel."},
        {"Clothing", 137, "SAA", "Clothes and shoes."},
        {"Personal Care", 176, "SA0", "Haircuts, toiletries, cosmetics."},
        {"Pets", 108, "SAR", "Pet food, vet, supplies."},
        {"Giving", 213, "", "Donations, church, gifts for others."},
        {"Savings", 35, "", "Emergency fund, retirement you add yourself, saving for goals."},
        {"Other", 245, "SA0", "Anything that doesn't fit elsewhere."},
    };
}

const std::vector<Template> &expense_templates() {
    static const std::vector<Template> t = {
        {"Rent", "Housing", MONTHLY, true, "Your monthly rent."},
        {"Mortgage", "Housing", MONTHLY, true, "Monthly mortgage payment (with escrow if it's included)."},
        {"Renter's insurance", "Housing", MONTHLY, true, "Often $10-$25 a month."},
        {"Property tax", "Housing", YEARLY, true, "Skip if your mortgage escrow already pays it."},
        {"Electric", "Utilities", MONTHLY, true, "Use a typical month, or a yearly total / 12."},
        {"Water & sewer", "Utilities", MONTHLY, true, ""},
        {"Natural gas / heating", "Utilities", MONTHLY, true, ""},
        {"Trash", "Utilities", MONTHLY, true, ""},
        {"Groceries", "Groceries", WEEKLY, true, "What you spend at the store in a normal week."},
        {"Eating out", "Dining Out", WEEKLY, false, "Takeout, restaurants, coffee."},
        {"Gas for the car", "Gas & Fuel", WEEKLY, true, ""},
        {"Car insurance", "Insurance", MONTHLY, true, "Paid every 6 months? Pick 'Twice a year'."},
        {"Car maintenance", "Transportation", YEARLY, true, "Oil changes, tires, repairs: a yearly guess."},
        {"Car registration", "Transportation", YEARLY, true, ""},
        {"Cell phone", "Phone & Internet", MONTHLY, true, ""},
        {"Internet", "Phone & Internet", MONTHLY, true, ""},
        {"Streaming", "Subscriptions", MONTHLY, false, ""},
        {"Gym", "Subscriptions", MONTHLY, false, ""},
        {"Prescriptions", "Health", MONTHLY, true, ""},
        {"Childcare", "Kids & School", WEEKLY, true, ""},
        {"Haircuts", "Personal Care", MONTHLY, false, ""},
        {"Clothing", "Clothing", MONTHLY, false, "A monthly average."},
        {"Pet food & vet", "Pets", MONTHLY, true, ""},
        {"Gifts & holidays", "Giving", YEARLY, false, "Birthdays and holidays for the whole year."},
        {"Emergency fund", "Savings", MONTHLY, true, "Pay yourself first, even $25 helps."},
        {"Credit card payment", "Debt & Loans", MONTHLY, true, "Tip: add it on the Loans tab instead to see payoff math."},
    };
    return t;
}

const std::vector<std::string> &loan_kinds() {
    static const std::vector<std::string> k = {"Car", "Student", "Mortgage", "Credit card", "Personal", "Medical", "Other"};
    return k;
}

void Budget::set_defaults() {
    categories = default_categories();
    settings.update_url = DEFAULT_UPDATE_URL;
}

const Category *Budget::category(const std::string &name) const {
    for (const auto &c : categories)
        if (c.name == name) return &c;
    for (const auto &c : categories)
        if (c.name == "Other") return &c;
    return categories.empty() ? nullptr : &categories.back();
}

// --- file format -------------------------------------------------------------
// [section] headers, then "key = value" lines. Repeated sections ([expense]) are list items.

static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') {}
        else o += c;
    }
    return o;
}

static std::string unesc(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++;
            o += s[i] == 'n' ? '\n' : s[i];
        } else {
            o += s[i];
        }
    }
    return o;
}

static std::string num(double v) {
    std::string s = util::fixed(v, 4);
    while (s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) {
        bool dot = s.back() == '.';
        s.pop_back();
        if (dot) break;
    }
    return s;
}

std::string Budget::serialize() const {
    std::string o;
    o += "# Tally budget file. Plain text on purpose: you can read or fix it in any editor.\n";
    o += "# Saved " + util::stamp_now() + ". Your budget in this file is never sent anywhere.\n";
    if (link.on) o += "# The [server] pairing code below lets Tally download purchases from your own server. Keep it private.\n";
    o += "tally = 1\n\n[household]\n";
    o += "zip = " + esc(home.zip) + "\nstate = " + esc(home.state) + "\nfiling = " + filing_key(home.filing) + "\n";
    o += "kids_under_17 = " + std::to_string(home.kids) + "\nother_dependents = " + std::to_string(home.other_deps) + "\n";
    o += "local_tax_percent = " + num(home.local_tax) + "\nsavings_now = " + num(home.savings) + "\n";
    o += "\n[settings]\ncolors = " + std::to_string(settings.color_mode) + "\nascii = " + (settings.ascii ? "yes" : "no") + "\n";
    o += "tips = " + std::string(settings.tips ? "yes" : "no") + "\nexport_folder = " + esc(settings.export_dir) + "\n";
    o += "update_url = " + esc(settings.update_url) + "\nwelcomed = " + (settings.welcomed ? "yes" : "no") + "\n";
    if (link.on || !link.url.empty()) {
        o += "\n[server]\non = " + std::string(link.on ? "yes" : "no") + "\naddress = " + esc(link.url) + "\npairing_code = " + esc(link.code) + "\n";
        o += "cursor = " + esc(link.cursor) + "\nlast_sync = " + esc(link.last_sync) + "\n";
        if (!link.food_month.empty())
            o += "food_month = " + esc(link.food_month) + "\nfood_wasted = " + num(link.food_wasted) + "\nfood_saved = " + num(link.food_saved) + "\n";
    }
    for (const auto &c : categories) {
        o += "\n[category]\nname = " + esc(c.name) + "\ncolor = " + std::to_string(c.color) + "\ninflation = " + esc(c.cpi) + "\n";
        o += "help = " + esc(c.help) + "\n";
    }
    for (const auto &e : earners) {
        o += "\n[income]\nname = " + esc(e.name) + "\nkind = " + std::to_string(e.kind) + "\nbasis = " + std::to_string(e.basis) + "\n";
        o += "amount = " + num(e.amount) + "\nhours = " + num(e.hours) + "\npaid = " + freq_key(e.pay) + "\n";
        o += "retirement_percent = " + num(e.retire_pct) + "\nhealth_per_check = " + num(e.health) + "\n";
        o += "after_tax_per_check = " + num(e.after_tax) + "\n";
        for (const auto &a : e.actual.list) o += "actual = " + a.month + " " + num(a.amount) + "\n";
    }
    for (const auto &e : expenses) {
        o += "\n[expense]\nname = " + esc(e.name) + "\namount = " + num(e.amount) + "\nevery = " + freq_key(e.freq) + "\n";
        o += "category = " + esc(e.category) + "\nneed = " + (e.need ? "yes" : "no") + "\ndue_day = " + std::to_string(e.due_day) + "\n";
        if (!e.notes.empty()) o += "notes = " + esc(e.notes) + "\n";
        if (!e.due_date.empty()) o += "due_date = " + esc(e.due_date) + "\n";
        if (!e.card.empty()) o += "card = " + esc(e.card) + "\n";
        for (const auto &a : e.actual.list) o += "actual = " + a.month + " " + num(a.amount) + "\n";
    }
    for (const auto &l : loans) {
        o += "\n[loan]\nname = " + esc(l.name) + "\nkind = " + esc(l.kind) + "\nbalance = " + num(l.balance) + "\n";
        o += "apr_percent = " + num(l.apr) + "\nmonths_left = " + std::to_string(l.months_left) + "\n";
        o += "payment = " + num(l.payment) + "\nextra_monthly = " + num(l.extra) + "\none_time_extra = " + num(l.lump) + "\n";
        o += "in_budget = " + std::string(l.in_budget ? "yes" : "no") + "\n";
        if (!l.next_due.empty()) o += "next_payment = " + esc(l.next_due) + "\n";
    }
    for (const auto &c : cards) {
        o += "\n[card]\nname = " + esc(c.name) + "\n";
        if (!c.notes.empty()) o += "notes = " + esc(c.notes) + "\n";
        for (const auto &a : c.paid.list) o += "paid = " + a.month + " " + num(a.amount) + "\n";
    }
    for (const auto &w : wants) {
        o += "\n[want]\nname = " + esc(w.name) + "\nprice = " + num(w.price) + "\n";
        if (!w.trip) continue;
        o += "trip = yes\nmiles_round_trip = " + num(w.miles) + "\nmpg = " + num(w.mpg) + "\ngas_price = " + num(w.gas_price) + "\n";
        o += "wear_per_mile = " + num(w.wear) + "\nnights = " + std::to_string(w.nights) + "\nlodging_per_night = " + num(w.lodging) + "\n";
        o += "people = " + std::to_string(w.people) + "\nmeals_out_per_day = " + std::to_string(w.meals) + "\nmeal_cost = " + num(w.meal_cost) + "\n";
        o += "tickets = " + num(w.tickets) + "\nfun = " + num(w.fun) + "\nsave_by = " + esc(w.save_by) + "\n";
    }
    for (const auto &p : purchases) {
        o += "\n[purchase]\ndate = " + esc(p.date) + "\nname = " + esc(p.name) + "\namount = " + num(p.amount) + "\ncategory = " + esc(p.category) + "\n";
        if (!p.store.empty()) o += "store = " + esc(p.store) + "\n";
        if (!p.id.empty()) o += "server_id = " + esc(p.id) + "\n";
    }
    return o;
}

bool Budget::parse(const std::string &text, std::string &error) {
    *this = Budget();
    settings.update_url = DEFAULT_UPDATE_URL;
    std::string section;
    bool saw_category = false;
    int lineno = 0;
    auto d = [](const std::string &v) { double x = 0; util::parse_number(v, x); return x; };
    auto yes = [](const std::string &v) { return v == "yes" || v == "true" || v == "1"; };
    // "actual = 2026-10 132.40"
    auto actual = [&](Actuals &into, const std::string &v) {
        size_t sp = v.find(' ');
        double amt = 0;
        if (sp != std::string::npos && valid_month(v.substr(0, sp)) && util::parse_number(util::trim(v.substr(sp + 1)), amt))
            into.set(v.substr(0, sp), amt);
    };
    for (const auto &raw : util::split(text, '\n')) {
        lineno++;
        std::string line = util::trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section == "category") { categories.emplace_back(); saw_category = true; }
            else if (section == "income") earners.emplace_back();
            else if (section == "expense") expenses.emplace_back();
            else if (section == "loan") loans.emplace_back();
            else if (section == "want") wants.emplace_back();
            else if (section == "card") cards.emplace_back();
            else if (section == "purchase") purchases.emplace_back();
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineno) + " has no '=': " + line;
            return false;
        }
        std::string k = util::trim(line.substr(0, eq)), v = unesc(util::trim(line.substr(eq + 1)));
        if (section == "household") {
            if (k == "zip") home.zip = v;
            else if (k == "state") home.state = v;
            else if (k == "filing") { for (int i = 0; i < FILING_COUNT; i++) if (v == filing_key((Filing)i)) home.filing = (Filing)i; }
            else if (k == "kids_under_17") home.kids = (int)d(v);
            else if (k == "other_dependents") home.other_deps = (int)d(v);
            else if (k == "local_tax_percent") home.local_tax = d(v);
            else if (k == "savings_now") home.savings = d(v);
        } else if (section == "settings") {
            if (k == "colors") settings.color_mode = (int)d(v);
            else if (k == "ascii") settings.ascii = yes(v);
            else if (k == "tips") settings.tips = yes(v);
            else if (k == "export_folder") settings.export_dir = v;
            else if (k == "update_url") settings.update_url = v;
            else if (k == "welcomed") settings.welcomed = yes(v);
        } else if (section == "category" && !categories.empty()) {
            auto &c = categories.back();
            if (k == "name") c.name = v;
            else if (k == "color") c.color = (int)d(v);
            else if (k == "inflation") c.cpi = v;
            else if (k == "help") c.help = v;
        } else if (section == "income" && !earners.empty()) {
            auto &e = earners.back();
            if (k == "name") e.name = v;
            else if (k == "kind") e.kind = (IncomeKind)std::min(2, std::max(0, (int)d(v)));
            else if (k == "basis") e.basis = (PayBasis)std::min(2, std::max(0, (int)d(v)));
            else if (k == "amount") e.amount = d(v);
            else if (k == "hours") e.hours = d(v);
            else if (k == "paid") e.pay = freq_from_key(v);
            else if (k == "retirement_percent") e.retire_pct = d(v);
            else if (k == "health_per_check") e.health = d(v);
            else if (k == "after_tax_per_check") e.after_tax = d(v);
            else if (k == "actual") actual(e.actual, v);
        } else if (section == "expense" && !expenses.empty()) {
            auto &e = expenses.back();
            if (k == "name") e.name = v;
            else if (k == "amount") e.amount = d(v);
            else if (k == "every") e.freq = freq_from_key(v);
            else if (k == "category") e.category = v;
            else if (k == "need") e.need = yes(v);
            else if (k == "due_day") e.due_day = (int)d(v);
            else if (k == "notes") e.notes = v;
            else if (k == "due_date") e.due_date = v;
            else if (k == "card") e.card = v;
            else if (k == "actual") actual(e.actual, v);
        } else if (section == "card" && !cards.empty()) {
            auto &c = cards.back();
            if (k == "name") c.name = v;
            else if (k == "notes") c.notes = v;
            else if (k == "paid") actual(c.paid, v);
        } else if (section == "loan" && !loans.empty()) {
            auto &l = loans.back();
            if (k == "name") l.name = v;
            else if (k == "kind") l.kind = v;
            else if (k == "balance") l.balance = d(v);
            else if (k == "apr_percent") l.apr = d(v);
            else if (k == "months_left") l.months_left = (int)d(v);
            else if (k == "payment") l.payment = d(v);
            else if (k == "extra_monthly") l.extra = d(v);
            else if (k == "one_time_extra") l.lump = d(v);
            else if (k == "in_budget") l.in_budget = yes(v);
            else if (k == "next_payment") l.next_due = v;
        } else if (section == "server") {
            if (k == "on") link.on = yes(v);
            else if (k == "address") link.url = v;
            else if (k == "pairing_code") link.code = v;
            else if (k == "cursor") link.cursor = v;
            else if (k == "last_sync") link.last_sync = v;
            else if (k == "food_month") link.food_month = v;
            else if (k == "food_wasted") link.food_wasted = d(v);
            else if (k == "food_saved") link.food_saved = d(v);
        } else if (section == "purchase" && !purchases.empty()) {
            auto &p = purchases.back();
            if (k == "date") p.date = v;
            else if (k == "name") p.name = v;
            else if (k == "amount") p.amount = d(v);
            else if (k == "category") p.category = v;
            else if (k == "store") p.store = v;
            else if (k == "server_id") p.id = v;
        } else if (section == "want" && !wants.empty()) {
            auto &w = wants.back();
            if (k == "name") w.name = v;
            else if (k == "price") w.price = d(v);
            else if (k == "trip") w.trip = yes(v);
            else if (k == "miles_round_trip") w.miles = d(v);
            else if (k == "mpg") w.mpg = d(v);
            else if (k == "gas_price") w.gas_price = d(v);
            else if (k == "wear_per_mile") w.wear = d(v);
            else if (k == "nights") w.nights = (int)d(v);
            else if (k == "lodging_per_night") w.lodging = d(v);
            else if (k == "people") w.people = std::max(1, (int)d(v));
            else if (k == "meals_out_per_day") w.meals = (int)d(v);
            else if (k == "meal_cost") w.meal_cost = d(v);
            else if (k == "tickets") w.tickets = d(v);
            else if (k == "fun") w.fun = d(v);
            else if (k == "save_by") w.save_by = v;
        }
    }
    if (!saw_category) categories = default_categories();
    if (settings.update_url.empty()) settings.update_url = DEFAULT_UPDATE_URL;
    return true;
}
