#!/bin/sh
# Install Tally on Linux (any distro: the program is a single static file).
#   sh install.sh            install for you (~/.local/bin), no password needed
#   sh install.sh --system   install for everyone (/usr/local/bin), asks for your password
#   sh install.sh --uninstall
set -e
here=$(cd "$(dirname "$0")" && pwd)
case "$(uname -m)" in
    x86_64|amd64) bin="$here/tally-x86_64" ;;
    aarch64|arm64) bin="$here/tally-arm64" ;;
    *) echo "Sorry, there's no ready-made Tally for $(uname -m). Build it from source: sh build.sh"; exit 1 ;;
esac

dest="$HOME/.local/bin"; apps="$HOME/.local/share/applications"; sudo=""
[ "$1" = "--system" ] && { dest=/usr/local/bin; apps=/usr/local/share/applications; sudo=sudo; }

if [ "$1" = "--uninstall" ]; then
    rm -f "$HOME/.local/bin/tally" "$HOME/.local/share/applications/Tally.desktop"
    [ -f /usr/local/bin/tally ] && sudo rm -f /usr/local/bin/tally /usr/local/share/applications/Tally.desktop
    echo "Tally is uninstalled. Your budget is still in ~/.local/share/tally (delete it yourself if you want)."
    exit 0
fi

$sudo mkdir -p "$dest" "$apps"
$sudo install -m 755 "$bin" "$dest/tally"
# App-menu entry: opens Tally in your default terminal
tmp=$(mktemp)
cat > "$tmp" <<DESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=Tally
GenericName=Budget Planner
Comment=Private budget planner (runs in a terminal)
Exec=$dest/tally
Terminal=true
Icon=accessories-calculator
Categories=Office;Finance;
DESKTOP
$sudo install -m 644 "$tmp" "$apps/Tally.desktop"
rm -f "$tmp"
command -v update-desktop-database >/dev/null && $sudo update-desktop-database "$apps" 2>/dev/null || true

echo "Tally is installed: $dest/tally"
case ":$PATH:" in
    *":$dest:"*) echo "Type  tally  in a terminal, or find Tally in your app menu." ;;
    *) echo "Find Tally in your app menu, or run $dest/tally"
       echo "(To type just 'tally', add this line to ~/.profile:  export PATH=\"$dest:\$PATH\")" ;;
esac
