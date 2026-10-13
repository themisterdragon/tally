#include "places.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace places {

namespace {
struct PlaceCounts { int income, rent, home; };
struct StateInfo { const char *code; const char *name; int region; PlaceCounts m; };
struct CountyInfo { int state; const char *name; PlaceCounts m; };
struct ZipInfo { int zip; int county; PlaceCounts m; };
#include "data/places.inc"

Medians conv(const PlaceCounts &c) { return {c.income, c.rent, c.home}; }
}  // namespace

int acs_year() { return ACS_YEAR; }

Place lookup(const std::string &zip_text) {
    Place p;
    p.us_m = conv(US_MEDIANS);
    std::string digits;
    for (char c : zip_text)
        if (c >= '0' && c <= '9') digits += c;
    if (digits.size() < 5) return p;
    digits = digits.substr(0, 5);
    int z = std::atoi(digits.c_str());
    p.zip = digits;
    const ZipInfo *begin = ZIPS, *end = ZIPS + std::size(ZIPS);
    const ZipInfo *it = std::lower_bound(begin, end, z, [](const ZipInfo &a, int v) { return a.zip < v; });
    const ZipInfo *hit = nullptr;
    if (it != end && it->zip == z) {
        hit = it;
        p.exact = true;
    } else {
        // PO-box and business ZIPs aren't in the Census list: use the closest ZIP
        // with the same first three digits (same mail region, so same state).
        const ZipInfo *cands[2] = {it != begin ? it - 1 : nullptr, it != end ? it : nullptr};
        for (auto c : cands)
            if (c && c->zip / 100 == z / 100 && (!hit || std::abs(c->zip - z) < std::abs(hit->zip - z))) hit = c;
    }
    if (!hit) return p;
    const CountyInfo &c = COUNTIES[hit->county];
    const StateInfo &s = STATES[c.state];
    p.found = true;
    p.state = s.code;
    p.state_name = s.name;
    p.region = s.region;
    p.county = c.name;
    if (p.exact) p.zip_m = conv(hit->m);
    p.county_m = conv(c.m);
    p.state_m = conv(s.m);
    return p;
}

int region_of(const std::string &code) {
    for (const auto &s : STATES)
        if (code == s.code) return s.region;
    return 0;
}

std::string region_name(int r) {
    static const char *n[] = {"U.S. average", "Northeast", "Midwest", "South", "West"};
    return n[(r >= 0 && r <= 4) ? r : 0];
}

std::string state_name(const std::string &code) {
    for (const auto &s : STATES)
        if (code == s.code) return s.name;
    return code;
}

bool valid_state(const std::string &code) { return region_of(code) != 0; }

}  // namespace places
