"""Summarise the compiler or clang-tidy warnings in a build log as a Markdown report.

Reads MSVC lines (`file(line,col): warning C4100: text`) and GCC / Clang / clang-tidy lines
(`file:line:col: warning: text [check]`). A warning in a header is printed once per source file
that includes it; the summary counts each distinct (file, line, warning) once. The module is the
folder under src/ (src/include and the like count as modules too).

Prints Markdown to stdout: the total, the warnings by code (MSVC) or check (clang-tidy), and
the warnings by module, most frequent first.
Usage: python warning_summary.py <build log> [title]
"""
import collections
import re
import sys

MSVC = re.compile(
    r"^(?:\s*\d+>)?\s*(?P<file>[^()\s][^()]*?)\((?P<line>\d+)(?:,\d+)?\)\s*:\s*"
    r"warning\s+(?P<code>[A-Z]+\d+)\s*:\s*(?P<text>.*?)\s*(?:\[[^\]]+\])?\s*$"
)
GNU = re.compile(
    r"^(?P<file>[^:\s][^:]*?|[A-Za-z]:[^:]+?):(?P<line>\d+):(?:\d+:)?\s*warning:\s*"
    r"(?P<text>.*?)\s*\[(?P<code>[^\]]+)\]\s*$"
)


def module_of(path: str) -> str:
    parts = path.replace("\\", "/").split("/")
    lowered = [p.lower() for p in parts]
    if "src" in lowered:
        i = len(lowered) - 1 - lowered[::-1].index("src")
        if i + 1 < len(parts) - 1:
            return parts[i + 1]
    return "(outside src)"


def main() -> int:
    log_path = sys.argv[1]
    title = sys.argv[2] if len(sys.argv) > 2 else "Compiler warnings"

    seen = set()
    by_code = collections.Counter()
    by_module = collections.Counter()
    examples = {}
    with open(log_path, encoding="utf-8", errors="replace") as log:
        for raw in log:
            m = MSVC.match(raw) or GNU.match(raw)
            if not m:
                continue
            path = m["file"].strip().replace("\\", "/")
            key = (path.lower(), int(m["line"]), m["code"])
            if key in seen:
                continue
            seen.add(key)
            by_code[m["code"]] += 1
            by_module[module_of(path)] += 1
            examples.setdefault(m["code"], m["text"])

    print(f"## {title}")
    print()
    print(f"{len(seen)} distinct warnings (file, line, code) in {len(by_module)} modules.")
    if not seen:
        return 0
    print()
    print("| Warning | Count | Example |")
    print("|---|---:|---|")
    for code, count in by_code.most_common():
        example = examples[code].replace("|", "\\|")
        if len(example) > 90:
            example = example[:87] + "..."
        print(f"| {code} | {count} | {example} |")
    print()
    print("| Module | Count |")
    print("|---|---:|")
    for module, count in by_module.most_common():
        print(f"| {module} | {count} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
