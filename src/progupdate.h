// Settings > Update Tally: replaces this program with the newest release, only when you choose it.
// One fixed request asks what the latest version is (the same request from every Tally), then the
// program file for this computer is downloaded, checked against its SHA-256 fingerprint, test-run,
// and swapped in. Your budget and its backups are never touched.
#pragma once
#include <string>

namespace progupdate {

extern const char *RELEASES;    // https://github.com/themisterdragon/tally/releases

std::string platform();          // "linux-x86_64", "linux-arm64", "windows-x86_64", "macos" ("" = no ready-made build)
bool newer(const std::string &a, const std::string &b);   // is version a newer than b? ("1.10.0" > "1.9.3")
std::string sha256_hex(const std::string &data);

struct Latest {
    bool ok = false;
    std::string message;         // why not, when !ok
    std::string version, tag;    // "1.2.3", "v1.2.3"
    std::string file, sha256;    // the program file for this computer and its fingerprint
};
Latest check();                  // fetch the release list (tally-latest.txt)
std::string describe();          // what check() and download() send, shown before anything happens

struct Installed {
    enum Kind { FAILED, DONE, NEEDS_ADMIN } kind = FAILED;
    std::string message;
    std::string new_file;        // NEEDS_ADMIN: the checked download, ready to copy into place
    std::string target;          // NEEDS_ADMIN / DONE: where Tally lives
};
// Download, check the fingerprint, make sure it runs and says the right version, then swap it in.
Installed download_and_install(const Latest &l);
// NEEDS_ADMIN on Linux/macOS: copy the checked file into place with sudo (asks for your password
// in the terminal). Returns true if it worked.
bool install_with_sudo(const Installed &i);
void cleanup_old();              // Windows: remove the previous program left beside the new one

}  // namespace progupdate
