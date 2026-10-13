#!/bin/sh
# Build Tally with no build system: just a C++17 compiler.
#   sh build.sh            -> ./tally
# Works with g++ or clang++ on Linux and macOS. On Windows use build.bat (MSVC) or
# MinGW:  g++ -std=c++17 -O2 src/*.cpp -o tally.exe -lshell32
set -e
cd "$(dirname "$0")"
CXX=${CXX:-$(command -v c++ || command -v g++ || command -v clang++)}
[ -n "$CXX" ] || { echo "No C++ compiler found (install g++ or clang++)."; exit 1; }
echo "Building Tally with $CXX ..."
$CXX -std=c++17 -O2 -Wall -Wextra -o tally src/*.cpp
echo "Done: ./tally   (copy it anywhere, e.g. ~/.local/bin)"
