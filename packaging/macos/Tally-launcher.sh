#!/bin/sh
# Tally.app opens the real program in a Terminal window.
exec open -a Terminal "$(cd "$(dirname "$0")/../Resources" && pwd)/tally"
