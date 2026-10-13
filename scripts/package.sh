#!/bin/sh
# Build test bundles for Windows, macOS, and Linux from one Linux machine, using Zig as
# a cross-compiler (no Windows or Mac needed).
#   ZIG=/path/to/zig sh scripts/package.sh      -> dist/Tally-<version>-{Windows.zip,macOS.zip,Linux.tar.gz}
# llvm-lipo (from LLVM) makes the universal Mac binary; zip and tar make the archives.
set -e
cd "$(dirname "$0")/.."
ZIG=${ZIG:-$(command -v zig || echo "$HOME/.cache/tally-build/zig/zig")}
[ -x "$ZIG" ] || { echo "Zig not found. Get it from ziglang.org and set ZIG=/path/to/zig"; exit 1; }
VERSION=$(sed -n 's/#define TALLY_VERSION "\(.*\)"/\1/p' src/version.h)
OUT=dist; WORK=dist/work
rm -rf "$WORK"; mkdir -p "$WORK"

build() {  # target output [extra flags]
    echo "  building $1"
    "$ZIG" c++ -target "$1" -std=c++17 -O2 -DNDEBUG -s -o "$2" src/*.cpp $3
}
echo "Tally $VERSION"
build x86_64-windows-gnu "$WORK/tally.exe" -lshell32
build x86_64-linux-musl "$WORK/tally-x86_64" -static
build aarch64-linux-musl "$WORK/tally-arm64" -static
build x86_64-macos.11.0 "$WORK/tally-macos-x86_64"
build aarch64-macos.11.0 "$WORK/tally-macos-arm64"

readme() {  # file platform-specific-text
    { printf 'TALLY %s - a private budget planner\r\n\r\n' "$VERSION"; printf '%b' "$2"
      printf '\r\nYour budget stays on your computer. Tally only uses the internet if you open\r\n'
      printf 'Settings > Update and choose to download, and it never sends anything you typed.\r\n\r\n'
      printf 'Press ? on any screen for help. Number keys switch tabs.\r\n'
      printf 'Tax figures are estimates for budgeting, not tax advice.\r\n'; } > "$1"
}

# --- Windows ---
W="$WORK/Tally-$VERSION-Windows"; mkdir -p "$W"
cp "$WORK/tally.exe" packaging/windows/install.ps1 packaging/windows/uninstall.ps1 "$W/"
cp "packaging/windows/Install Tally.bat" "packaging/windows/Uninstall Tally.bat" "$W/"
readme "$W/READ ME FIRST.txt" 'INSTALL: double-click "Install Tally.bat".\r\n  Windows may say "Windows protected your PC" because Tally is not signed yet:\r\n  click "More info", then "Run anyway".\r\n  Then open Tally from the Start Menu or Desktop.\r\n\r\nNO INSTALL: just double-click tally.exe.\r\n\r\nNeeds Windows 10 or 11. Windows Terminal gives the best colors.\r\n'

# --- macOS ---
M="$WORK/Tally-$VERSION-macOS"; mkdir -p "$M/Tally.app/Contents/MacOS" "$M/Tally.app/Contents/Resources"
llvm-lipo -create -output "$M/tally" "$WORK/tally-macos-x86_64" "$WORK/tally-macos-arm64"
chmod 755 "$M/tally"
cp "$M/tally" "$M/Tally.app/Contents/Resources/tally"
install -m 755 packaging/macos/Tally-launcher.sh "$M/Tally.app/Contents/MacOS/Tally"
sed "s/VERSION/$VERSION/g" packaging/macos/Info.plist > "$M/Tally.app/Contents/Info.plist"
install -m 755 "packaging/macos/Install Tally.command" "packaging/macos/Uninstall Tally.command" "$M/"
readme "$M/READ ME FIRST.txt" 'INSTALL: right-click "Install Tally.command" and choose Open, then Open again.\n  (macOS asks because Tally is not signed by Apple yet. A plain double-click\n  may only say it "cannot be opened": right-click > Open is the way past that.)\n  Or in Terminal:  sh "Install Tally.command"\n\nThen open Tally from Applications, or type  tally  in Terminal.\n\nNO INSTALL: in Terminal, run ./tally from this folder.\n\nWorks on Intel and Apple Silicon Macs, macOS 11 or newer.\n'
sed -i 's/\r//' "$M/READ ME FIRST.txt"

# --- Linux ---
L="$WORK/Tally-$VERSION-Linux"; mkdir -p "$L"
install -m 755 "$WORK/tally-x86_64" "$WORK/tally-arm64" packaging/linux/install.sh "$L/"
readme "$L/READ ME FIRST.txt" 'INSTALL:  sh install.sh\n  (for everyone on this computer:  sh install.sh --system)\n  Then type  tally  in a terminal or find Tally in your app menu.\n\nNO INSTALL: run ./tally-x86_64 (or ./tally-arm64 on ARM, e.g. Raspberry Pi).\n\nWorks on any Linux distro: the program is one self-contained file.\nOn the Linux text console, set Settings > Colors to Basic and Drawing to Plain ASCII.\n\nUNINSTALL:  sh install.sh --uninstall\n'
sed -i 's/\r//' "$L/READ ME FIRST.txt"

# --- Update Tally: the plain program files plus the list Settings > Update Tally reads ---
# Attach everything in dist/update-<version>/ to the GitHub release.
U="$OUT/update-$VERSION"; rm -rf "$U"; mkdir -p "$U"
install -m 755 "$WORK/tally-x86_64" "$U/tally-linux-x86_64"
install -m 755 "$WORK/tally-arm64" "$U/tally-linux-arm64"
install -m 755 "$M/tally" "$U/tally-macos"
install -m 755 "$WORK/tally.exe" "$U/tally-windows-x86_64.exe"
{ echo "# Tally release list for Settings > Update Tally"
  echo "version $VERSION"
  echo "tag v$VERSION"
  for pair in linux-x86_64:tally-linux-x86_64 linux-arm64:tally-linux-arm64 macos:tally-macos windows-x86_64:tally-windows-x86_64.exe; do
      key=${pair%%:*}; file=${pair#*:}
      echo "file $key $file $(sha256sum "$U/$file" | cut -d' ' -f1)"
  done; } > "$U/tally-latest.txt"

mkdir -p "$OUT"
( cd "$WORK" && rm -f "../Tally-$VERSION-Windows.zip" "../Tally-$VERSION-macOS.zip" "../Tally-$VERSION-Linux.tar.gz" \
  && zip -qr "../Tally-$VERSION-Windows.zip" "Tally-$VERSION-Windows" \
  && zip -qry "../Tally-$VERSION-macOS.zip" "Tally-$VERSION-macOS" \
  && tar -czf "../Tally-$VERSION-Linux.tar.gz" "Tally-$VERSION-Linux" )
rm -rf "$WORK"
ls -la "$OUT"/Tally-"$VERSION"-* "$U"
