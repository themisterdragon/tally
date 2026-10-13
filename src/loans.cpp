#include "loans.h"

#include <algorithm>
#include <cmath>

namespace loans {

static const int MAX_MONTHS = 1200;  // 100 years: anything longer counts as "never"

double payment_for(double balance, double apr, int months) {
    if (months <= 0 || balance <= 0) return 0;
    double r = apr / 100.0 / 12.0;
    if (r <= 0) return balance / months;
    return balance * r / (1.0 - std::pow(1.0 + r, -months));
}

double interest_first_month(double balance, double apr) { return balance * apr / 100.0 / 12.0; }

std::vector<Row> schedule(double balance, double apr, double payment, double extra, double lump) {
    std::vector<Row> rows;
    double r = apr / 100.0 / 12.0;
    double b = std::max(0.0, balance - lump);
    for (int m = 1; b > 0.005 && m <= MAX_MONTHS; m++) {
        double interest = b * r;
        double pay = std::min(payment + extra, b + interest);
        if (pay <= interest + 1e-9) {  // never shrinks
            rows.push_back({m, pay, interest, 0, b});
            break;
        }
        b = b + interest - pay;
        rows.push_back({m, pay, interest, pay - interest, std::max(0.0, b)});
    }
    return rows;
}

Payoff simulate(double balance, double apr, double payment, double extra, double lump) {
    Payoff p;
    p.paid = std::min(lump, balance);
    if (balance - lump <= 0.005) return p;
    auto rows = schedule(balance, apr, payment, extra, lump);
    if (rows.empty() || rows.back().balance > 0.005) {
        p.never = true;
        return p;
    }
    p.months = (int)rows.size();
    for (const auto &r : rows) { p.interest += r.interest; p.paid += r.payment; }
    return p;
}

PlanResult plan(const std::vector<Loan> &input, double extra, Strategy s) {
    struct L { std::string name; double b, apr, min; bool done; };
    std::vector<L> ls;
    for (const auto &l : input)
        if (l.balance > 0) ls.push_back({l.name, l.balance, l.apr, l.monthly_payment(), false});
    PlanResult res;
    if (ls.empty()) return res;
    // Avalanche: highest rate first (least interest). Snowball: smallest balance first (quick wins).
    std::stable_sort(ls.begin(), ls.end(), [s](const L &a, const L &b) {
        return s == AVALANCHE ? a.apr > b.apr : a.b < b.b;
    });
    double budget = extra;
    for (const auto &l : ls) budget += l.min;
    for (int m = 1; m <= MAX_MONTHS; m++) {
        double left = budget;
        for (auto &l : ls) {  // interest, then minimums
            if (l.done) continue;
            double i = l.b * l.apr / 100.0 / 12.0;
            res.interest += i;
            l.b += i;
            double pay = std::min(l.min, l.b);
            l.b -= pay;
            left -= pay;
        }
        for (auto &l : ls) {  // everything else to the first loan still open, in strategy order
            if (l.done || left <= 0) continue;
            double pay = std::min(left, l.b);
            l.b -= pay;
            left -= pay;
        }
        bool all = true;
        for (auto &l : ls) {
            if (!l.done && l.b <= 0.005) { l.done = true; res.order.push_back(l.name); }
            all = all && l.done;
        }
        if (all) { res.months = m; return res; }
    }
    res.never = true;
    return res;
}

}  // namespace loans
