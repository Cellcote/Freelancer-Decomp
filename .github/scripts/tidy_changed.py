#!/usr/bin/env python3
"""The first-party sources clang-tidy should check for a pull request.

A source is selected when it changed, or when a header it includes changed (from the build's
own dependency records, `ninja -t deps`, so the portable build has to have run first). Only the
sources the clang-tidy build would check are candidates: the compile database's entries under
src/, without the vendored libraries (cmake/Freelancer.cmake's FL_VENDORED_MODULES).

Usage: tidy_changed.py <build dir> <changed-files list>   (prints one absolute source per line)
"""

import json
import os
import subprocess
import sys

VENDORED = ("src/zlib/", "src/qhull/")


def main() -> int:
    build, changed_list = sys.argv[1], sys.argv[2]
    root = os.getcwd()
    with open(changed_list, encoding="utf-8") as stream:
        changed = {os.path.normpath(os.path.join(root, line.strip())) for line in stream if line.strip()}

    with open(os.path.join(build, "compile_commands.json"), encoding="utf-8") as stream:
        database = json.load(stream)
    candidates = set()
    for entry in database:
        path = os.path.normpath(os.path.join(entry["directory"], entry["file"]))
        relative = os.path.relpath(path, root).replace(os.sep, "/")
        if relative.startswith("src/") and not relative.startswith(VENDORED):
            candidates.add(path)

    selected = {path for path in candidates if path in changed}

    # `ninja -t deps`: "<object>: #deps N, ..." then one indented dependency per line; the
    # object's source is its first dependency.
    deps = subprocess.run(["ninja", "-C", build, "-t", "deps"], capture_output=True, text=True, check=True).stdout
    source = None
    first = False
    for line in deps.splitlines():
        if not line.startswith(" "):
            source, first = None, True
            continue
        path = os.path.normpath(os.path.join(build, line.strip()))
        if first:
            source, first = path, False
            continue
        if source in candidates and path in changed:
            selected.add(source)

    for path in sorted(selected):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
