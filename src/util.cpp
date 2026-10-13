#include "util.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace util {

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool starts_with(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string money(double v, bool cents) {
    bool neg = v < 0;
    double a = std::fabs(v);
    long long whole;
    int frac = 0;
    if (cents) {
        long long c = std::llround(a * 100.0);
        whole = c / 100;
        frac = (int)(c % 100);
    } else {
        whole = std::llround(a);
    }
    std::string digits = std::to_string(whole), grouped;
    int n = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        grouped.insert(grouped.begin(), digits[i]);
        if (++n % 3 == 0 && i > 0) grouped.insert(grouped.begin(), ',');
    }
    std::string out = (neg ? "-$" : "$") + grouped;
    if (cents) {
        char buf[8];
        std::snprintf(buf, sizeof buf, ".%02d", frac);
        out += buf;
    }
    return out;
}

std::string fixed(double v, int places) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", places, v);
    return buf;
}

std::string percent(double f, int places) { return fixed(f * 100.0, places) + "%"; }

bool parse_number(const std::string &s, double &out) {
    std::string t;
    for (char c : s)
        if (c != ',' && c != '$' && c != '%' && c != ' ') t += c;
    if (t.empty()) return false;
    char *end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (!end || *end || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// --- UTF-8 -----------------------------------------------------------------

static size_t char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 6) return 2;
    if ((c >> 4) == 14) return 3;
    if ((c >> 3) == 30) return 4;
    return 1;
}

size_t width(const std::string &s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size(); i += char_len((unsigned char)s[i])) w++;
    return w;
}

std::string cut(const std::string &s, size_t cols) {
    size_t w = 0, i = 0;
    while (i < s.size() && w < cols) { i += char_len((unsigned char)s[i]); w++; }
    return s.substr(0, i);
}

std::string fit(const std::string &s, size_t cols) {
    size_t w = width(s);
    if (w <= cols) return s + std::string(cols - w, ' ');
    if (cols == 0) return "";
    return cut(s, cols - 1) + "\xE2\x80\xA6";  // …
}

std::string pad_left(const std::string &s, size_t cols) {
    size_t w = width(s);
    return w >= cols ? s : std::string(cols - w, ' ') + s;
}

std::vector<std::string> wrap(const std::string &s, size_t cols) {
    std::vector<std::string> lines;
    for (const auto &para : split(s, '\n')) {
        std::string line;
        std::istringstream words(para);
        std::string w;
        while (words >> w) {
            while (width(w) > cols) {  // very long word: hard-split
                if (!line.empty()) { lines.push_back(line); line.clear(); }
                lines.push_back(cut(w, cols));
                w = w.substr(cut(w, cols).size());
            }
            if (line.empty()) line = w;
            else if (width(line) + 1 + width(w) <= cols) line += " " + w;
            else { lines.push_back(line); line = w; }
        }
        lines.push_back(line);
    }
    return lines;
}

void pop_utf8(std::string &s) {
    while (!s.empty()) {
        unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

// --- dates -----------------------------------------------------------------

Date today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[(m - 1) % 12];
}

std::string month_name(int m, bool shortname) {
    static const char *names[] = {"January", "February", "March", "April", "May", "June", "July",
                                  "August", "September", "October", "November", "December"};
    std::string n = names[(m - 1 + 1200) % 12];
    return shortname ? n.substr(0, 3) : n;
}

std::string date_text(const Date &d) {
    return month_name(d.m, true) + " " + std::to_string(d.d) + ", " + std::to_string(d.y);
}

std::string stamp_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string add_months_text(int months) {
    Date d = today();
    int total = d.y * 12 + (d.m - 1) + months;
    return month_name(total % 12 + 1, true) + " " + std::to_string(total / 12);
}

std::string ordinal(int n) {
    int t = n % 100;
    if (t >= 11 && t <= 13) return "th";
    switch (n % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; default: return "th"; }
}

// --- files -----------------------------------------------------------------

static std::string env(const char *name) {
    const char *v = std::getenv(name);
    return v ? v : "";
}

std::string exe_path() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return n ? std::string(buf, n) : "";
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    std::error_code ec;
    auto p = fs::weakly_canonical(fs::path(buf), ec);
    return ec ? "" : p.string();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? "" : p.string();
#endif
}

static std::string exe_dir() {
    std::string p = exe_path();
    return p.empty() ? "" : fs::path(p).parent_path().string();
}

std::string data_dir() {
    std::string d = env("TALLY_DATA");
    if (!d.empty()) return d;
    // Portable mode: a "tally-data" folder beside the program (for a USB stick) wins.
    std::string ex = exe_dir();
    if (!ex.empty() && exists(join(ex, "tally-data"))) return join(ex, "tally-data");
#ifdef _WIN32
    std::string app = env("APPDATA");
    return join(app.empty() ? "." : app, "Tally");
#elif defined(__APPLE__)
    return join(env("HOME"), "Library/Application Support/Tally");
#else
    std::string x = env("XDG_DATA_HOME");
    return x.empty() ? join(env("HOME"), ".local/share/tally") : join(x, "tally");
#endif
}

std::string documents_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, buf))) return buf;
    return join(env("USERPROFILE"), "Documents");
#else
    std::string home = env("HOME");
    std::string docs = join(home, "Documents");
    return exists(docs) ? docs : home;
#endif
}

bool make_dirs(const std::string &p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return fs::is_directory(p, ec);
}

bool read_file(const std::string &path, std::string &out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_atomic(const std::string &path, const std::string &data, bool private_file) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(fs::u8path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        f.flush();
        if (!f) return false;
    }
#ifndef _WIN32
    if (private_file) chmod(tmp.c_str(), 0600);  // only you can read your budget
#else
    (void)private_file;
#endif
    std::error_code ec;
    fs::rename(fs::u8path(tmp), fs::u8path(path), ec);
    if (ec) {  // Windows can refuse to replace; fall back to remove + rename
        fs::remove(fs::u8path(path), ec);
        fs::rename(fs::u8path(tmp), fs::u8path(path), ec);
    }
    return !ec;
}

bool exists(const std::string &path) {
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec);
}

std::string join(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    return (fs::u8path(a) / fs::u8path(b)).u8string();
}

bool have_program(const std::string &name) {
#ifdef _WIN32
    char buf[MAX_PATH];
    return SearchPathA(nullptr, (name + ".exe").c_str(), nullptr, MAX_PATH, buf, nullptr) > 0;
#else
    for (const auto &dir : split(env("PATH"), ':'))
        if (!dir.empty() && access(join(dir, name).c_str(), X_OK) == 0) return true;
    return false;
#endif
}

void open_with_system(const std::string &path) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    // double fork so the viewer is never left as a zombie child of Tally
    pid_t pid = fork();
    if (pid == 0 && fork() != 0) _exit(0);
    if (pid == 0) {
        // detach from the terminal so the viewer can't scribble on Tally's screen
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
#ifdef __APPLE__
        execlp("open", "open", path.c_str(), (char *)nullptr);
#else
        execlp("xdg-open", "xdg-open", path.c_str(), (char *)nullptr);
#endif
        _exit(127);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
#endif
}

}  // namespace util

namespace util {

void rgb256(int c, int &r, int &g, int &b) {
    static const int base[16][3] = {{0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0}, {0, 0, 128}, {128, 0, 128},
                                    {0, 128, 128}, {192, 192, 192}, {128, 128, 128}, {255, 0, 0}, {0, 255, 0},
                                    {255, 255, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    if (c < 0) c = 7;
    if (c < 16) { r = base[c][0]; g = base[c][1]; b = base[c][2]; return; }
    if (c >= 232) { r = g = b = 8 + (c - 232) * 10; return; }
    static const int lv[] = {0, 95, 135, 175, 215, 255};
    int i = c - 16;
    r = lv[i / 36];
    g = lv[(i / 6) % 6];
    b = lv[i % 6];
}

std::string hex256(int c) {
    int r, g, b;
    rgb256(c, r, g, b);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X%02X%02X", r, g, b);
    return buf;
}

bool is_dark256(int c) {
    int r, g, b;
    rgb256(c, r, g, b);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b < 140;
}

}  // namespace util
