#include "budget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "util.h"

namespace budget {

std::string cpi_id(int region, const std::string &item) {
    return "CUUR0" + std::to_string(region) + "00" + item;
}

const std::vector<Point> *series(const std::string &id) {
    const auto &s = datapack::active().series;
    auto it = s.find(id);
    return it == s.end() || it->second.empty() ? nullptr : &it->second;
}

bool yoy(const std::string &id, double &out, std::string *month) {
    const auto *pts = series(id);
    if (!pts) return false;
    const Point &last = pts->back();
    for (auto it = pts->rbegin(); it != pts->rend(); ++it) {
        if (it->y * 12 + it->m == last.y * 12 + last.m - 12) {
            if (it->v <= 0) return false;
            out = last.v / it->v - 1.0;
            if (month) *month = util::month_name(last.m, true) + " " + std::to_string(last.y);
            return true;
        }
    }
    return false;
}

bool region_yoy(int region, const std::string &item, double &out, std::string *month) {
    if (region > 0 && yoy(cpi_id(region, item), out, month)) return true;
    return yoy(cpi_id(0, item), out, month);
}

void normalize(Budget &b) {
    if (b.home.state.empty() && !b.home.zip.empty()) {
        auto pl = places::lookup(b.home.zip);
        if (pl.found) b.home.state = pl.state;
    }
}

Summary summarize(const Budget &b) {
    Summary s;
    s.place = places::lookup(b.home.zip);
    // A ZIP without a state (hand-edited file, older version): use the ZIP's state
    Budget eff = b;
    if (eff.home.state.empty() && s.place.found) eff.home.state = s.place.state;
    int region = places::region_of(eff.home.state);
    if (!region) region = s.place.region;
    s.region = region;
    s.tax = tax::estimate(eff, datapack::active());
    s.income_month = s.tax.net / 12.0;
    s.gross_month = (s.tax.gross + s.tax.untaxed) / 12.0;

    std::map<std::string, double> by;
    auto add = [&](const std::string &cat, double m, bool need) {
        by[cat] += m;
        s.spend_month += m;
        if (cat == "Savings") s.saving += m;
        else if (need) s.needs += m;
        else s.wants += m;
        if (cat == "Housing") s.housing += m;
    };
    for (const auto &e : b.expenses) add(e.category, e.monthly(), e.need);
    for (const auto &l : b.loans) {
        if (!l.in_budget || l.balance <= 0) continue;
        double min = l.monthly_payment();
        bool mortgage = l.kind == "Mortgage";
        add(mortgage ? "Housing" : "Debt & Loans", min, true);
        if (l.extra > 0) {
            // paying extra on debt is saving-like: it builds your net worth
            by[mortgage ? "Housing" : "Debt & Loans"] += l.extra;
            s.spend_month += l.extra;
            s.saving += l.extra;
        }
    }
    // 401(k) money is saving too, even though it never reaches your bank account
    double retire_month = 0;
    for (const auto &e : b.earners)
        if (e.kind != UNTAXED) retire_month += e.gross_year() * e.retire_pct / 100.0 / 12.0;
    s.left_month = s.income_month - s.spend_month;

    // Real hourly pay: what actually reaches you for each hour you work
    double worked_net = 0;
    for (size_t i = 0; i < b.earners.size() && i < s.tax.earners.size(); i++) {
        const Earner &e = b.earners[i];
        if (e.kind == UNTAXED || e.hours <= 0) continue;
        s.work_hours_year += e.hours * 52.0;
        worked_net += s.tax.earners[i].net;
    }
    if (s.work_hours_year > 0) s.real_hourly = worked_net / s.work_hours_year;

    region_yoy(region, "SA0", s.all_items_yoy, &s.price_month);
    for (const auto &kv : by) {
        if (kv.second <= 0.004) continue;
        CatTotal c;
        c.name = kv.first;
        const Category *cat = b.category(kv.first);
        c.color = cat ? cat->color : 245;
        c.monthly = kv.second;
        std::string item = cat && cat->name == kv.first ? cat->cpi : "SA0";
        if (!item.empty()) c.has_yoy = region_yoy(region, item, c.yoy);
        s.next_year_month += c.monthly * (1.0 + (c.has_yoy ? c.yoy : 0));
        s.cats.push_back(c);
    }
    std::sort(s.cats.begin(), s.cats.end(), [](const CatTotal &a, const CatTotal &b) { return a.monthly > b.monthly; });

    // --- friendly checkups (plain language, never scolding) ---------------------
    auto tip = [&](Mood m, const std::string &t, const std::string &d) { s.tips.push_back({m, t, d}); };
    if (b.earners.empty()) {
        tip(OKAY, "Start with your income", "Add your job on the Income tab (press 2). Tally will estimate your take-home pay.");
        return s;
    }
    if (b.expenses.empty() && b.loans.empty()) {
        tip(OKAY, "Now list your bills", "On the Expenses tab (press 3), press T for a list of common bills to fill in.");
        return s;
    }
    if (s.left_month < -0.5)
        tip(WATCH, "Spending is more than income",
            "You're short " + util::money(-s.left_month, false) + " a month. Look at the biggest bars on the chart: small cuts there add up fastest.");
    else if (s.left_month < s.income_month * 0.05)
        tip(OKAY, "Very little room to spare",
            "Only " + util::money(s.left_month, false) + " a month is unassigned. An unexpected bill could cause trouble; even $20 a month to savings helps.");
    else
        tip(GOOD, "You have money left over",
            util::money(s.left_month, false) + " a month isn't assigned yet. Giving every dollar a job (savings, debt, goals) is called zero-based budgeting.");

    for (const auto &e : b.expenses) {
        int li = loan_twin(b, e);
        if (li < 0) continue;
        tip(WATCH, "Counted twice? " + e.name + " and the loan " + b.loans[(size_t)li].name,
            "Loans on the Loans tab already count in your monthly budget (they show on the Expenses list too). If '" + e.name +
                "' is the same payment, delete it on the Expenses tab so it isn't counted twice.");
    }

    // What was actually bought this month (typed in, or from your own server)
    {
        util::Date t = util::today();
        std::string month = this_month();
        std::map<std::string, double> bought;
        for (const auto &p : b.purchases) if (p.month() == month) bought[p.category] += p.amount;
        double spent = 0, planned = 0;
        for (const auto &kv : bought) {
            spent += kv.second;
            for (const auto &c : s.cats) if (c.name == kv.first) planned += c.monthly;
        }
        if (!bought.empty()) {
            double pace = (double)t.d / util::days_in_month(t.y, t.m);
            std::string cats = bought.size() == 1 ? bought.begin()->first : std::to_string(bought.size()) + " categories";
            if (planned <= 0)
                tip(OKAY, "Bought this month: " + util::money(spent, false) + " (" + cats + ")",
                    "There's no plan for this yet. Add an expense in that category so Tally can compare.");
            else
                tip(spent > planned ? WATCH : spent > planned * pace + 1 ? OKAY : GOOD,
                    "Bought this month: " + util::money(spent, false) + " of " + util::money(planned, false) + " planned (" + cats + ")",
                    spent > planned ? "Over the plan for this month. Maybe the plan needs to grow, or the rest of the month gets lean."
                    : spent > planned * pace + 1 ? "Spending faster than the month: only " + util::percent(pace, 0) + " of it has gone by. Press 7, then B, to see what was bought."
                                                 : "Right on track: " + util::percent(pace, 0) + " of the month has gone by.");
        }
        // bills and pay that change: what really happened this month vs. the plan
        MonthBills mb = month_bills(b, month);
        if (mb.bills) {
            double diff = mb.paid - mb.plan;
            tip(diff > mb.plan * 0.10 ? WATCH : diff > 0.5 ? OKAY : GOOD,
                "Bills so far: " + util::money(mb.paid, false) + " paid vs " + util::money(mb.plan, false) + " planned",
                diff > 0.5 ? util::money(diff, false) + " over across " + std::to_string(mb.bills) + (mb.bills == 1 ? " bill" : " bills") +
                                 (mb.biggest.empty() ? "." : "; the most was " + mb.biggest + " (+" + util::money(mb.biggest_over, false) + ").") +
                                 " If it keeps happening, open the bill and choose 'Use my average as the plan'."
                             : std::to_string(mb.bills) + (mb.bills == 1 ? " bill" : " bills") + " entered, at or under the plan.");
        }
        for (const auto &c : mb.cards) {
            if (!c.entered || c.bills == 0) continue;
            if (c.other() > 0.5)
                tip(c.other() > c.expected * 0.25 ? WATCH : OKAY, c.name + ": " + util::money(c.other(), false) + " beyond the bills on it",
                    "You paid " + util::money(c.paid, false) + " on the card; the bills on it come to " + util::money(c.expected, false) +
                        ". The rest was other spending on the card. Press 3, then C, to see it.");
            else if (c.other() < -0.5)
                tip(OKAY, c.name + ": paid " + util::money(-c.other(), false) + " less than the bills on it",
                    "Whatever isn't paid stays on the card and can grow with interest. If you carry a balance, add it on the Loans tab.");
        }
        double got = 0, expected = 0;
        int earners_in = 0;
        for (size_t i = 0; i < b.earners.size(); i++)
            if (const Actual *a = b.earners[i].actual.find(month)) {
                earners_in++;
                got += a->amount;
                if (i < s.tax.earners.size()) expected += s.tax.earners[i].net / 12;
            }
        if (earners_in) {
            double diff = got - expected;
            tip(diff < -expected * 0.10 ? WATCH : diff < -0.5 ? OKAY : GOOD,
                "Pay this month: " + util::money(got, false) + " vs about " + util::money(expected, false) + " expected",
                diff < -0.5 ? "A lighter month. Bills that can wait a week, or the leftover from last month, can cover the gap."
                            : "At or above the estimate. Extra can go straight to savings or a debt.");
        }
        double extra = 0, side = 0;
        for (const auto &x : b.extras)
            if (x.month() == month) { extra += x.amount; if (x.kind == EXTRA_SIDE) side += x.amount; }
        if (extra > 0.005)
            tip(GOOD, "Extra money this month: " + util::money(extra, false),
                "It isn't part of the plan, so it's a bonus. Putting it toward savings or a debt makes it last." +
                    std::string(side > 0.005 ? " Side hustle money is taxable: setting aside about 25% covers it." : ""));
        double once = 0;
        std::string biggest;
        double big = 0;
        for (const auto &x : b.one_times)
            if (x.month() == month) { once += x.amount; if (x.amount > big) { big = x.amount; biggest = x.name; } }
        if (once > 0.005)
            tip(once > s.left_month && s.left_month > 0 ? WATCH : OKAY, "One-time costs this month: " + util::money(once, false),
                (biggest.empty() ? std::string() : "The biggest is " + biggest + ". ") +
                    (once > s.left_month ? "That's more than a usual month's leftover money, so savings may need to cover part of it."
                                         : "Your usual leftover money covers it. Saving a little each month for surprises makes the next one easier."));
        if (b.link.food_month == month && (b.link.food_wasted > 0 || b.link.food_saved > 0))
            tip(b.link.food_wasted <= b.link.food_saved ? GOOD : OKAY,
                "Food: " + util::money(b.link.food_wasted) + " expired, " + util::money(b.link.food_saved) + " saved by freezing",
                b.link.food_wasted > 0 ? "Freezing meat and bread before the date, and eating oldest-first, keeps money out of the trash."
                                       : "Nothing thrown out this month. Nice work.");
    }
    if (s.gross_month > 0 && s.housing > 0) {
        double share = s.housing / s.gross_month;
        tip(share <= 0.30 ? GOOD : share <= 0.40 ? OKAY : WATCH, "Housing is " + util::percent(share, 0) + " of gross pay",
            "A common guide is 30% or less. Above that, housing squeezes everything else.");
    }
    if (s.income_month > 0) {
        // 401(k) money counts as saving, measured against take-home plus what went to the 401(k)
        double base = s.income_month + retire_month;
        double sav = (s.saving + retire_month) / base;
        double needs = s.needs / base, wants = s.wants / base;
        tip(needs <= 0.5 && sav >= 0.2 ? GOOD : needs <= 0.6 ? OKAY : WATCH,
            "50/30/20: needs " + util::percent(needs, 0) + ", wants " + util::percent(wants, 0) + ", saving " + util::percent(sav, 0),
            "A starting guide: about 50% of take-home for needs, 30% for wants, 20% for saving and extra debt payments.");
    }
    double essentials = s.needs;
    if (essentials > 0) {
        double months = b.home.savings / essentials;
        tip(months >= 3 ? GOOD : months >= 1 ? OKAY : WATCH,
            "Emergency fund: " + util::fixed(months, 1) + " months of needs",
            months >= 3 ? "That's a solid cushion. Three to six months of needs is the usual goal."
                        : "Aim for one month first, then three to six. Set 'Savings now' on the Income tab.");
    }
    if (retire_month <= 0 && !b.earners.empty() && b.earners[0].kind == JOB)
        tip(OKAY, "No retirement saving yet", "If your job matches 401(k) contributions, getting the full match is free money.");
    for (const auto &l : b.loans) {
        if (l.apr >= 15 && l.balance > 0) {
            tip(WATCH, l.name + " charges " + util::fixed(l.apr, 1) + "% interest",
                "High-interest debt usually beats any savings account. The Loans tab shows what extra payments save.");
            break;
        }
    }
    if (s.next_year_month > s.spend_month + 1)
        tip(OKAY, "Prices: about +" + util::money(s.next_year_month - s.spend_month, false) + "/month next year",
            "If prices keep rising like the last 12 months, this budget costs that much more a year from now.");
    return s;
}

int loan_twin(const Budget &b, const Expense &e) {
    for (size_t i = 0; i < b.loans.size(); i++) {
        const Loan &l = b.loans[i];
        if (!l.in_budget || l.balance <= 0) continue;
        double pay = l.monthly_payment(), m = e.monthly();
        if (pay <= 0 || std::fabs(m - pay) > std::max(1.0, pay * 0.02)) continue;
        std::string n = util::lower(e.name), ln = util::lower(l.name), lk = util::lower(l.kind);
        bool loan_cat = e.category == "Debt & Loans" || (l.kind == "Mortgage" && e.category == "Housing");
        bool named = (!ln.empty() && n.find(ln) != std::string::npos) || (!lk.empty() && n.find(lk) != std::string::npos) ||
                     n.find("loan") != std::string::npos || n.find("payment") != std::string::npos;
        if (loan_cat || named) return (int)i;
    }
    return -1;
}

MonthMoney month_money(const Budget &b, const Summary &s, const std::string &month) {
    MonthMoney mm;
    for (size_t i = 0; i < b.earners.size(); i++) {
        if (const Actual *a = b.earners[i].actual.find(month)) { mm.pay += a->amount; mm.pay_entered++; }
        else if (i < s.tax.earners.size()) mm.pay += s.tax.earners[i].net / 12;
    }
    for (const auto &x : b.extras)
        if (x.month() == month) { mm.extra += x.amount; mm.extras++; }
    for (const auto &x : b.one_times)
        if (x.month() == month) { mm.one_time += x.amount; mm.one_times++; }
    MonthBills mb = month_bills(b, month);
    for (const auto &e : b.expenses) {
        const Card *c = card_of(b, e);
        if (c && mb.cards[(size_t)(c - b.cards.data())].entered) continue;   // counted in the card payment below
        const Actual *a = e.actual.find(month);
        mm.bills += a ? a->amount : e.planned_in_month(month);
    }
    for (const auto &cm : mb.cards) if (cm.entered) mm.bills += cm.paid;
    for (const auto &l : b.loans)
        if (l.in_budget && l.balance > 0) mm.loans += l.monthly_payment() + l.extra;
    return mm;
}

const Card *card_of(const Budget &b, const Expense &e) {
    if (e.card.empty()) return nullptr;
    for (const auto &c : b.cards) if (c.name == e.card) return &c;
    return nullptr;
}

MonthBills month_bills(const Budget &b, const std::string &month) {
    MonthBills mb;
    for (const auto &c : b.cards) {
        CardMonth cm;
        cm.name = c.name;
        if (const Actual *a = c.paid.find(month)) { cm.entered = true; cm.paid = a->amount; }
        mb.cards.push_back(cm);
    }
    for (const auto &e : b.expenses) {
        const Actual *a = e.actual.find(month);
        double plan = e.planned_in_month(month);
        int ci = -1;
        if (const Card *c = card_of(b, e)) ci = (int)(c - b.cards.data());
        if (ci >= 0) {
            CardMonth &cm = mb.cards[(size_t)ci];
            cm.bills++;
            cm.expected += a ? a->amount : plan;
            if (cm.entered) continue;   // the card payment covers it: never count it twice
        }
        if (!a) continue;
        mb.bills++;
        mb.paid += a->amount;
        mb.plan += plan;
        if (a->amount - plan > mb.biggest_over) { mb.biggest_over = a->amount - plan; mb.biggest = e.name; }
    }
    for (const auto &cm : mb.cards) {
        if (!cm.entered) continue;
        mb.bills++;
        mb.paid += cm.paid;
        // a card with no bills on it is all "other" spending: nothing was planned for it
        mb.plan += cm.expected;
        if (cm.other() > mb.biggest_over) { mb.biggest_over = cm.other(); mb.biggest = cm.name + " (other charges)"; }
    }
    return mb;
}

double average_gas(int region) {
    if (region > 0)
        if (const auto *s = series("APU0" + std::to_string(region) + "0074714")) return s->back().v;
    if (const auto *s = series("APU000074714")) return s->back().v;
    return 0;
}

int months_until(const std::string &month) {
    if (month.size() != 7) return 0;
    int y = std::atoi(month.c_str()), m = std::atoi(month.c_str() + 5);
    if (y < 1900 || m < 1 || m > 12) return 0;
    util::Date t = util::today();
    return std::max(1, (y - t.y) * 12 + (m - t.m));
}

TripCost trip_cost(const Want &w, int region) {
    TripCost t;
    double avg = average_gas(region);
    t.local_gas = w.gas_price <= 0 && avg > 0;
    t.gas_price = w.gas_price > 0 ? w.gas_price : avg;
    if (w.miles > 0 && w.mpg > 0) t.gallons = w.miles / w.mpg;
    t.gas = t.gallons * t.gas_price;
    t.wear = std::max(0.0, w.miles) * std::max(0.0, w.wear);
    t.lodging = std::max(0, w.nights) * std::max(0.0, w.lodging);
    t.days = std::max(1, w.nights + 1);
    t.food = t.days * std::max(0, w.meals) * std::max(1, w.people) * std::max(0.0, w.meal_cost);
    t.tickets = std::max(0.0, w.tickets);
    t.fun = std::max(0.0, w.fun);
    t.total = t.gas + t.wear + t.lodging + t.food + t.tickets + t.fun;
    return t;
}

std::string this_month() {
    util::Date t = util::today();
    char m[16];
    std::snprintf(m, sizeof m, "%04d-%02d", t.y, t.m);
    return m;
}

WantMath want_math(const Summary &s, double price) {
    WantMath w;
    if (price <= 0) { w.verdict = "Enter a price to see the math."; return w; }
    if (s.real_hourly > 0) w.hours = price / s.real_hourly;
    double left = s.left_month;
    if (left > 0) {
        w.share_of_left = price / left;
        w.weeks_to_save = price / (left * 12.0 / 52.0);
    }
    std::string hrs = s.real_hourly > 0 ? util::fixed(w.hours, w.hours < 10 ? 1 : 0) + " hours of work" : "";
    if (left <= 0) {
        w.mood = 2;
        w.verdict = "Your budget has no money left over right now, so this would come from savings or a bill.";
    } else if (price <= left * 0.25) {
        w.mood = 0;
        w.verdict = "Fits easily: " + util::percent(w.share_of_left, 0) + " of this month's leftover money.";
    } else if (price <= left) {
        w.mood = 0;
        w.verdict = "Fits this month, but uses " + util::percent(w.share_of_left, 0) + " of your leftover money.";
    } else {
        w.mood = 1;
        int weeks = (int)std::ceil(w.weeks_to_save);
        w.verdict = "Save for about " + std::to_string(weeks) + (weeks == 1 ? " week" : " weeks") +
                    (weeks > 8 ? " (" + util::fixed(weeks / 4.33, 0) + " months)" : "") + " to buy it without debt.";
    }
    if (!hrs.empty()) w.verdict = "Costs " + hrs + ". " + w.verdict;
    return w;
}

}  // namespace budget
