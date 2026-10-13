#include "progupdate.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "net.h"
#include "util.h"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace progupdate {

const char *RELEASES = "https://github.com/themisterdragon/tally/releases";

// For testing a release before it's the newest: TALLY_UPDATE_BASE=https://github.com/<you>/tally/releases
// with TALLY_UPDATE_TAG=v1.2.3. Still https only, still checked like any address.
static std::string base() {
    const char *b = std::getenv("TALLY_UPDATE_BASE");
    return b && *b ? b : RELEASES;
}
static std::string manifest_url() {
    const char *t = std::getenv("TALLY_UPDATE_TAG");
    if (t && *t) return base() + "/download/" + t + "/tally-latest.txt";
    return base() + "/latest/download/tally-latest.txt";
}

std::string platform() {
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    return "windows-x86_64";
#elif defined(__APPLE__)
    return "macos";          // one universal file for Intel and Apple Silicon
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "";
#endif
}

static std::vector<int> parts(const std::string &v) {
    std::vector<int> p;
    for (const auto &x : util::split(v, '.')) p.push_back(std::atoi(x.c_str()));
    while (p.size() < 3) p.push_back(0);
    return p;
}

bool newer(const std::string &a, const std::string &b) {
    auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}

// --- SHA-256 (FIPS 180-4), so a download is checked with nothing else installed ---------------

std::string sha256_hex(const std::string &data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string m = data;
    uint64_t bits = (uint64_t)data.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; i--) m += (char)((bits >> (i * 8)) & 0xff);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)(unsigned char)m[off + i * 4] << 24 | (uint32_t)(unsigned char)m[off + i * 4 + 1] << 16 |
                   (uint32_t)(unsigned char)m[off + i * 4 + 2] << 8 | (uint32_t)(unsigned char)m[off + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

// --- Asking and downloading ---------------------------------------------------------------------

static bool get(const std::string &url, std::string &out) {
    if (!net::safe_url(url) || !util::starts_with(url, "https://")) return false;
    // https only, even across GitHub's redirect to its download server
    return net::run_curl("--proto =https --proto-redir =https " + net::quote_arg(url), out);
}

std::string describe() {
    return "Update Tally asks GitHub for the newest version:\n  " + manifest_url() +
           "\n\nIf there's a newer one, it downloads the program file for this computer (" + platform() +
           ") from the same place, checks its SHA-256 fingerprint and that it runs, and only then swaps it in.\n\n"
           "Sent: nothing but those two requests, the same from every Tally.\n"
           "Not sent: your budget, ZIP, income, or anything you typed. Your budget and backups aren't touched.";
}

Latest check() {
    Latest l;
    if (platform().empty()) { l.message = "There's no ready-made Tally for this kind of computer. Build it from source instead."; return l; }
    if (!net::curl_available()) { l.message = "curl isn't installed, so Tally can't download. Download the new version from GitHub instead."; return l; }
    std::string text;
    if (!get(manifest_url(), text)) { l.message = "Couldn't reach GitHub (no internet?). Nothing changed."; return l; }
    // "version 1.2.3" / "tag v1.2.3" / "file linux-x86_64 tally-linux-x86_64 <sha256>"
    for (const auto &raw : util::split(text, '\n')) {
        auto w = util::split(util::trim(raw), ' ');
        if (w.size() == 2 && w[0] == "version") l.version = w[1];
        else if (w.size() == 2 && w[0] == "tag") l.tag = w[1];
        else if (w.size() == 4 && w[0] == "file" && w[1] == platform()) { l.file = w[2]; l.sha256 = w[3]; }
    }
    if (l.version.empty() || l.tag.empty()) { l.message = "GitHub's answer didn't look like a Tally release list. Nothing changed."; return l; }
    if (l.file.empty() || l.sha256.size() != 64) { l.message = "The newest release has no program file for this computer (" + platform() + ")."; return l; }
    if (!net::safe_url(base() + "/download/" + l.tag + "/" + l.file)) { l.message = "The release list has an odd file name. Nothing changed."; return l; }
    l.ok = true;
    return l;
}

static std::string run_version(const std::string &file) {
    std::string out;
#ifdef _WIN32
    FILE *p = _popen(("\"" + file + "\" --version").c_str(), "rb");
#else
    FILE *p = popen((net::quote_arg(file) + " --version 2>/dev/null").c_str(), "r");
#endif
    if (!p) return "";
    char buf[256];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return util::trim(out);
}

static bool writable_dir(const std::string &dir) {
#ifdef _WIN32
    std::string probe = util::join(dir, ".tally-write-test");
    FILE *f = std::fopen(probe.c_str(), "wb");
    if (!f) return false;
    std::fclose(f);
    std::remove(probe.c_str());
    return true;
#else
    return access(dir.c_str(), W_OK) == 0;
#endif
}

Installed download_and_install(const Latest &l) {
    Installed r;
    std::string exe = util::exe_path();
    if (exe.empty()) { r.message = "Tally couldn't tell where it's installed. Download the new version from GitHub instead."; return r; }
    r.target = exe;
    std::string data;
    if (!get(base() + "/download/" + l.tag + "/" + l.file, data) || data.size() < 10000) {
        r.message = "The download didn't finish (no internet?). Nothing changed.";
        return r;
    }
    if (sha256_hex(data) != l.sha256) {
        r.message = "The download's fingerprint didn't match, so it was thrown away. Nothing changed.";
        return r;
    }
    std::string dir = fs::path(exe).parent_path().string();
    bool in_place = writable_dir(dir);
    // beside the program when we can replace it ourselves (so the swap is one rename), otherwise
    // in Tally's data folder for sudo to copy from
    std::string tmp_dir = in_place ? dir : util::data_dir();
    util::make_dirs(tmp_dir);
#ifdef _WIN32
    std::string fresh = util::join(tmp_dir, "tally-new.exe");
#else
    std::string fresh = util::join(tmp_dir, ".tally-new");
#endif
    if (!util::write_file_atomic(fresh, data, false)) { r.message = "Couldn't save the download. Nothing changed."; return r; }
#ifndef _WIN32
    chmod(fresh.c_str(), 0755);
#endif
    std::string said = run_version(fresh);
    if (said != "Tally " + l.version) {
        std::remove(fresh.c_str());
        r.message = "The new program didn't start correctly on this computer, so it wasn't installed. Nothing changed.";
        return r;
    }
    if (!in_place) {
        r.kind = Installed::NEEDS_ADMIN;
        r.new_file = fresh;
        r.message = "Tally is installed in " + dir + ", which needs your password to change.";
        return r;
    }
#ifdef _WIN32
    // Windows won't overwrite a running program, but it will rename it out of the way.
    std::string old = exe + ".old";
    std::remove(old.c_str());
    if (!MoveFileExA(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::remove(fresh.c_str()); r.message = "Windows wouldn't let Tally replace itself. Nothing changed."; return r; }
    if (!MoveFileExA(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExA(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        r.message = "Windows wouldn't let Tally replace itself. Nothing changed.";
        return r;
    }
#else
    if (std::rename(fresh.c_str(), exe.c_str()) != 0) { std::remove(fresh.c_str()); r.message = "Couldn't replace the program. Nothing changed."; return r; }
#endif
    r.kind = Installed::DONE;
    r.message = "Updated to Tally " + l.version + ".";
    return r;
}

bool install_with_sudo(const Installed &i) {
#ifdef _WIN32
    (void)i;
    return false;
#else
    if (!util::have_program("sudo")) return false;
    std::string cmd = "sudo install -m 755 " + net::quote_arg(i.new_file) + " " + net::quote_arg(i.target);
    int rc = std::system(cmd.c_str());
    std::remove(i.new_file.c_str());
    return rc == 0;
#endif
}

void cleanup_old() {
#ifdef _WIN32
    std::string exe = util::exe_path();
    if (!exe.empty()) std::remove((exe + ".old").c_str());
#endif
}

}  // namespace progupdate
