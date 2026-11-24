#!/usr/bin/env python3
"""Makes the new-filter wizard's starter (data/new-filter) from Sully's QoL Filter.

    python3 -I tools/starter/make_starter.py <QoL-Filter folder> data/new-filter

Writes:
  starter-filter.json  a copy of the filter: the wizard edits it (levels, steps, parts turned off, styles)
  parts.json           the wizard's steps and parts: which of the filter's rules each part is, and how it is edited
  sounds/              the filter's sounds (icons are not copied: 13 MB; the editor takes them from the player's
                       QoL-Filter folder when it is there)

Every rule of the filter must be in exactly one part: the script stops when one is left out or in two.
"""
import json
import os
import re
import shutil
import sys

R = lambda a, b: ["R%03d" % n for n in range(a, b + 1)]

STEPS = [
    {"key": "name", "title": "Name"},
    {"key": "levels", "title": "Levels"},
    {"key": "protect", "title": "Always show",
     "help": "Items no other rule can hide. Each one can be limited to the lower levels."},
    {"key": "gold", "title": "Gold and potions",
     "help": "What is hidden, from which level, and from which character level. Each line is one step: add more for finer control."},
    {"key": "junk", "title": "Gems and odds and ends",
     "help": "Gems, scrolls and small items, hidden as the game goes on."},
    {"key": "gear", "title": "Gear",
     "help": "White, superior, magic and rare weapons and armour."},
    {"key": "uniques", "title": "Uniques, sets, jewellery, coupons",
     "help": "Uniques and sets by tier, rings, amulets, charms and Ancient Coupons."},
    {"key": "announce", "title": "Announcements and labels",
     "help": "Chat lines, sounds, label colours and short names. Turn off what you do not want."},
    {"key": "look", "title": "Look"},
    {"key": "check", "title": "Check"},
]

# kind: show (never hidden), hide (hidden), effect (chat, sound, label, name). rows: each rule can be edited.
# minimal: on in the "minimal" start.
PARTS = [
    # Always show
    ("protect", "show", "mythUniques", "Mythical uniques", R(1, 2), True,
     "Mythical uniques, Lich King Essence amulets included."),
    ("protect", "show", "ethereal", "Ethereal weapons with 0 or 2+ sockets", R(3, 6), True,
     "Ethereal weapons good for runewords, by tier; ethereal Mythical bases on every level."),
    ("protect", "show", "skillBases", "Bases with +All Skills, summons or +3 to a skill tab", R(7, 9), True,
     "White and superior gear with +All Skills, +1 Summons Limit, or +3 or more to a skill tab."),
    ("protect", "show", "valuables", "S-tier materials and high runes", R(10, 10), True,
     "S-tier crafting materials, runes Mal and up, gold and purple ESR runes and their decals."),
    ("protect", "show", "superiorRolls", "Superior bases with the best special rolls", R(52, 68), True,
     "Superior gloves, belts, helms, boots, missile weapons and crossbows with their top rolls (and lower rolls on the lower levels)."),
    # Gold and potions
    ("gold", "hide", "gold", "Gold piles", R(70, 82), True,
     "Small gold piles. Each step hides piles up to an amount, from a filter level and a character level."),
    ("gold", "hide", "minorPotions", "Minor healing and mana potions", R(84, 84), True, ""),
    ("gold", "hide", "lightPotions", "Light healing and mana potions", ["R085", "R090"], True, ""),
    ("gold", "hide", "normalPotions", "Healing and mana potions", ["R086", "R088", "R091", "R092"], False, ""),
    ("gold", "hide", "greaterPotions", "Greater and super healing and mana potions", ["R087", "R089", "R093"], False, ""),
    ("gold", "hide", "throwingPotions", "Throwing potions (gas and oil)", R(94, 94), True, ""),
    # Gems and odds and ends
    ("junk", "hide", "lowValue", "Low-value items, scrolls and tomes", R(95, 95), True,
     "Stamina, antidote and thawing potions, tomes and scrolls."),
    ("junk", "hide", "ancientScrolls", "Ancient Scrolls", R(96, 96), False, ""),
    ("junk", "hide", "chippedGems", "Chipped gems and skulls", R(97, 97), True, ""),
    ("junk", "hide", "flawedGems", "Flawed gems and skulls", R(100, 101), False, ""),
    ("junk", "hide", "standardGems", "Standard gems and skulls", R(102, 103), False, ""),
    ("junk", "hide", "moreGems", "More gem and skull grades", R(98, 99), False, ""),
    ("junk", "hide", "rejuvs", "Small rejuvenation potions", ["R133", "R135"], False, ""),
    ("junk", "hide", "lowMaps", "Tier 1 maps", R(134, 134), False, ""),
    ("junk", "hide", "decipherer", "Ancient Decipherer", R(136, 136), False, ""),
    # Gear
    ("gear", "hide", "inferior", "Inferior (cracked, damaged) items", R(104, 104), True, ""),
    ("gear", "hide", "normalGear", "White weapons and armour", ["R105", "R106", "R109"], False,
     "White gear with at most 1 socket; on higher levels all white gear."),
    ("gear", "hide", "superiorGear", "Superior weapons and armour without +All Skills", ["R110", "R111", "R116"], False, ""),
    ("gear", "hide", "magicGear", "Magic weapons and armour", R(107, 108), False, ""),
    ("gear", "hide", "quivers", "Arrows and bolts", ["R112", "R114", "R117"], False, ""),
    ("gear", "hide", "rareGear", "Rare weapons and armour", ["R113", "R115", "R118"], False, ""),
    ("gear", "hide", "mythicalBases", "White Mythical bases", R(124, 124), False, ""),
    # Uniques, sets, jewellery, coupons
    ("uniques", "hide", "uniqueGear", "Unique weapons and armour by tier", ["R119", "R121", "R123"], False, ""),
    ("uniques", "hide", "setItems", "Set items by tier", ["R120", "R122"], False, ""),
    ("uniques", "hide", "jewellery", "Magic rings, amulets, class jewellery and charms", ["R125", "R127", "R128", "R131"], False, ""),
    ("uniques", "hide", "normalCoupons", "Normal Ancient Coupons", ["R126", "R129", "R132"], False, ""),
    ("uniques", "hide", "exceptionalCoupons", "Exceptional Ancient Coupons", R(130, 130), False, ""),
]
# Announcements and labels: one part per section of the filter, for what is left.
EFFECT_SECTIONS_ROWS = False


def strip_comments(s):
    out, i, ins = [], 0, False
    while i < len(s):
        c = s[i]
        if ins:
            out.append(c)
            if c == "\\":
                out.append(s[i + 1])
                i += 2
                continue
            if c == '"':
                ins = False
        elif c == '"':
            ins = True
            out.append(c)
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = j if j >= 0 else len(s)
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def main():
    src, out = sys.argv[1], sys.argv[2]
    text = open(os.path.join(src, "Sullys-QoL-Filter.json"), encoding="utf-8").read()
    data = json.loads(re.sub(r",(\s*[}\]])", r"\1", strip_comments(text)))
    rules = {r["comment"].split(" ")[0]: r for r in data["rules"]}

    # The section of each rule.
    part = sec = None
    where, order = {}, []
    for line in text.split("\n"):
        s = line.strip()
        m = re.match(r"// ==== (.*)", s)
        if m:
            part = m.group(1)
            continue
        m = re.match(r"// ---- (.*)", s)
        if m:
            sec = m.group(1)
            continue
        m = re.search(r'"comment": "(R\d{3}) ', s)
        if m:
            where[m.group(1)] = (part, sec)
            order.append(m.group(1))

    used = {}
    parts = []
    for step, kind, key, label, ids, minimal, help_ in PARTS:
        for i in ids:
            if i not in rules:
                sys.exit("no rule " + i)
            if i in used:
                sys.exit(i + " is in " + used[i] + " and " + key)
            used[i] = key
        parts.append({"key": key, "step": step, "kind": kind, "label": label, "help": help_, "rules": ids,
                      "rows": True, "minimal": minimal})
    # What is left: announcement and label parts, one per section.
    sections = {}
    for i in order:
        if i in used:
            continue
        sections.setdefault(where[i], []).append(i)
    for (p, s), ids in sections.items():
        key = re.sub(r"[^A-Za-z]", "", (p + s).title())
        key = key[0].lower() + key[1:]
        label = s if p in ("Highlights and Labels",) else p + ": " + s
        if p == "Hide by Strictness" and s == "Gold":
            label = "Gold: large-pile announcement and $$ names"
        for i in ids:
            used[i] = key
        parts.append({"key": key, "step": "announce", "kind": "effect", "label": label, "help": "", "rules": ids,
                      "rows": EFFECT_SECTIONS_ROWS, "minimal": True})
    missing = [i for i in order if i not in used]
    if missing:
        sys.exit("left out: " + ", ".join(missing))

    # The styles the look step recolours (those with a border, background or beam).
    names = {"UniqueLabel": "Uniques", "SetLabel": "Set items", "Mythical": "Mythicals", "EtherealMythical": "Ethereal Mythicals",
             "MidRune": "Mid runes", "PurpleRune": "Purple ESR runes", "STierMaterial": "S-tier materials", "Valuable": "Valuables",
             "Currency": "Currency", "Jackpot": "Jackpot finds", "GoldPlate": "Gold-plated items", "Uber": "Uber items",
             "Map": "Maps", "MapTier1": "Tier 1 maps", "MapTier5": "Tier 5 maps", "CharmLabel": "Charms",
             "RareJewelry": "Rare jewellery", "QuestLabel": "Quest items", "CraftedLabel": "Crafted items",
             "TemperedLabel": "Tempered items", "BaseTop": "Top bases", "BaseSockets": "Socketed bases",
             "AffixMax": "Top rolls", "AffixGood": "Good rolls"}
    looks = []
    for name, style in data.get("styles", {}).items():
        if not any(k in style for k in ("labelBorder", "labelBackground", "droplight")):
            continue
        looks.append({"style": name, "label": names.get(name, name)})

    os.makedirs(out, exist_ok=True)
    shutil.copyfile(os.path.join(src, "Sullys-QoL-Filter.json"), os.path.join(out, "starter-filter.json"))
    sounds = os.path.join(out, "sounds")
    os.makedirs(sounds, exist_ok=True)
    for f in sorted(os.listdir(os.path.join(src, "sounds"))):
        if f.lower().endswith((".mp3", ".wav", ".flac")):
            shutil.copyfile(os.path.join(src, "sounds", f), os.path.join(sounds, f))
    doc = {
        "formatVersion": 1,
        "about": "The ESR Filter Editor's new-filter wizard: its steps, and the parts of starter-filter.json each step edits. Made by tools/starter/make_starter.py.",
        "from": data.get("title", "") + " " + data.get("version", ""),
        "towns": [1, 40, 75, 103, 109],
        "steps": STEPS,
        "parts": parts,
        "looks": looks,
    }
    with open(os.path.join(out, "parts.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write("\n")
    print("%d rules in %d parts, %d looks, from %s" % (len(order), len(parts), len(looks), doc["from"]))


if __name__ == "__main__":
    main()
