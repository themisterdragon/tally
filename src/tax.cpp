#include "tax.h"

#include <algorithm>
#include <cmath>

#include "util.h"

namespace tax {

double bracket_tax(const std::vector<Bracket> &br, double income) {
    double t = 0;
    for (size_t i = 0; i < br.size(); i++) {
        double lo = br[i].over, hi = i + 1 < br.size() ? br[i + 1].over : 1e300;
        if (income <= lo) break;
        t += (std::min(income, hi) - lo) * br[i].rate;
    }
    return t;
}

double marginal_rate(const std::vector<Bracket> &br, double income) {
    double r = 0;
    for (const auto &b : br)
        if (income > b.over) r = b.rate;
    return r;
}

Result estimate(const Budget &b, const DataPack &pack) {
    Result r;
    const Federal &f = pack.fed;
    int fi = std::min<int>(b.home.filing, 3);
    bool joint = b.home.filing == JOINT;

    double wages = 0, se_net = 0;
    std::vector<double> earner_wages;
    for (const auto &e : b.earners) {
        PerEarner p;
        p.name = e.name;
        p.gross = e.gross_year();
        p.checks = e.checks_year();
        p.after_tax = e.after_tax * p.checks;
        if (e.kind == UNTAXED) {
            r.untaxed += p.gross;
        } else {
            double retire = p.gross * std::max(0.0, std::min(100.0, e.retire_pct)) / 100.0, health = e.health * p.checks;
            p.pretax = std::min(p.gross, retire + health);
            r.pretax += p.pretax;
            r.gross += p.gross;
            if (e.kind == JOB) {
                // FICA: Social Security up to the wage base, Medicare on everything.
                // Health (cafeteria plan) is FICA-free; 401(k) is not.
                double fica_wages = std::max(0.0, p.gross - health);
                double ss = std::min(fica_wages, f.ss_base) * f.ss_rate;
                double med = fica_wages * f.med_rate;
                r.social_security += ss;
                r.medicare += med;
                p.taxes += ss + med;
                wages += fica_wages;
            } else {
                // Self-employment tax: both halves of FICA on 92.35% of net earnings
                double base = std::max(0.0, p.gross - health) * 0.9235;
                double se = std::min(base, f.ss_base) * f.ss_rate * 2 + base * f.med_rate * 2;
                r.self_employment += se;
                p.taxes += se;
                se_net += base;
            }
        }
        r.earners.push_back(p);
        earner_wages.push_back(e.kind == UNTAXED ? 0 : p.gross - p.pretax);
    }
    // Additional Medicare tax on high earners (household threshold)
    double addl = std::max(0.0, wages + se_net - f.addl_threshold[fi]) * f.addl_rate;
    r.medicare += addl;

    // Federal income tax
    r.agi = std::max(0.0, r.gross - r.pretax - r.self_employment / 2);
    r.std_deduction = f.std_ded[fi];
    r.taxable = std::max(0.0, r.agi - r.std_deduction);
    r.federal_before_credits = bracket_tax(f.brackets[fi], r.taxable);
    r.marginal = marginal_rate(f.brackets[fi], r.taxable);
    double phase_at = joint ? f.phase_joint : f.phase_single;
    double credit = b.home.kids * f.ctc + b.home.other_deps * f.odc;
    credit = std::max(0.0, credit - std::ceil(std::max(0.0, r.agi - phase_at) / 1000.0) * 50.0);
    double nonrefund = std::min(credit, r.federal_before_credits);
    // Part of the child credit is refundable: 15% of earnings over $2,500, capped per child
    double refundable = std::min({credit - nonrefund, b.home.kids * f.ctc_refundable,
                                  std::max(0.0, (r.gross - 2500) * 0.15)});
    r.credits = nonrefund + std::max(0.0, refundable);
    r.federal = r.federal_before_credits - r.credits;   // can go negative = refund

    // State income tax
    r.state_code = b.home.state;
    auto it = pack.states.find(b.home.state);
    if (it != pack.states.end()) {
        r.state_known = true;
        const StateTax &s = it->second;
        bool use_joint = joint && !s.joint.empty();
        const auto &br = use_joint ? s.joint : s.single;
        double sd = joint ? s.std_joint : s.std_single;
        double ex = joint ? s.ex_joint : s.ex_single;
        bool ex_credit = joint ? s.ex_joint_credit : s.ex_single_credit;
        int deps = b.home.kids + b.home.other_deps;
        double income = r.agi;
        if (!s.std_credit) income -= sd;
        if (!ex_credit) income -= ex;
        if (!s.ex_dep_credit) income -= deps * s.ex_dep;
        double t = bracket_tax(br, std::max(0.0, income));
        if (s.std_credit) t -= sd;
        if (ex_credit) t -= ex;
        if (s.ex_dep_credit) t -= deps * s.ex_dep;
        r.state = std::max(0.0, t);
        r.state_payroll = (wages) * s.payroll_pct / 100.0;
        if (br.empty()) r.notes.push_back("Your state has no income tax on wages.");
    } else {
        r.notes.push_back("Pick your state (Income > Household) to include state income tax.");
    }
    r.local = (wages + se_net) * b.home.local_tax / 100.0;

    // Share household-level taxes out to earners by their taxable wages
    double total_w = 0;
    for (double w : earner_wages) total_w += w;
    double shared = r.federal + r.state + r.state_payroll + r.local + addl;
    for (size_t i = 0; i < r.earners.size(); i++) {
        auto &p = r.earners[i];
        if (total_w > 0) p.taxes += shared * earner_wages[i] / total_w;
        p.net = p.gross - p.pretax - p.taxes - p.after_tax;
        r.after_tax_deductions += p.after_tax;
    }
    r.net = r.gross + r.untaxed - r.pretax - r.total_taxes() - r.after_tax_deductions;
    r.effective = r.gross > 0 ? r.total_taxes() / r.gross : 0;
    return r;
}

}  // namespace tax
