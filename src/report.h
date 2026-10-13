// Printouts and spreadsheets: PDF (with color charts), Excel .xlsx (with live formulas),
// CSV, and plain text. All written by Tally itself; no other programs needed.
#pragma once
#include <string>

#include "model.h"

namespace report {

std::string export_folder(const Budget &b);   // Settings folder, or Documents/Tally

// Each returns the path written, or "" on failure (error explains why).
std::string pdf(const Budget &b, const std::string &folder, std::string &error);
std::string xlsx(const Budget &b, const std::string &folder, std::string &error);
std::string csv(const Budget &b, const std::string &folder, std::string &error);
std::string txt(const Budget &b, const std::string &folder, std::string &error);
std::string loan_schedule_csv(const Budget &b, size_t loan, const std::string &folder, std::string &error);

std::string text_report(const Budget &b, int width = 78);   // also used by `tally --report`

bool print_file(const std::string &path, std::string &message);  // send to the printer if there's a way

}  // namespace report
