// Shared plumbing for the two optional, user-triggered network features (Update and the
// server link): the system's curl program, and a tiny JSON reader. Nothing here runs by itself.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace net {

struct J {
    enum T { NUL, STR, NUM, ARR, OBJ, BOOL } t = NUL;
    std::string s;               // text of a string, number, or true/false
    std::vector<J> a;
    std::map<std::string, J> o;
    const J &operator[](const std::string &k) const;
    double num() const;          // numbers, or numbers sent as strings ("5.49")
};

J parse_json(const std::string &text, bool &bad);
std::string json_str(const std::string &s);     // a quoted, escaped JSON string

std::string quote_arg(const std::string &s);
bool curl_available();
// Only plain web-address characters: nothing a shell (cmd.exe or sh) could treat as special.
// Every address is checked with this before it reaches curl, even from a hand-edited budget file.
bool safe_url(const std::string &url);

// Runs curl with fixed options. -q ignores any personal ~/.curlrc so nothing extra
// (cookies, headers, proxies with credentials) rides along. True on a 2xx reply with a body.
bool run_curl(const std::string &args, std::string &out);

// Like run_curl, but doesn't follow redirects and reports the HTTP status
// (0 = couldn't connect at all). `config` is an optional curl config file (-K) for
// headers we don't want showing up in the process list, such as a pairing code.
int fetch(const std::string &url, const std::string &config, std::string &out);

}  // namespace net
