#!/bin/sh
rm -rf /Applications/Tally.app "$HOME/Applications/Tally.app"
[ -f /usr/local/bin/tally ] && { rm -f /usr/local/bin/tally 2>/dev/null || sudo rm -f /usr/local/bin/tally; }
echo "Tally is uninstalled. Your budget is still in ~/Library/Application Support/Tally"
