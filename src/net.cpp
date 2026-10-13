#include "net.h"

#include <cctype>
#include <cstring>

#include <cstdio>
#include <cstdlib>

#include "util.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace net {

const J &J::operator[](const std::string &k) const {
    static J none;
    auto it = o.find(k);
    return it == o.end() ? none : it->second;
}

double J::num() const {
    double v = 0;
    if (t == NUM || t == STR) util::parse_number(s, v);
    return v;
}

namespace {

void put_utf8(std::string &out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

struct Reader {
    const std::string &s;
    size_t i = 0;
    bool bad = false;
    explicit Reader(const std::string &str) : s(str) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
    unsigned hex4() {
        if (i + 4 >= s.size()) { bad = true; return 0; }
        unsigned v = (unsigned)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
        i += 4;
        return v;
    }
    std::string str() {
        std::string out;
        i++;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                char c = s[i];
                if (c == 'n') out += '\n';
                else if (c == 't') out += '\t';
                else if (c == 'r' || c == 'b' || c == 'f') {}
                else if (c == 'u') {
                    // a surrogate pair makes one character; half of one, or a broken pair, is U+FFFD
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        size_t back = i;
                        i += 2;
                        unsigned lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { cp = 0xFFFD; i = back; }   // read the second \u on its own next time
                    }
                    if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
                    put_utf8(out, cp);
                } else out += c;
            } else {
                out += s[i];
            }
            i++;
        }
        i++;
        return out;
    }
    J value(int depth = 0) {
        J v;
        ws();
        if (i >= s.size() || depth > 64) { bad = true; return v; }
        char c = s[i];
        if (c == '{') {
            v.t = J::OBJ;
            i++;
            ws();
            if (i < s.size() && s[i] == '}') { i++; return v; }
            while (i < s.size() && !bad) {
                ws();
                if (i >= s.size() || s[i] != '"') { bad = true; break; }
                std::string k = str();
                ws();
                if (i >= s.size() || s[i] != ':') { bad = true; break; }
                i++;
                v.o[k] = value(depth + 1);
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                bad = true;
            }
        } else if (c == '[') {
            v.t = J::ARR;
            i++;
            ws();
            if (i < s.size() && s[i] == ']') { i++; return v; }
            while (i < s.size() && !bad) {
                v.a.push_back(value(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                bad = true;
            }
        } else if (c == '"') {
            v.t = J::STR;
            v.s = str();
        } else {
            size_t st = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') i++;
            v.s = s.substr(st, i - st);
            v.t = (v.s == "true" || v.s == "false") ? J::BOOL : v.s == "null" ? J::NUL : J::NUM;
            if (v.s.empty()) bad = true;
        }
        return v;
    }
};

FILE *open_pipe(const std::string &cmd) {
#ifdef _WIN32
    return _popen(cmd.c_str(), "rb");
#else
    return popen(cmd.c_str(), "r");
#endif
}

int close_pipe(FILE *p) {
#ifdef _WIN32
    return _pclose(p);
#else
    int st = pclose(p);   // a wait status: the exit code is inside it
    return st != -1 && WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

// --max-filesize lets curl stop early when the server says the size up front
#ifdef _WIN32
const char *CURL = "curl.exe -q -sS --http1.1 --max-filesize 20971520 -A Tally ";
const char *QUIET = " 2>NUL";
#else
const char *CURL = "curl -q -sS --http1.1 --max-filesize 20971520 -A Tally ";
const char *QUIET = " 2>/dev/null";
#endif

// Nothing Tally downloads comes near this; a reply that does is broken or hostile.
const size_t MAX_DOWNLOAD = 20 * 1024 * 1024;

bool read_all(const std::string &cmd, std::string &out, int &rc) {
    FILE *p = open_pipe(cmd);
    if (!p) return false;
    char buf[8192];
    size_t n;
    out.clear();
    bool too_big = false;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) {
        if (!too_big) out.append(buf, n);
        if (out.size() > MAX_DOWNLOAD) { too_big = true; out.clear(); }   // keep reading so curl can finish
    }
    rc = close_pipe(p);
    if (too_big) rc = 63;   // curl's own "too big" code
    return true;
}

}  // namespace

J parse_json(const std::string &text, bool &bad) {
    Reader rd(text);
    J v = rd.value();
    bad = rd.bad;
    return v;
}

std::string json_str(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

std::string quote_arg(const std::string &s) {
#ifdef _WIN32
    return "\"" + s + "\"";  // Windows paths can't contain quotes
#else
    std::string o = "'";
    for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return o + "'";
#endif
}

bool curl_available() { return util::have_program("curl"); }

bool safe_url(const std::string &url) {
    if (url.empty() || url.size() > 2000) return false;
    for (unsigned char c : url)
        if (!(std::isalnum(c) || std::strchr("-._~:/?#[]@&+,;=", c))) return false;   // no % ! ^ " ' ` $ \ spaces
    return true;
}

bool run_curl(const std::string &args, std::string &out) {
    int rc = -1;
    if (!read_all(std::string(CURL) + "-f -L --connect-timeout 10 -m 60 " + args + QUIET, out, rc)) return false;
    return rc == 0 && !out.empty();
}

int fetch(const std::string &url, const std::string &config, std::string &out) {
    // The status code comes last, on its own line, so we can tell "wrong pairing code"
    // from "server not running".
    if (!safe_url(url)) return 0;
    std::string args = "-m 30 --connect-timeout 8 -w \"\\n%{http_code}\" ";
    if (!config.empty()) args += "-K " + quote_arg(config) + " ";
    int rc = -1;
    if (!read_all(std::string(CURL) + args + quote_arg(url) + QUIET, out, rc)) return 0;
    if (rc == 63) { out.clear(); return 413; }   // the reply was too big to be real
    size_t nl = out.find_last_of('\n');
    if (nl == std::string::npos) return 0;
    int code = std::atoi(out.c_str() + nl + 1);
    out.erase(nl);
    return code;
}

}  // namespace net
