// The optional link to a server you run yourself (for example, a phone-friendly web page where
// you scan barcodes and type prices). Off unless you turn it on, and only runs when you ask.
//
// Tally only DOWNLOADS from the server. Each request carries your pairing code and a bookmark
// number the server gave Tally last time; nothing from your budget is ever uploaded.
// The protocol is written up in docs/SERVER-LINK.md.
#pragma once
#include <string>

#include "model.h"

namespace server {

struct Outcome {
    bool ok = false;
    std::string message;
    int added = 0;
    double added_total = 0;
};

// Checks the address. Plain http:// is allowed only inside your home network
// (192.168.x.x, 10.x.x.x, localhost, name.local, Tailscale 100.x...). Returns "" when it's fine.
std::string address_problem(const std::string &url);
bool code_ok(const std::string &code);

bool valid_date(const std::string &iso);             // "2026-10-12"
std::string today_iso();

std::string describe(const ServerLink &l);           // shown before anything is sent
Outcome check(const ServerLink &l);                  // "is it there, and is the code right?"
Outcome pull(Budget &b);                             // download new purchases + food totals
Outcome import_file(Budget &b, const std::string &path);   // same data from a file, no network

}  // namespace server
