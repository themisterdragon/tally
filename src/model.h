// Everything you type into Tally. Saved as a plain text file you can read in any editor.
#pragma once
#include <string>
#include <vector>

enum Freq { DAILY, WEEKLY, BIWEEKLY, SEMIMONTHLY, MONTHLY, BIMONTHLY, QUARTERLY, SEMIANNUAL, YEARLY, FREQ_COUNT };
double per_year(Freq f);
const char *freq_name(Freq f);   // "Every 2 weeks"
const char *freq_key(Freq f);    // "biweekly" (file format)
Freq freq_from_key(const std::string &k);

// Calendar helpers: "2026-10-09" or "10/9/2026" -> y, m, d. Day numbers count days since 1970-01-01.
bool parse_date(const std::string &s, int &y, int &m, int &d);
long day_number(int y, int m, int d);
void from_day_number(long n, int &y, int &m, int &d);
std::string date_key(int y, int m, int d);            // "2026-10-09"
std::string month_year_text(int y, int m, int add);   // month (y, m) plus 'add' months: "Mar 2029"

// What really happened in one month, for a bill or paycheck that changes ("2026-10" -> 132.40).
struct Actual {
    std::string month;
    double amount = 0;
};

// Shared by expenses and income: the actual amounts entered so far, newest last.
struct Actuals {
    std::vector<Actual> list;
    const Actual *find(const std::string &month) const;
    void set(const std::string &month, double amount);
    void clear(const std::string &month);
    double average(int last_n, int *count = nullptr) const;   // of the most recent entries
};

enum Filing { SINGLE, JOINT, HOH, SEPARATE, FILING_COUNT };
const char *filing_name(Filing f);
const char *filing_key(Filing f);

enum IncomeKind { JOB, SELF_EMPLOYED, UNTAXED, KIND_COUNT };
const char *kind_name(IncomeKind k);

enum PayBasis { SALARY, HOURLY, PER_CHECK, BASIS_COUNT };
const char *basis_name(PayBasis b);

struct Earner {
    std::string name = "My job";
    IncomeKind kind = JOB;
    PayBasis basis = SALARY;
    double amount = 0;        // yearly salary, hourly rate, or gross per paycheck
    double hours = 40;        // hours actually worked per week (also used for "real hourly pay")
    Freq pay = BIWEEKLY;      // how often paychecks come
    double retire_pct = 0;    // pre-tax 401(k)/403(b), percent of gross
    double health = 0;        // pre-tax health/dental/vision/HSA per paycheck
    double after_tax = 0;     // after-tax deductions per paycheck (Roth, union dues, garnishments)
    Actuals actual;           // take-home actually received, by month (pay that changes: overtime, tips, shifts)
    double gross_year() const;
    double checks_year() const { return per_year(pay); }
};

struct Category {
    std::string name;
    int color = 245;          // xterm-256 color
    std::string cpi;          // BLS CPI item used for inflation ("" = fixed cost, no inflation)
    std::string help;
};

struct Expense {
    std::string name;
    double amount = 0;
    Freq freq = MONTHLY;
    std::string category = "Other";
    bool need = true;         // need vs. want (for the 50/30/20 checkup)
    int due_day = 0;          // day of month it's due (0 = not set); kept in step with due_date
    std::string notes;
    std::string due_date;     // a day it's due ("2026-10-09"): sets the weekday for weekly bills, the month for yearly ones
    std::string card;         // name of the credit card it's charged to ("" = paid some other way)
    Actuals actual;           // what was really paid, by month (bills that change: electric, water, groceries)
    // The monthly AVERAGE: weekly bills x 52 / 12, because a year has 52 weeks, not 48.
    double monthly() const { return amount * per_year(freq) / 12.0; }
    // How many payments land in one month ("2026-10"). -1 = not known (weekly, yearly... with no due date).
    int payments_in(const std::string &month) const;
    // The next day it's due, on or after 'from' (day numbers). -1 = not known.
    long next_due(long from) const;
    // What the plan expects in that month: the real number of payments when known, else a
    // monthly-or-more-often bill's monthly average, or one payment for bills that come less often.
    double planned_in_month(const std::string &month) const {
        int n = payments_in(month);
        if (n >= 0) return amount * n;
        return per_year(freq) >= 12 ? monthly() : amount;
    }
    double yearly() const { return amount * per_year(freq); }
};

struct Loan {
    std::string name = "Loan";
    std::string kind = "Car";
    double balance = 0;
    double apr = 0;           // percent, e.g. 6.9
    int months_left = 0;      // used when payment is 0
    double payment = 0;       // monthly payment (0 = figure it from months_left)
    double extra = 0;         // extra paid every month
    double lump = 0;          // one-time extra payment now (what-if)
    bool in_budget = true;    // count the payment in the monthly budget
    std::string next_due;     // date of the next payment ("2026-10-20"); "" = assume next month
    int due_day() const;      // day of month from next_due (0 = not set)
    double monthly_payment() const;   // payment, or the one that finishes in months_left
    void first_payment(int &y, int &m) const;   // month of the next payment (not set: next month)
    // The month payment number k (1 = the next one) is made: "Dec 2026"
    std::string payment_month(int k) const;
};

// A credit card, by nickname only ("Visa", "Store card"). Never the card number: Tally doesn't need it.
// Bills can be marked as charged to it (Expense::card). When you enter what you paid on the card,
// that payment stands in for those bills, so nothing is counted twice.
struct Card {
    std::string name = "Credit card";
    std::string notes;
    Actuals paid;             // what you paid on the card, by month
};

struct Want {
    std::string name;
    double price = 0;         // for a trip: the total, worked out from the parts below
    // trips and vacations
    bool trip = false;
    double miles = 0;         // driving, round trip
    double mpg = 25;          // the car's miles per gallon
    double gas_price = 0;     // per gallon (0 = the average in your region)
    double wear = 0.10;       // oil, tires, and upkeep per mile
    int nights = 0;
    double lodging = 0;       // per night, with taxes and fees
    int people = 1;
    int meals = 2;            // meals eaten out each day
    double meal_cost = 15;    // per person, per meal
    double tickets = 0;       // flights, tickets, passes, parking
    double fun = 0;           // activities, souvenirs, anything else
    std::string save_by;      // "2027-06": the month of the trip
};

// Something you actually bought (typed in, or brought in from your own server).
// Expenses are the plan; purchases are what really happened.
struct Purchase {
    std::string id;           // the server's id ("" = typed into Tally)
    std::string date;         // YYYY-MM-DD
    std::string name;
    double amount = 0;
    std::string category = "Groceries";
    std::string store;
    std::string month() const { return date.size() >= 7 ? date.substr(0, 7) : ""; }   // "2026-10"
};

// The optional connection to a server you run yourself (off unless you turn it on).
// Tally only downloads from it; your budget is never uploaded.
struct ServerLink {
    bool on = false;
    std::string url;          // "http://192.168.1.20:8484" or "https://myserver.example.com"
    std::string code;         // pairing code from the server
    std::string cursor;       // the server's bookmark: "give me what's new since this"
    std::string last_sync;    // "2026-10-12 17:30"
    // food totals the server reports (expired vs. saved by freezing), for one month
    std::string food_month;   // "2026-10"
    double food_wasted = 0;
    double food_saved = 0;
};

struct Household {
    std::string zip;
    std::string state;        // two-letter code; set from ZIP, can be changed
    Filing filing = SINGLE;
    int kids = 0;             // children under 17
    int other_deps = 0;       // other dependents
    double local_tax = 0;     // city/county income tax, percent of wages
    double savings = 0;       // money set aside now (emergency fund checkup)
};

struct Settings {
    int color_mode = 0;       // 0 full color, 1 basic 16 colors, 2 no color
    bool ascii = false;       // plain ASCII drawing for very old terminals
    bool tips = true;         // show friendly checkups on Home
    std::string export_dir;   // "" = Documents/Tally
    std::string update_url;   // where Update fetches a newer data pack
    bool welcomed = false;
};

struct Budget {
    Household home;
    std::vector<Earner> earners;
    std::vector<Expense> expenses;
    std::vector<Loan> loans;
    std::vector<Card> cards;
    std::vector<Want> wants;
    std::vector<Purchase> purchases;
    std::vector<Category> categories;
    ServerLink link;
    Settings settings;

    void set_defaults();                       // categories etc. for a brand new file
    const Category *category(const std::string &name) const;
    std::string serialize() const;
    bool parse(const std::string &text, std::string &error);
};

std::vector<Category> default_categories();
extern const char *DEFAULT_UPDATE_URL;

struct Template { const char *name; const char *category; Freq freq; bool need; const char *hint; };
const std::vector<Template> &expense_templates();
const std::vector<std::string> &loan_kinds();
