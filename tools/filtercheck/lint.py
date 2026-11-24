"""Checks Sully's QoL Filter against its own conventions and the mod's tables.

    python3 lint.py <filter.json> <data dir>

- Every rule title is "R### | what it does | action", and every R### is used once.
- A list of item codes that holds Mythical bases holds all of them (or all the
  Mythical weapons, for a weapons-only rule), and never 7wd, which is an
  ordinary elite sword despite its name.
- Every item code in the filter exists in the mod.
- Every translated text has all 13 languages.
Prints the next unused R### id. Exits 1 when something is wrong.
"""
import csv, json, re, sys

LANGS = ["enUS", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "ruRU", "zhCN", "zhTW", "esMX", "jaJP", "ptBR"]


def table(path):
    with open(path, encoding="latin-1", newline="") as f:
        rows = list(csv.reader(f, delimiter="\t"))
    head = rows[0]
    return [dict(zip(head, r)) for r in rows[1:] if r and r[0] != "Expansion"]


def strip_comments(text):
    return "\n".join(l for l in text.splitlines() if not l.strip().startswith("//"))


def main():
    path, data = sys.argv[1], sys.argv[2]
    text = open(path, encoding="utf-8").read()
    filt = json.loads(strip_comments(text))
    problems = []

    # The mod's bases. A Mythical base is its own normal, exceptional and elite version.
    weapons = table(f"{data}/weapons.txt")
    armor = table(f"{data}/armor.txt")
    misc = table(f"{data}/misc.txt")
    def mythical(rows):
        return {r["code"] for r in rows if r.get("code") and r["code"] == r.get("normcode") == r.get("ubercode") == r.get("ultracode")
                and r.get("level") == "96"}
    myth_weapons, myth_armor = mythical(weapons), mythical(armor)
    myth_all = myth_weapons | myth_armor
    known = {r["code"] for rows in (weapons, armor, misc) for r in rows if r.get("code")}

    ids = {}
    for n, rule in enumerate(filt["rules"], 1):
        title = rule.get("comment", "")
        m = re.match(r"^(R\d{3,}) \| .+ \| .+$", title)
        if not m:
            problems.append(f"rule {n}: title is not 'R### | what | action': {title[:80]!r}")
            continue
        rid = m.group(1)
        if rid in ids:
            problems.append(f"{rid}: used by rules {ids[rid]} and {n}")
        ids.setdefault(rid, n)
        for key in ("itemCodes", "excludeItemCodes"):
            codes = set(rule.get(key, []))
            for c in sorted(codes - known):
                problems.append(f"{rid}: {key} has {c!r}, which is not an item code in the mod")
            if "7wd" in codes and codes & myth_all:
                problems.append(f"{rid}: {key} lists 7wd with the Mythical bases; 7wd is an ordinary elite sword")
            have = codes & myth_all
            if len(have) >= 5 and not (have >= myth_all or (have >= myth_weapons and not have & myth_armor)):
                missing = sorted((myth_weapons if not have & myth_armor else myth_all) - have)
                problems.append(f"{rid}: {key} holds {len(have)} Mythical bases but misses {len(missing)}: {' '.join(missing)}")

    def walk(value, where):
        if isinstance(value, dict):
            if value and all(k in LANGS for k in value):
                missing = [l for l in LANGS if l not in value]
                if missing:
                    problems.append(f"{where}: text lacks {', '.join(missing)}")
                return
            for k, v in value.items():
                walk(v, f"{where}.{k}")
        elif isinstance(value, list):
            for i, v in enumerate(value):
                walk(v, f"{where}[{i}]")
    for n, rule in enumerate(filt["rules"], 1):
        walk({k: v for k, v in rule.items() if k != "comment"}, rule.get("comment", f"rule {n}")[:5])
    for name, style in filt.get("styles", {}).items():
        walk(style, f"style {name}")
    walk(filt.get("notifications", {}), "notifications")

    for p in problems:
        print("PROBLEM:", p)
    numbers = sorted(int(r[1:]) for r in ids)
    print(f"lint: {len(filt['rules'])} rules, {len(problems)} problems; Mythical bases in the mod: {len(myth_all)} "
          f"({len(myth_weapons)} weapons, {len(myth_armor)} armor); next unused id: R{numbers[-1] + 1:03d}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
