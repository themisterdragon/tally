// Loan math: payments, payoff time, interest saved, and payoff strategies.
#pragma once
#include <string>
#include <vector>

#include "model.h"

namespace loans {

double payment_for(double balance, double apr_percent, int months);   // standard amortized payment
double interest_first_month(double balance, double apr_percent);

struct Payoff {
    int months = 0;          // until the balance hits zero
    double interest = 0;     // total interest paid
    double paid = 0;         // everything paid
    bool never = false;      // payment doesn't even cover the interest
};
Payoff simulate(double balance, double apr_percent, double payment, double extra = 0, double lump = 0);

struct Row { int month; double payment, interest, principal, balance; };
std::vector<Row> schedule(double balance, double apr_percent, double payment, double extra, double lump);

// Several loans, one total monthly amount. Minimum payments go to every loan; anything
// left over (plus payments freed by loans already paid off) goes to the target loan.
enum Strategy { AVALANCHE, SNOWBALL };
struct PlanResult { int months = 0; double interest = 0; bool never = false; std::vector<std::string> order; };
PlanResult plan(const std::vector<Loan> &loans, double extra_per_month, Strategy s);

}  // namespace loans
