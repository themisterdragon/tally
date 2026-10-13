// Tax tables and prices: downloaded only when you choose Update. (Network code lives here, in
// progupdate.cpp for Update Tally, and in server.cpp for the optional server link.)
// It downloads fixed public files with the system's curl program. Nothing you typed
// (ZIP, income, expenses, loans) is ever part of a request: every Tally user sends
// exactly the same request, so it can't tell anyone anything about you.
#pragma once
#include <string>
#include <vector>

namespace update {

bool curl_available();
std::vector<std::string> price_series();          // the fixed list of public BLS series
std::string describe_requests(const std::string &pack_url);   // shown before anything is sent

struct Outcome { bool ok = false; std::string message; };
Outcome update_prices();                           // BLS consumer prices -> prices.txt
Outcome update_pack(const std::string &url);       // newer data pack -> datapack.txt
Outcome import_pack(const std::string &path);      // from a file / USB stick (no internet)
Outcome export_pack(const std::string &path);      // share the current pack as a file

}  // namespace update
