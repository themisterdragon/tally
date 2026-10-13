// Small helpers shared by every part of Tally: text, money, dates, files.
#pragma once
#include <string>
#include <vector>

namespace util {

std::string trim(const std::string &s);
std::string lower(std::string s);
std::vector<std::string> split(const std::string &s, char sep);
bool starts_with(const std::string &s, const std::string &prefix);

// "$1,234.56" (cents shown), "$1,235" (whole dollars), "-$12.00"
std::string money(double v, bool cents = true);
std::string percent(double fraction, int places = 1);  // 0.034 -> "3.4%"
std::string fixed(double v, int places);
// Reads "1,234.50", "$99", "12%" etc. Returns false when it isn't a number.
bool parse_number(const std::string &s, double &out);

// UTF-8 aware
size_t width(const std::string &s);                   // columns on screen
std::string fit(const std::string &s, size_t cols);   // cut (with "…") or pad to exactly cols
std::string pad_left(const std::string &s, size_t cols);
std::string cut(const std::string &s, size_t cols);   // cut only, no padding
std::vector<std::string> wrap(const std::string &s, size_t cols);
void pop_utf8(std::string &s);                        // remove last character

// Dates (local time)
struct Date { int y = 0, m = 0, d = 0; };
Date today();
int days_in_month(int y, int m);
std::string month_name(int m, bool shortname = false);
std::string date_text(const Date &d);          // "Oct 12, 2026"
std::string stamp_now();                        // "2026-10-12 17:30"
std::string add_months_text(int months);        // month/year that many months from now: "Mar 2029"
std::string ordinal(int n);                     // the suffix only: 1 -> "st", 12 -> "th", 22 -> "nd"

// Files and folders
std::string exe_path();    // this program's own file, links followed ("" = unknown)
std::string data_dir();     // where tally.txt lives (TALLY_DATA, portable folder, or the OS default)
std::string documents_dir();
bool make_dirs(const std::string &path);
bool read_file(const std::string &path, std::string &out);
bool write_file_atomic(const std::string &path, const std::string &data, bool private_file);
bool exists(const std::string &path);
std::string join(const std::string &a, const std::string &b);
bool have_program(const std::string &name);
void open_with_system(const std::string &path);   // open a file in the default app

}  // namespace util

namespace util {
// xterm-256 color index -> RGB (for printouts and spreadsheets)
void rgb256(int index, int &r, int &g, int &b);
std::string hex256(int index);   // "1F77B4"
bool is_dark256(int index);      // dark enough that white text reads better
}
