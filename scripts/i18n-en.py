#!/usr/bin/env python3
"""Rewrites src/i18n/en.json from the texts the sources translate."""
import glob, json, os, re

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# A counted text needs its singular written by hand.
ones = {"%Ln photos": "%Ln photo", "%Ln photos saved to %1": "%Ln photo saved to %1",
        "Compress %Ln photos": "Compress %Ln photo"}
found = {}
for path in sorted(glob.glob(root + "/src/*.qml") + glob.glob(root + "/src/*.cpp") + glob.glob(root + "/src/*.h")):
    code = re.sub(r"(?m)^\s*//.*$", "", open(path).read())
    for m in re.finditer(r'(?<![A-Za-z0-9_])(qsTr|tr)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', code):
        text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
        text = re.sub(r"\\(.)", lambda k: {"n": "\n", "t": "\t"}.get(k.group(1), k.group(1)), text)
        i, depth, commas = m.end(), 0, 0
        while i < len(code):
            c = code[i]
            if c == '"':
                i += 1
                while code[i] != '"':
                    i += 2 if code[i] == "\\" else 1
            elif c in "([{":
                depth += 1
            elif c in ")]}":
                if depth == 0:
                    break
                depth -= 1
            elif c == "," and depth == 0:
                commas += 1
            i += 1
        found[text] = found.get(text, False) or commas >= 2
catalog = {k: ({"one": ones[k], "other": k} if found[k] else k) for k in sorted(found)}
with open(root + "/src/i18n/en.json", "w") as out:
    json.dump(catalog, out, ensure_ascii=False, indent=2)
    out.write("\n")
print(len(catalog), "texts")
