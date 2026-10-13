// The yearly numbers Tally needs: tax tables and price history.
// One plain-text "data pack". A copy is built into the program; Update (only when
// you ask) can download a newer one, or you can import one from a file or USB stick.
#pragma once
#include <map>
#include <string>
#include <vector>

struct Bracket { double over, rate; };

struct StateTax {
    std::vector<Bracket> single, joint;   // empty = no wage income tax
    double std_single = 0, std_joint = 0;
    bool std_credit = false;              // the "deduction" is really a credit off the tax
    double ex_single = 0, ex_joint = 0, ex_dep = 0;
    bool ex_single_credit = false, ex_joint_credit = false, ex_dep_credit = false;
    double payroll_pct = 0;               // employee-paid state payroll programs (e.g. CA SDI)
};

struct Federal {
    double std_ded[4] = {0, 0, 0, 0};     // by Filing
    std::vector<Bracket> brackets[4];
    double ss_rate = 0.062, ss_base = 184500, med_rate = 0.0145, addl_rate = 0.009;
    double addl_threshold[4] = {200000, 250000, 200000, 125000};
    double ctc = 2200, ctc_refundable = 1700, odc = 500, phase_single = 200000, phase_joint = 400000;
};

struct Point { int y, m; double v; };

struct DataPack {
    std::string date;          // when it was built, YYYY-MM-DD
    int tax_year = 0;
    std::string source;
    std::string origin;        // "built in", "downloaded", "imported"
    Federal fed;
    std::map<std::string, StateTax> states;
    std::map<std::string, std::vector<Point>> series;

    bool parse(const std::string &text, std::string &error);
    std::string text() const;  // back to the file format (for sharing a pack)
};

namespace datapack {
const DataPack &active();            // newest of built-in / saved pack (+ fresher prices)
void reload();                       // after Update or Import
const DataPack &built_in();
bool load_saved(DataPack &out);      // the saved pack in the data folder, if any
std::string saved_path();
std::string prices_path();
}
