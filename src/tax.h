// Take-home pay estimate: federal income tax, Social Security, Medicare, state and local tax.
// An estimate for budgeting, not tax advice; real withholding depends on your W-4 and more.
#pragma once
#include <string>
#include <vector>

#include "datapack.h"
#include "model.h"

namespace tax {

struct PerEarner {
    std::string name;
    double gross = 0;          // per year
    double pretax = 0;         // 401(k) + health per year
    double taxes = 0;          // this earner's share of all taxes
    double after_tax = 0;      // after-tax deductions per year
    double net = 0;            // take-home per year
    double checks = 0;         // paychecks per year
    double per_check() const { return checks > 0 ? net / checks : 0; }
};

struct Result {
    double gross = 0, untaxed = 0, pretax = 0, agi = 0;
    double std_deduction = 0, taxable = 0;
    double federal_before_credits = 0, credits = 0, federal = 0;
    double social_security = 0, medicare = 0, self_employment = 0;
    double state = 0, state_payroll = 0, local = 0;
    double after_tax_deductions = 0;
    double net = 0;            // take-home per year (incl. untaxed income)
    double marginal = 0;       // federal bracket your last dollar falls in
    double effective = 0;      // all taxes / gross
    std::string state_code;
    bool state_known = false;
    std::vector<PerEarner> earners;
    std::vector<std::string> notes;
    double total_taxes() const { return federal + social_security + medicare + self_employment + state + state_payroll + local; }
};

Result estimate(const Budget &b, const DataPack &pack);
double bracket_tax(const std::vector<Bracket> &br, double income);
double marginal_rate(const std::vector<Bracket> &br, double income);

}  // namespace tax
