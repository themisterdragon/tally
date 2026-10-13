// Tally: a private, offline-first household budget planner for the terminal.
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "app.h"
#include "budget.h"
#include "datapack.h"
#include "model.h"
#include "report.h"
#include "server.h"
#include "update.h"
#include "util.h"

#define TALLY_VERSION "1.2.1"

static void usage() {
    std::printf(
        "Tally %s - a private budget planner\n\n"
        "Usage:\n"
        "  tally                     open Tally\n"
        "  tally --report            print your budget summary as plain text\n"
        "  tally --export FORMAT     save pdf, xlsx, csv, or txt (to Documents/Tally or --to DIR)\n"
        "  tally --file PATH         use a different budget file\n"
        "  tally --import-pack PATH  load newer tax tables/prices from a data pack file\n"
        "  tally --get-purchases     download new purchases from your server (if the link is on)\n"
        "  tally --import-purchases PATH   add purchases from a file your server exported\n"
        "  tally --where             show where your budget file and backups are\n"
        "  tally --version\n\n"
        "Your budget never leaves this computer. Tally only goes online when you choose\n"
        "Update in Settings (or uses the optional server link you set up), and even\n"
        "then it never sends your budget.\n",
        TALLY_VERSION);
}

static bool load(const std::string &path, Budget &b) {
    std::string text, err;
    if (!util::read_file(path, text)) { b.set_defaults(); return true; }
    if (!b.parse(text, err)) { std::fprintf(stderr, "tally: couldn't read %s: %s\n", path.c_str(), err.c_str()); return false; }
    budget::normalize(b);
    return true;
}

int main(int argc, char **argv) {
    std::string file = util::join(util::data_dir(), "tally.txt"), export_fmt, to, import, import_purchases;
    bool report_only = false, get_purchases = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](std::string &out) {
            if (i + 1 >= argc) { std::fprintf(stderr, "tally: %s needs a value\n", a.c_str()); std::exit(2); }
            out = argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "-v" || a == "--version") { std::printf("Tally %s\n", TALLY_VERSION); return 0; }
        else if (a == "--report") report_only = true;
        else if (a == "--export") next(export_fmt);
        else if (a == "--to") next(to);
        else if (a == "--file") next(file);
        else if (a == "--import-pack") next(import);
        else if (a == "--get-purchases") get_purchases = true;
        else if (a == "--import-purchases") next(import_purchases);
        else if (a == "--where") {
            std::printf("Budget file: %s\nBackups:     %s\nExports:     %s\n", file.c_str(),
                        util::join(util::data_dir(), "backups").c_str(), util::join(util::documents_dir(), "Tally").c_str());
            return 0;
        } else { std::fprintf(stderr, "tally: unknown option %s (try --help)\n", a.c_str()); return 2; }
    }
    if (!import.empty()) {
        util::make_dirs(util::data_dir());
        auto r = update::import_pack(import);
        std::printf("%s\n", r.message.c_str());
        return r.ok ? 0 : 1;
    }
    if (get_purchases || !import_purchases.empty()) {
        Budget b;
        if (!load(file, b)) return 1;
        auto r = get_purchases ? server::pull(b) : server::import_file(b, import_purchases);
        util::make_dirs(util::data_dir());
        if (r.added || get_purchases) util::write_file_atomic(file, b.serialize(), true);
        std::printf("%s\n", r.message.c_str());
        return r.ok ? 0 : 1;
    }
    if (report_only || !export_fmt.empty()) {
        Budget b;
        if (!load(file, b)) return 1;
        if (report_only) { std::fputs(report::text_report(b).c_str(), stdout); return 0; }
        std::string folder = to.empty() ? report::export_folder(b) : to, err, out;
        if (export_fmt == "pdf") out = report::pdf(b, folder, err);
        else if (export_fmt == "xlsx") out = report::xlsx(b, folder, err);
        else if (export_fmt == "csv") out = report::csv(b, folder, err);
        else if (export_fmt == "txt") out = report::txt(b, folder, err);
        else { std::fprintf(stderr, "tally: format must be pdf, xlsx, csv, or txt\n"); return 2; }
        if (out.empty()) { std::fprintf(stderr, "tally: %s\n", err.c_str()); return 1; }
        std::printf("%s\n", out.c_str());
        return 0;
    }
    int rc = run_app(file);
    if (rc == 1) {
        std::fprintf(stderr, "Tally needs an interactive terminal. Try: tally --report\n");
    }
    return rc;
}
