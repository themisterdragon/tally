// Puts it all together: monthly totals, category breakdown, inflation outlook, checkups.
#pragma once
#include <string>
#include <vector>

#include "datapack.h"
#include "model.h"
#include "places.h"
#include "tax.h"

namespace budget {

struct CatTotal {
    std::string name;
    int color = 245;
    double monthly = 0;
    double yoy = 0;            // price change over the last 12 months for this kind of spending
    bool has_yoy = false;
};

enum Mood { GOOD, OKAY, WATCH };
struct Tip { Mood mood; std::string title, detail; };

struct Summary {
    places::Place place;
    int region = 0;                // BLS region for prices (0 = U.S. average)
    tax::Result tax;
    double income_month = 0;       // take-home, monthly
    double gross_month = 0;
    double spend_month = 0;        // expenses + loan payments in the budget
    double left_month = 0;         // income - spending
    double needs = 0, wants = 0, saving = 0;   // monthly, for 50/30/20
    double housing = 0;
    double next_year_month = 0;    // spending a year from now if prices keep rising like the last 12 months
    double all_items_yoy = 0;      // overall inflation, your region
    std::string price_month;       // "Aug 2026"
    double real_hourly = 0;        // take-home pay per hour actually worked (0 = unknown)
    double work_hours_year = 0;
    std::vector<CatTotal> cats;    // biggest first, only non-zero
    std::vector<Tip> tips;
};

Summary summarize(const Budget &b);
void normalize(Budget &b);
std::string this_month();          // "2026-10", the format purchases use          // fill in the state from the ZIP if it's missing

// One month of what was really paid, with no double counting: a bill charged to a credit card
// is covered by the card payment once you enter it.
struct CardMonth {
    std::string name;
    int bills = 0;             // bills marked as charged to this card
    double expected = 0;       // those bills this month (what you entered, or the plan)
    bool entered = false;      // a card payment has been entered for this month
    double paid = 0;           // what you paid on the card
    double other() const { return paid - expected; }   // > 0: other charges on the card; < 0: a balance carried over
};
struct MonthBills {
    int bills = 0;             // bills (and card payments) entered for the month
    double paid = 0, plan = 0;
    std::string biggest;       // the one furthest over the plan
    double biggest_over = 0;
    std::vector<CardMonth> cards;   // same order as Budget::cards
};
MonthBills month_bills(const Budget &b, const std::string &month);
// Everything in and out for one month, using what really happened where it's been entered and the plan
// everywhere else. Extra money (gifts, side hustles) counts only in the month it came in.
struct MonthMoney {
    double pay = 0;            // take-home: entered pay, else the estimate
    int pay_entered = 0;       // incomes with a real amount entered for the month
    double extra = 0;          // gifts, side hustles...
    int extras = 0;
    double bills = 0;          // expenses (card payments stand in for the bills on them)
    double loans = 0;          // loan payments in the budget, with any extra
    double one_time = 0;       // costs that happen once: a car repair, a vet visit...
    int one_times = 0;
    double in() const { return pay + extra; }
    double out() const { return bills + loans + one_time; }
    double left() const { return in() - out(); }
};
MonthMoney month_money(const Budget &b, const Summary &s, const std::string &month);
const Card *card_of(const Budget &b, const Expense &e);   // the card it's on, if that card still exists

// Loans already count in the budget. An expense that looks like the same payment (same amount,
// a loan category or the loan's name) is probably counted twice. Returns the loan's index, or -1.
int loan_twin(const Budget &b, const Expense &e);

// A trip or vacation, added up
struct TripCost {
    double gas_price = 0;      // per gallon used
    bool local_gas = false;    // gas price is the average for your region
    double gallons = 0, gas = 0, wear = 0, lodging = 0, food = 0, tickets = 0, fun = 0, total = 0;
    int days = 0;
};
TripCost trip_cost(const Want &w, int region);
double average_gas(int region);     // regular gas, dollars per gallon (0 = unknown)
int months_until(const std::string &month);   // from this month to "2027-06", at least 1 (0 = not set)

// "Is this want worth it?" in plain numbers
struct WantMath {
    double hours = 0;          // hours of work it costs, at real hourly pay
    double share_of_left = 0;  // fraction of one month's leftover money
    double weeks_to_save = 0;  // saving all the leftover money
    std::string verdict;       // friendly one-liner
    int mood = 0;              // 0 fits, 1 save up, 2 tight
};
WantMath want_math(const Summary &s, double price);

// Price series helpers (region 0 = national)
std::string cpi_id(int region, const std::string &item);
const std::vector<Point> *series(const std::string &id);
bool yoy(const std::string &id, double &out, std::string *month = nullptr);
bool region_yoy(int region, const std::string &item, double &out, std::string *month = nullptr);

}  // namespace budget
