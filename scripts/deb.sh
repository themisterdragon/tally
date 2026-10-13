#!/bin/sh
# Ubuntu / Debian packages for Tally (amd64 and arm64), built on any Linux with Zig.
#   sh scripts/deb.sh   -> dist/tally_<ver>_<arch>.deb
# Install on Ubuntu:  sudo apt install ./tally_1.2.1_amd64.deb   (or double-click it)
# A .deb is an "ar" archive of three members; no dpkg needed to make one.
set -e
cd "$(dirname "$0")/.."
ZIG=${ZIG:-$(command -v zig || echo "$HOME/.cache/tally-build/zig/zig")}
[ -x "$ZIG" ] || { echo "Zig not found. Set ZIG=/path/to/zig"; exit 1; }
TV=$(sed -n 's/#define TALLY_VERSION "\(.*\)"/\1/p' src/main.cpp)
OUT=dist; WORK=dist/debwork
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"

deb() {  # name version arch dir
    name=$1; ver=$2; arch=$3; root=$4
    size=$(du -sk "$root/usr" | cut -f1)
    sed -i "s/^Installed-Size:.*/Installed-Size: $size/" "$root/DEBIAN/control"
    ( cd "$root" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums )
    tmp="$WORK/$name-$arch-parts"; rm -rf "$tmp"; mkdir -p "$tmp"
    printf '2.0\n' > "$tmp/debian-binary"
    tar --owner=0 --group=0 --numeric-owner -czf "$tmp/control.tar.gz" -C "$root/DEBIAN" .
    tar --owner=0 --group=0 --numeric-owner -czf "$tmp/data.tar.gz" -C "$root" ./usr
    rm -f "$OUT/${name}_${ver}_${arch}.deb"
    ( cd "$tmp" && ar rc "../../${name}_${ver}_${arch}.deb" debian-binary control.tar.gz data.tar.gz )
    echo "  $OUT/${name}_${ver}_${arch}.deb"
}

for arch in amd64 arm64; do
    case $arch in amd64) cpu=x86_64 ;; arm64) cpu=aarch64 ;; esac

    # --- Tally (terminal app, fully static) ---
    R="$WORK/tally-$arch"; mkdir -p "$R/DEBIAN" "$R/usr/bin" "$R/usr/share/applications" \
        "$R/usr/share/icons/hicolor/scalable/apps" "$R/usr/share/doc/tally"
    "$ZIG" c++ -target $cpu-linux-musl -std=c++17 -O2 -DNDEBUG -s -static -o "$R/usr/bin/tally" src/*.cpp
    cp packaging/linux/icons/tally.svg "$R/usr/share/icons/hicolor/scalable/apps/tally.svg"
    cat > "$R/usr/share/applications/tally.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Tally
Comment=A private budget planner
Exec=tally
Icon=tally
Terminal=true
Categories=Office;Finance;
DESK
    printf 'Tally %s\nCopyright: Tally contributors\nLicense: GPL-3.0-or-later\nFull text: /usr/share/common-licenses/GPL-3\n' "$TV" > "$R/usr/share/doc/tally/copyright"
    cat > "$R/DEBIAN/control" <<CTL
Package: tally
Version: $TV
Architecture: $arch
Maintainer: themisterdragon <274141300+themisterdragon@users.noreply.github.com>
Installed-Size: 0
Section: utils
Priority: optional
Recommends: curl
Description: private, offline-first budget planner for the terminal
 Take-home pay estimates, every bill on one monthly picture, loans, wants,
 inflation by category, and printable reports. Your budget stays on your
 computer. Optional: bring in purchases from a server you run yourself.
CTL
    deb tally "$TV" $arch "$R"

done
rm -rf "$WORK"
