#!/usr/bin/env python3
"""Copy the History of package/FileBrowser.readme into README.md as its
"Changelog" section (between the changelog markers). Public domain.

    tools/update_readme.py [--check]

--check only reports (exit 1) when README.md is out of date.
make_lha.sh runs it, so every release updates the GitHub page.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
START = "<!-- changelog start: made by tools/update_readme.py from package/FileBrowser.readme -->"
END = "<!-- changelog end -->"


def history():
    # the readme is ISO-8859-1, like every Amiga readme
    text = open(os.path.join(ROOT, "package", "FileBrowser.readme"), encoding="latin-1").read()
    m = re.search(r"^History\n-+\n\n(.*)", text, re.S | re.M)
    if not m:
        sys.exit("package/FileBrowser.readme: no History section")
    # versions: "1.2 (16.11.2026)" followed by " - item" lines (items may wrap)
    versions = []
    for line in m.group(1).splitlines():
        v = re.match(r"(\d+\.\d+(?: beta)?) \((\d+)\.(\d+)\.(\d+)\)\s*$", line)
        if v:
            versions.append(("%s" % v.group(1), "%s-%s-%s" % (v.group(4), v.group(3), v.group(2)), []))
        elif line.startswith(" - "):
            versions[-1][2].append(line[3:].strip())
        elif line.strip() and versions and versions[-1][2]:
            versions[-1][2][-1] += " " + line.strip()
    out = []
    for ver, date, items in versions:
        if len(items) == 1:
            out.append("- **%s** (%s): %s" % (ver, date, items[0]))
        else:
            out.append("- **%s** (%s):" % (ver, date))
            out.extend("  - " + it for it in items)
    return "\n".join(out)


def main():
    path = os.path.join(ROOT, "README.md")
    readme = open(path, encoding="utf-8").read()
    block = START + "\n" + history() + "\n" + END
    if START in readme:
        new = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda _: block, readme, flags=re.S)
    else:
        sys.exit("README.md has no changelog markers")
    if new == readme:
        return 0
    if "--check" in sys.argv:
        print("README.md changelog is out of date: run tools/update_readme.py", file=sys.stderr)
        return 1
    open(path, "w", encoding="utf-8").write(new)
    print("README.md changelog updated")
    return 0


sys.exit(main())
