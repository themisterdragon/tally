#!/usr/bin/env python3
"""Maintainer tool: rebuild Tally's built-in data (src/data/*.inc).

Tally itself never needs this script or Python. It only runs when a maintainer
refreshes the offline data for a new year. Everything it downloads is public,
key-free government data or a published table:

  * Census 2020 ZCTA-to-county relationship file (which county each ZIP is in)
  * Census ACS 5-year table-based summary files (median income, rent, home value)
  * Tax Foundation "State Individual Income Tax Rates and Brackets, 2026"
  * BLS consumer prices (CPI + average prices), all regions at once

Downloads are cached in ~/.cache/tally-build so a rebuild works offline.
Run:  python3 -I scripts/build_data.py [--no-bls]
"""
import json, os, re, subprocess, sys, time, zipfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "data")
CACHE = os.path.expanduser("~/.cache/tally-build")
ACS_YEAR = 2024
TAX_YEAR = 2026

SOURCES = {
    "zcta_county.txt": "https://www2.census.gov/geo/docs/maps-data/data/rel2020/zcta520/tab20_zcta520_county20_natl.txt",
    "acs_%d_b19013.dat" % ACS_YEAR: "https://www2.census.gov/programs-surveys/acs/summary_file/%d/table-based-SF/data/5YRData/acsdt5y%d-b19013.dat" % (ACS_YEAR, ACS_YEAR),
    "acs_%d_b25064.dat" % ACS_YEAR: "https://www2.census.gov/programs-surveys/acs/summary_file/%d/table-based-SF/data/5YRData/acsdt5y%d-b25064.dat" % (ACS_YEAR, ACS_YEAR),
    "acs_%d_b25077.dat" % ACS_YEAR: "https://www2.census.gov/programs-surveys/acs/summary_file/%d/table-based-SF/data/5YRData/acsdt5y%d-b25077.dat" % (ACS_YEAR, ACS_YEAR),
    "state_tax_%d.xlsx" % TAX_YEAR: "https://taxfoundation.org/wp-content/uploads/2026/02/2026-State-Individual-Income-Tax-Rates-Brackets.xlsx",
}

# FIPS, postal code, name, BLS/Census region (1 Northeast, 2 Midwest, 3 South, 4 West)
STATES = [
    ("01","AL","Alabama",3),("02","AK","Alaska",4),("04","AZ","Arizona",4),("05","AR","Arkansas",3),
    ("06","CA","California",4),("08","CO","Colorado",4),("09","CT","Connecticut",1),("10","DE","Delaware",3),
    ("11","DC","District of Columbia",3),("12","FL","Florida",3),("13","GA","Georgia",3),("15","HI","Hawaii",4),
    ("16","ID","Idaho",4),("17","IL","Illinois",2),("18","IN","Indiana",2),("19","IA","Iowa",2),
    ("20","KS","Kansas",2),("21","KY","Kentucky",3),("22","LA","Louisiana",3),("23","ME","Maine",1),
    ("24","MD","Maryland",3),("25","MA","Massachusetts",1),("26","MI","Michigan",2),("27","MN","Minnesota",2),
    ("28","MS","Mississippi",3),("29","MO","Missouri",2),("30","MT","Montana",4),("31","NE","Nebraska",2),
    ("32","NV","Nevada",4),("33","NH","New Hampshire",1),("34","NJ","New Jersey",1),("35","NM","New Mexico",4),
    ("36","NY","New York",1),("37","NC","North Carolina",3),("38","ND","North Dakota",2),("39","OH","Ohio",2),
    ("40","OK","Oklahoma",3),("41","OR","Oregon",4),("42","PA","Pennsylvania",1),("44","RI","Rhode Island",1),
    ("45","SC","South Carolina",3),("46","SD","South Dakota",2),("47","TN","Tennessee",3),("48","TX","Texas",3),
    ("49","UT","Utah",4),("50","VT","Vermont",1),("51","VA","Virginia",3),("53","WA","Washington",4),
    ("54","WV","West Virginia",3),("55","WI","Wisconsin",2),("56","WY","Wyoming",4),
]
FIPS = {f: i for i, (f, *_rest) in enumerate(STATES)}

# Tax Foundation row labels -> postal code
TF_NAMES = {"Ala.":"AL","Alaska":"AK","Ariz.":"AZ","Ark.":"AR","Calif.":"CA","Colo.":"CO","Conn.":"CT","Del.":"DE",
    "Fla.":"FL","Ga.":"GA","Hawaii":"HI","Idaho":"ID","Ill.":"IL","Ind.":"IN","Iowa":"IA","Kans.":"KS","Ky.":"KY",
    "La.":"LA","Maine":"ME","Md.":"MD","Mass.":"MA","Mich.":"MI","Minn.":"MN","Miss.":"MS","Mo.":"MO","Mont.":"MT",
    "Nebr.":"NE","Nev.":"NV","N.H.":"NH","N.J.":"NJ","N.M.":"NM","N.Y.":"NY","N.C.":"NC","N.D.":"ND","Ohio":"OH",
    "Okla.":"OK","Ore.":"OR","Pa.":"PA","R.I.":"RI","S.C.":"SC","S.D.":"SD","Tenn.":"TN","Tex.":"TX","Utah":"UT",
    "Vt.":"VT","Va.":"VA","Wash.":"WA","W.Va.":"WV","Wis.":"WI","Wyo.":"WY","D.C.":"DC"}
WAGE_TAX_FREE = {"WA"}  # Washington's listed tax is on capital gains only

# Employee-paid state payroll programs named in the Tax Foundation footnotes (percent of wages)
STATE_PAYROLL = {"CA": 1.3, "MA": 0.46, "WA": 0.58}

# Federal 2026 (IRS Rev. Proc. 2025-32; SSA 2026 wage base). Brackets are "over:rate".
FEDERAL = [
    "fed std single 16100 joint 32200 hoh 24150 separate 16100",
    "fed brackets single 0:0.10 12400:0.12 50400:0.22 105700:0.24 201775:0.32 256225:0.35 640600:0.37",
    "fed brackets joint 0:0.10 24800:0.12 100800:0.22 211400:0.24 403550:0.32 512450:0.35 768700:0.37",
    "fed brackets hoh 0:0.10 17700:0.12 67450:0.22 105700:0.24 201750:0.32 256200:0.35 640600:0.37",
    "fed brackets separate 0:0.10 12400:0.12 50400:0.22 105700:0.24 201775:0.32 256225:0.35 384350:0.37",
    "fed payroll ss_rate 0.062 ss_base 184500 med_rate 0.0145 addl_rate 0.009 addl_single 200000 addl_joint 250000 addl_hoh 200000 addl_separate 125000",
    "fed credits ctc 2200 ctc_refundable 1700 odc 500 phase_single 200000 phase_joint 400000",
]

# BLS: the same fixed request goes out for every user (all regions), so it reveals nothing.
CPI_ITEMS = ["SA0","SAF11","SEFV","SAH1","SAH2","SETB01","SAT","SAM","SAA","SAE","SAR","SEHF01"]
AP_REGIONAL = ["74714","72610","72620"]                  # gas gallon, electricity kWh, utility gas therm
AP_NATIONAL = ["708111","709112","702111","703112","706111"]  # eggs, milk, bread, ground beef, chicken
AREAS = ["0000","0100","0200","0300","0400"]


def fetch(name):
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        print("downloading", SOURCES[name])
        subprocess.run(["curl", "-sSfL", "--http1.1", "-m", "600", "-A", "Mozilla/5.0", "-o", path, SOURCES[name]], check=True)
    return path


def bls_series():
    ids = []
    for a in AREAS:
        ids += ["CUUR%s%s" % (a, i) for i in CPI_ITEMS]
        ids += ["APU%s%s" % (a, i) for i in AP_REGIONAL]
    ids += ["APU0000%s" % i for i in AP_NATIONAL]
    return ids


def fetch_bls():
    path = os.path.join(CACHE, "bls.json")
    if os.path.exists(path) and time.time() - os.path.getmtime(path) < 86400:
        return json.load(open(path))
    year = time.localtime().tm_year
    ids, series = bls_series(), []
    for i in range(0, len(ids), 25):
        body = json.dumps({"seriesid": ids[i:i+25], "startyear": str(year - 2), "endyear": str(year)})
        out = subprocess.run(["curl", "-sS", "--http1.1", "-m", "120", "-A", "Tally", "-H", "Content-Type: application/json",
                              "-d", body, "https://api.bls.gov/publicAPI/v1/timeseries/data/"],
                             capture_output=True, text=True, check=True).stdout
        d = json.loads(out)
        if d.get("status") != "REQUEST_SUCCEEDED":
            sys.exit("BLS request failed: %s" % d.get("message"))
        series += d["Results"]["series"]
    json.dump(series, open(path, "w"))
    return series


def read_xlsx(path):
    z = zipfile.ZipFile(path)
    m = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
    shared = ["".join(t.text or "" for t in si.iter(m + "t")) for si in ET.fromstring(z.read("xl/sharedStrings.xml")).findall(m + "si")]
    rows = []
    for row in ET.fromstring(z.read("xl/worksheets/sheet1.xml")).iter(m + "row"):
        cells = {}
        for c in row.findall(m + "c"):
            v = c.find(m + "v")
            val = "" if v is None else v.text
            if c.get("t") == "s":
                val = shared[int(val)]
            cells[re.match(r"[A-Z]+", c.get("r")).group()] = val.strip()
        rows.append(cells)
    return rows


def money(s):
    """'3000' -> (3000, False); '$153 credit' -> (153, True); 'n.a.' -> (0, False)"""
    s = (s or "").replace(",", "")
    m = re.search(r"\$?(\d[\d.]*)\s*(credit)?", s)
    return (float(m.group(1)), bool(m.group(2))) if m else (0.0, False)


def build_taxes():
    rows = read_xlsx(fetch("state_tax_%d.xlsx" % TAX_YEAR))
    states, cur = {}, None
    for r in rows[2:]:
        a = r.get("A", "")
        label = re.sub(r"\s*\(.*", "", a).strip()
        if a.startswith("("):
            if len(a) > 30:
                break  # footnotes begin
        elif label in TF_NAMES:
            cur = TF_NAMES[label]
            (sd_s, sd_cr), sd_j = money(r.get("H")), money(r.get("I"))[0]
            ex = [money(r.get(k)) for k in "JKL"]
            states[cur] = {"single": [], "joint": [], "std": (sd_s, sd_j), "std_credit": sd_cr, "ex": ex}
        if cur is None:
            continue
        for rate_col, br_col, key in (("B", "D", "single"), ("E", "G", "joint")):
            try:
                rate, over = float(r.get(rate_col, "")), float(r.get(br_col, ""))
            except ValueError:
                continue
            states[cur][key].append((over, rate))
    for code in WAGE_TAX_FREE:
        states[code] = {"single": [], "joint": [], "std": (0, 0), "std_credit": False, "ex": [(0, False)] * 3}
    missing = [s[1] for s in STATES if s[1] not in states]
    if missing:
        sys.exit("state tax table missing: %s" % missing)
    return states


def cpp_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    os.makedirs(CACHE, exist_ok=True)
    stamp = "// Generated by scripts/build_data.py -- do not edit by hand.\n"

    # --- ZIP -> county (largest land overlap) ----------------------------------
    best = {}
    counties = {}
    with open(fetch("zcta_county.txt"), encoding="utf-8-sig") as f:
        head = f.readline().rstrip("\n").split("|")
        iz, ic, iname, iland = (head.index(k) for k in ("GEOID_ZCTA5_20", "GEOID_COUNTY_20", "NAMELSAD_COUNTY_20", "AREALAND_PART"))
        for line in f:
            p = line.rstrip("\n").split("|")
            z, c = p[iz], p[ic]
            if not z or c[:2] not in FIPS:
                continue
            counties[c] = p[iname]
            land = int(p[iland] or 0)
            if z not in best or land > best[z][1]:
                best[z] = (c, land)

    def acs(table, level):
        vals = {}
        with open(fetch("acs_%d_%s.dat" % (ACS_YEAR, table))) as f:
            f.readline()
            for line in f:
                geo, est = line.split("|")[:2]
                if geo.startswith(level):
                    try:
                        vals[geo[len(level):]] = int(float(est))
                    except ValueError:
                        pass
        return vals

    inc_z, rent_z, home_z = acs("b19013", "860Z200US"), acs("b25064", "860Z200US"), acs("b25077", "860Z200US")
    inc_s, rent_s, home_s = acs("b19013", "0400000US"), acs("b25064", "0400000US"), acs("b25077", "0400000US")
    inc_c, rent_c, home_c = acs("b19013", "0500000US"), acs("b25064", "0500000US"), acs("b25077", "0500000US")
    inc_us, rent_us, home_us = acs("b19013", "0100000US").get(""), acs("b25064", "0100000US").get(""), acs("b25077", "0100000US").get("")

    county_ids = sorted(counties)
    cidx = {c: i for i, c in enumerate(county_ids)}
    with open(os.path.join(OUT, "places.inc"), "w") as o:
        o.write(stamp)
        o.write("// Census 2020 ZCTA-county relationships; ACS %d 5-year medians (0 = not published).\n" % ACS_YEAR)
        o.write("static const int ACS_YEAR = %d;\n" % ACS_YEAR)
        o.write("static const PlaceCounts US_MEDIANS = {%d, %d, %d};\n" % (inc_us or 0, rent_us or 0, home_us or 0))
        o.write("static const StateInfo STATES[] = {\n")
        for f, code, name, region in STATES:
            o.write('  {"%s", %s, %d, {%d, %d, %d}},\n' % (code, cpp_str(name), region, inc_s.get(f, 0), rent_s.get(f, 0), home_s.get(f, 0)))
        o.write("};\n")
        o.write("// county: state index, name, medians\nstatic const CountyInfo COUNTIES[] = {\n")
        for c in county_ids:
            o.write("  {%d, %s, {%d, %d, %d}},\n" % (FIPS[c[:2]], cpp_str(counties[c]), inc_c.get(c, 0), rent_c.get(c, 0), home_c.get(c, 0)))
        o.write("};\n")
        o.write("// zip, county index, medians (sorted by zip)\nstatic const ZipInfo ZIPS[] = {\n")
        for z in sorted(best):
            o.write("{%d,%d,{%d,%d,%d}},\n" % (int(z), cidx[best[z][0]], inc_z.get(z, 0), rent_z.get(z, 0), home_z.get(z, 0)))
        o.write("};\n")
    print("places.inc: %d ZIPs, %d counties" % (len(best), len(county_ids)))

    # --- data pack: taxes + prices, one plain-text file -------------------------
    # The same text is compiled into Tally (src/data/datapack.inc) and published as
    # data/tally-datapack.txt, which Tally's optional Update can download or import.
    taxes = build_taxes()
    lines = ["tally-datapack 1", "date " + time.strftime("%Y-%m-%d"), "taxyear %d" % TAX_YEAR,
             "source Federal: IRS Rev. Proc. 2025-32, SSA. States: Tax Foundation %d. Prices: BLS CPI-U and average prices." % TAX_YEAR]
    lines += FEDERAL
    def br(lst):
        return " ".join("%.10g:%.10g" % (over, rate) for over, rate in lst) or "none"
    for f, code, *_ in STATES:
        t = taxes[code]
        (es, cs), (ej, cj), (ed, cd) = t["ex"]
        lines.append("state %s single %s" % (code, br(t["single"])))
        lines.append("state %s joint %s" % (code, br(t["joint"])))
        lines.append("state %s std %.10g %.10g %d ex %.10g %d %.10g %d %.10g %d payroll %.10g" % (
            code, t["std"][0], t["std"][1], t["std_credit"], es, cs, ej, cj, ed, cd, STATE_PAYROLL.get(code, 0)))
    print("taxes: %d states" % len(taxes))

    if "--no-bls" in sys.argv:
        old = os.path.join(ROOT, "data", "tally-datapack.txt")
        series_lines = [l for l in open(old).read().splitlines() if l.startswith("series ")] if os.path.exists(old) else []
    else:
        series_lines = []
        for s in fetch_bls():
            pts = sorted((int(d["year"]), int(d["period"][1:]), d["value"]) for d in s["data"]
                         if d["period"].startswith("M") and d["period"] != "M13" and d["value"] not in ("-", ""))
            if pts:
                series_lines.append("series %s %s" % (s["seriesID"], " ".join("%d-%02d=%s" % p for p in pts)))
    lines += series_lines
    print("prices: %d series" % len(series_lines))

    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    with open(os.path.join(ROOT, "data", "tally-datapack.txt"), "w") as o:
        o.write("\n".join(lines) + "\n")
    with open(os.path.join(OUT, "datapack.inc"), "w") as o:
        o.write(stamp)
        o.write("static const char *DATAPACK_LINES[] = {\n")
        for l in lines:
            o.write("  %s,\n" % cpp_str(l))
        o.write("  nullptr,\n};\n")


if __name__ == "__main__":
    main()
