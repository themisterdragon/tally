#!/bin/sh
# Install Tally on a Mac: Tally.app into Applications, and the "tally" command for Terminal.
set -e
here=$(cd "$(dirname "$0")" && pwd)
# Downloaded files are flagged by macOS; clear the flag on Tally's own files so they can run.
xattr -dr com.apple.quarantine "$here" 2>/dev/null || true

if [ -w /Applications ]; then apps=/Applications; else apps="$HOME/Applications"; mkdir -p "$apps"; fi
rm -rf "$apps/Tally.app"
cp -R "$here/Tally.app" "$apps/"
echo "Installed $apps/Tally.app"

# The command: /usr/local/bin if possible (it's on every Mac's PATH), otherwise ~/.local/bin
if [ -d /usr/local/bin ] && [ -w /usr/local/bin ]; then
    cp "$here/tally" /usr/local/bin/tally
    echo "Installed the command: /usr/local/bin/tally"
else
    echo "To type 'tally' in Terminal, macOS needs your password once (or press Control-C to skip)."
    if sudo mkdir -p /usr/local/bin && sudo cp "$here/tally" /usr/local/bin/tally; then
        echo "Installed the command: /usr/local/bin/tally"
    else
        echo "Skipped. Tally.app still works."
    fi
fi
echo ""
echo "Done! Open Tally from Applications (or Launchpad), or type  tally  in Terminal."
