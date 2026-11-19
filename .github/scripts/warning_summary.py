"""Summarise the compiler warnings of an MSBuild log as a Markdown report.

A warning in a header is reported once per translation unit that includes it; the summary
counts each distinct (file, line, warning) once. Prints Markdown to stdout: totals, the
warnings by code and the warnings by project, most frequent first.
Usage: python warning_summary.py <msbuild warnings log> [repository root]
"""
import collections
import ntpath
import os
import re
import sys

# C:\a\repo\src\Common\x.cpp(12,5): warning C4100: 'Arg': unreferenced parameter [C:\a\...\Common.vcxproj]
WARNING = re.compile(
    r"^\s*(?:\d+>)?\s*(?P<file>[^()]+?)\((?P<line>\d+)(?:,\d+)?\)\s*:\s*warning\s+(?P<code>[A-Z]+\d+)\s*:"
    r"\s*(?P<text>.*?)\s*(?:\[(?P<project>[^\]]+)\])?\s*$"
)


def main() -> int:
    log_path = sys.argv[1]
    root = os.path.normcase(os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "."))

    seen = set()
    by_code = collections.Counter()
    by_project = collections.Counter()
    titles = {}
    with open(log_path, encoding="utf-8", errors="replace") as log:
        for raw in log:
            m = WARNING.match(raw)
            if not m:
                continue
            path = os.path.normcase(os.path.abspath(m["file"].strip()))
            if path.startswith(root):
                path = os.path.relpath(path, root)
            key = (path.replace("\\", "/"), int(m["line"]), m["code"])
            if key in seen:
                continue
            seen.add(key)
            project = ntpath.splitext(ntpath.basename(m["project"] or "?"))[0]
            by_code[m["code"]] += 1
            by_project[project] += 1
            titles.setdefault(m["code"], m["text"])

    print("## Compiler warnings")
    print()
    print(f"{len(seen)} distinct warnings (file, line, code) in {len(by_project)} projects.")
    print()
    print("| Warning | Count | Example |")
    print("|---|---:|---|")
    for code, count in by_code.most_common():
        example = titles[code].replace("|", "\\|")
        if len(example) > 90:
            example = example[:87] + "..."
        print(f"| {code} | {count} | {example} |")
    print()
    print("| Project | Count |")
    print("|---|---:|")
    for project, count in by_project.most_common():
        print(f"| {project} | {count} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
