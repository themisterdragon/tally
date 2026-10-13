# Tally

A private, friendly budget planner for the terminal. It's written for first-time budgeters, runs on
Linux, macOS, and Windows, and needs no internet, no account, and nothing else installed.

- **Take-home pay estimate.** Enter your pay the way you know it (salary, hourly, or per paycheck).
  Tally works out federal tax, Social Security, Medicare, state tax (all 50 states + DC), and local
  tax, then shows where each paycheck goes.
- **Every bill on one monthly picture.** Expenses can be weekly, every 2 weeks, twice a month,
  monthly, quarterly, twice a year, or yearly. Each one gets a color-coded spending category.
  Weekly bills show their true monthly average ($200 a week = $866.67, because a year has 52 weeks),
  and Tally shows which months get 5 payments. Due dates are picked on a calendar. Loans from the
  Loans tab show on the list too, and Tally flags a loan that was also entered as an expense.
- **Credit cards, by nickname only.** Mark which bills and subscriptions are charged to each card.
  Type what you paid on the card and that payment covers those bills (never counted twice), with
  anything extra shown as other spending on the card. No card numbers, ever.
- **Charts right in the terminal.** Bars show how each month's pay is split and where the money goes.
  Two-year sparklines show price trends.
- **Your ZIP code, offline.** Median income, rent, and home values for your ZIP, county, and state
  are built into the program.
- **Inflation by category.** Bureau of Labor Statistics price changes for your region show what your
  budget could cost next year.
- **Loans.** See the payoff date and total interest. Press `+`/`-` to see what an extra $25 a month
  saves. Compare snowball and avalanche, and export a full payment schedule.
- **Wants: "is it worth it?"** Type a name and a price. Tally shows the hours of work it costs at
  your *real* hourly pay, and how long it would take to save for it. Planning a trip? Tally adds up
  gas (at your region's average price), car wear, lodging, meals, tickets, and fun, then shows what
  to set aside each month and can add that to your budget.
- **Bills and pay that change.** Press `M` when the electric bill or a paycheck comes in and type
  what it really was this month. The plan stays put, Tally shows the difference, and after a couple
  of months it can use your real average as the plan.
- **What you really bought.** On Prices, press `B` to see this month's purchases next to the plan,
  category by category. Add, edit, or delete any purchase. It's for trends only: purchases never
  change your expenses or totals.
- **Optional server link.** If you run your own server (for example, a phone page for scanning
  barcodes at the store), Tally can bring those purchases in, plus food totals: money lost to expired
  food vs. money saved by freezing. Off unless you turn it on. Want to build your own server? The
  whole contract is in `docs/SERVER-LINK.md`.
- **Checkups and lessons.** Friendly checkups use your own numbers (housing share, 50/30/20,
  emergency fund, high-interest debt). There are 13 short lessons, and `?` explains every screen.
- **Print & export.**
  - A printable PDF with color charts
  - An Excel `.xlsx` workbook with live formulas (opens in LibreOffice, Google Sheets, and Numbers)
  - CSV and plain text

## Privacy

Everything you type stays on your computer, in one plain text file you can open in any editor.
Tally has no account, no analytics, and no ads.

Tally **never** goes online by itself. The only network code is the two optional updates in
Settings (**Update Tally** and **Update tax tables & prices**), plus the server link below. Each
runs only when you choose it:

- It first shows exactly what it will download.
- **Update Tally** asks GitHub for `tally-latest.txt` from this repository's newest release, then
  downloads the program file for your computer, checks its SHA-256 fingerprint and that it runs,
  and only then swaps it in. Your budget and backups aren't touched. If Tally is installed for
  everyone (for example `/usr/local/bin`), it asks for your password with `sudo` to finish.
- It sends the **same fixed request for every user**. Prices for all U.S. regions are requested
  together, so even your region isn't revealed.
- Your ZIP code, income, expenses, loans, and wants are never part of any request.
- It uses the system's `curl` with `-q`, so your personal curl settings can't add anything.

The optional **server link** (Settings → Server link) is off until you turn it on, and it only
connects when you ask (`G` on Prices → Bought). It talks only to a server *you* run, and it only
downloads: each request carries your pairing code and a bookmark number, never your budget. Plain
`http://` is allowed only inside your home network; anywhere else needs `https://`. Purchases can
also come in from a file instead (Settings → Server link → Import purchases from a file).

No internet at all? Tally works fully offline with its built-in data. Newer tax tables and prices
can be loaded from a **data pack file**, for example on a USB stick (Settings → Import a data pack).

## Build

You only need a C++17 compiler.

```sh
sh build.sh            # Linux / macOS -> ./tally
```

Windows (Visual Studio "Developer Command Prompt"): `build.bat`, or with MinGW:

```sh
g++ -std=c++17 -O2 src/*.cpp -o tally.exe -lshell32
```

CMake works too: `cmake -B build && cmake --build build`.

Copy the single `tally` binary anywhere, for example `~/.local/bin`.

**Test bundles for every platform** (built on Linux with [Zig](https://ziglang.org) as a cross-compiler):
`sh scripts/package.sh` makes `dist/Tally-<version>-Windows.zip`, `-macOS.zip` (universal Intel +
Apple Silicon, macOS 11+, with Tally.app), and `-Linux.tar.gz` (static x86_64 and ARM64 builds that
run on any distro). Each bundle has an installer and a READ ME FIRST.

## Use

```
tally                     open Tally
tally --report            print the budget summary as plain text
tally --export pdf        save pdf, xlsx, csv, or txt (to Documents/Tally, or --to DIR)
tally --file PATH         use a different budget file
tally --import-pack PATH  load newer tax tables/prices from a data pack file
tally --get-purchases     download new purchases from your server (if the link is on)
tally --import-purchases PATH   add purchases from a file your server exported
tally --where             show where your files are
```

Inside the app, number keys `1`–`0` (or a mouse click) switch tabs. `?` explains the current
screen, arrows move, Enter changes things, and Esc goes back. Every key is listed along the bottom
of the screen.

On very old terminals or the Linux text console, set Settings → Colors to *Basic* and Drawing to
*Plain ASCII*.

## Where your data lives

| | |
|---|---|
| Linux | `~/.local/share/tally/tally.txt` |
| macOS | `~/Library/Application Support/Tally/tally.txt` |
| Windows | `%APPDATA%\Tally\tally.txt` |

Tally saves a dated backup beside it each day you use it and keeps the last 30. `TALLY_DATA=/some/dir`
overrides the location.

**Portable mode:** put a folder named `tally-data` next to the program (on a USB stick, say) and
Tally keeps everything there.

## Built-in data and sources

| Data | Source |
|---|---|
| ZIP → county, state | U.S. Census Bureau, 2020 ZCTA-to-county relationship file |
| Median income, rent, home value | Census American Community Survey 2020–2024 5-year estimates |
| Federal tax 2026 | IRS Revenue Procedure 2025-32; SSA 2026 wage base |
| State income tax 2026 | Tax Foundation, *State Individual Income Tax Rates and Brackets, 2026* |
| Prices and inflation | U.S. Bureau of Labor Statistics CPI-U and average prices (keyless public endpoint) |

Tax results are **estimates for budgeting, not tax advice**. Known simplifications:

- State tax uses each state's standard deduction and exemptions, but not every credit or phase-out.
- Head-of-household filers use the single brackets for state tax.
- Local income taxes (city or county occupational/earnings taxes) are entered by hand as a percent.

## For maintainers: refreshing the data

`scripts/build_data.py` (Python 3, maintainers only) downloads the public sources above and
regenerates `src/data/places.inc`, `src/data/datapack.inc`, and `data/tally-datapack.txt`. Downloads
are cached in `~/.cache/tally-build`.

`data/tally-datapack.txt` is the file the app's Update downloads, so publishing a new one updates
everyone's tax tables without a new release.

For a new version of the program: set `TALLY_VERSION` in `src/version.h`, run `sh scripts/package.sh`
(and `sh scripts/deb.sh`), and attach the installers **plus every file in `dist/update-<version>/`**
to a GitHub release tagged `v<version>`. Settings → Update Tally reads `tally-latest.txt` from the
newest release, so a release without it can't be installed from inside Tally.

When a new tax year comes out, update `FEDERAL` and `TAX_YEAR` in the script and the Tax Foundation
link in `SOURCES`, then rebuild.

## License

GPL-3.0. See `LICENSE`.
