// ZIP code lookup from data built into the program (Census 2020 + ACS 5-year medians).
// Works with no internet; your ZIP is only ever looked up here, on your computer.
#pragma once
#include <string>

namespace places {

struct Medians { int income = 0, rent = 0, home = 0; };   // 0 = not published

struct Place {
    bool found = false;       // the ZIP (or one near it) is known
    bool exact = false;       // this exact ZIP is in the Census list
    std::string zip, state, state_name, county;
    int region = 0;           // 1 Northeast, 2 Midwest, 3 South, 4 West
    Medians zip_m, county_m, state_m, us_m;
};

Place lookup(const std::string &zip);
int region_of(const std::string &state);            // 0 if unknown
std::string region_name(int region);
std::string state_name(const std::string &code);
bool valid_state(const std::string &code);
int acs_year();

}  // namespace places
