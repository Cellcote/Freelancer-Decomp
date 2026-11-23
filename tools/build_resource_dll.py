#!/usr/bin/env python3
# Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
# faithful to the behaviour of the original game.
#
# PORT(macOS, Linux): builds a resource-only DLL (a PE image holding only a .rsrc section) from
# one of the resource scripts under src/Resources, as rc.exe and the linker do on Windows, for
# the builds that have neither (cmake/Freelancer.cmake, fl_add_generated_resource_dll). The
# platform layer reads it (src/include/pe_resources.h). Plain Python 3, nothing else.
#
# What the game reads from these DLLs is supported, with rc.exe's meaning:
# - STRINGTABLE (with its own LANGUAGE, CHARACTERISTICS and VERSION options): blocks of 16
#   strings, id (block - 1) * 16 + n, each a length-prefixed UTF-16LE string; missing entries
#   are empty.
# - "<name> <type> "<file>"": the file's bytes as they are, for a numeric or named type
#   (23, the HTML infocards, RCDATA, user types), and BITMAP (the file without its
#   BITMAPFILEHEADER).
# - LANGUAGE at the top level sets the language of what follows; a resource's own LANGUAGE
#   option sets it for that resource only.
# DIALOG, DIALOGEX, MENU, MENUEX, ACCELERATORS, ICON, CURSOR and VERSIONINFO are skipped (the
# port has no dialogs or menus from resources yet; nothing reads the others); so are
# preprocessor lines (#include <windows.h>, #pragma code_page): scripts are read as UTF-8,
# which the Windows build declares with /c65001.
#
# The output is reproducible: no time stamps.
#
# Usage: build_resource_dll.py SCRIPT.rc OUTPUT.dll [--machine i386|amd64] [--depfile FILE]

import argparse
import os
import struct
import sys

LANG = {
    "LANG_NEUTRAL": 0x00,
    "LANG_INVARIANT": 0x7F,
    "LANG_ENGLISH": 0x09,
    "LANG_GERMAN": 0x07,
    "LANG_FRENCH": 0x0C,
    "LANG_SPANISH": 0x0A,
    "LANG_ITALIAN": 0x10,
    "LANG_JAPANESE": 0x11,
    "LANG_KOREAN": 0x12,
    "LANG_CHINESE": 0x04,
    "LANG_RUSSIAN": 0x19,
    "LANG_POLISH": 0x15,
}
SUBLANG = {
    "SUBLANG_NEUTRAL": 0x00,
    "SUBLANG_DEFAULT": 0x01,
    "SUBLANG_SYS_DEFAULT": 0x02,
    "SUBLANG_ENGLISH_US": 0x01,
    "SUBLANG_ENGLISH_UK": 0x02,
    "SUBLANG_GERMAN": 0x01,
    "SUBLANG_FRENCH": 0x01,
    "SUBLANG_SPANISH": 0x01,
    "SUBLANG_SPANISH_MODERN": 0x03,
    "SUBLANG_ITALIAN": 0x01,
    "SUBLANG_JAPANESE_JAPAN": 0x01,
    "SUBLANG_KOREAN": 0x01,
    "SUBLANG_CHINESE_TRADITIONAL": 0x01,
    "SUBLANG_CHINESE_SIMPLIFIED": 0x02,
    "SUBLANG_RUSSIAN_RUSSIA": 0x01,
    "SUBLANG_POLISH_POLAND": 0x01,
}

TYPES = {
    "CURSOR": 1,
    "BITMAP": 2,
    "ICON": 3,
    "MENU": 4,
    "DIALOG": 5,
    "STRINGTABLE": 6,
    "FONTDIR": 7,
    "FONT": 8,
    "ACCELERATORS": 9,
    "RCDATA": 10,
    "MESSAGETABLE": 11,
    "VERSIONINFO": 16,
    "DLGINCLUDE": 17,
    "PLUGPLAY": 19,
    "VXD": 20,
    "ANICURSOR": 21,
    "ANIICON": 22,
    "HTML": 23,
    "MANIFEST": 24,
}
# Resource statements whose body is skipped (see the top of the file).
SKIPPED = {"DIALOG", "DIALOGEX", "MENU", "MENUEX", "ACCELERATORS", "ICON", "CURSOR",
           "VERSIONINFO", "TOOLBAR", "DLGINIT", "TEXTINCLUDE", "DESIGNINFO"}
# Of those, the ones that name a file rather than hold a block.
FILE_SKIPPED = {"ICON", "CURSOR"}
# Options that may follow a resource's type, with their argument counts.
OPTIONS = {"LANGUAGE": 2, "CHARACTERISTICS": 1, "VERSION": 1}
MEMORY_FLAGS = {"PRELOAD", "LOADONCALL", "FIXED", "MOVEABLE", "DISCARDABLE", "PURE", "IMPURE",
                "SHARED", "NONSHARED"}


class RcError(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# Script reading
# ---------------------------------------------------------------------------------------------

def tokenize(text, path):
    """(kind, value, line) tokens: 'str' (a quoted string, quotes and escapes kept as written),
    'word' (anything else up to white space, a comma or a brace) and 'punct' (',', '{', '}')."""
    tokens = []
    i = 0
    n = len(text)
    line = 1
    at_line_start = True
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            at_line_start = True
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if at_line_start and c == "#":
            # A preprocessor line (with its continuations).
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    line += 1
                    i += 1
                i += 1
            continue
        at_line_start = False
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise RcError("%s:%d: unterminated comment" % (path, line))
            line += text.count("\n", i, end)
            i = end + 2
            continue
        if c == '"' or (c in "Ll" and i + 1 < n and text[i + 1] == '"'):
            start = i
            if c != '"':
                i += 1
            i += 1
            while True:
                if i >= n or text[i] == "\n":
                    raise RcError("%s:%d: unterminated string" % (path, line))
                if text[i] == '"':
                    if i + 1 < n and text[i + 1] == '"':
                        i += 2
                        continue
                    i += 1
                    break
                if text[i] == "\\" and i + 1 < n:
                    if text[i + 1] == "\n":
                        line += 1  # a line continuation (unquote drops it)
                    i += 2
                    continue
                i += 1
            tokens.append(("str", text[start:i], line))
            continue
        if c in ",{}":
            tokens.append(("punct", c, line))
            i += 1
            continue
        start = i
        while i < n and text[i] not in " \t\r\n\f\v,{}\"":
            i += 1
        tokens.append(("word", text[start:i], line))
    return tokens


def unquote(token, path, line):
    """The text of a string token, with rc.exe's escapes: "" for a quote, \\n \\t \\r \\a \\\\
    \\" \\0, octal \\ooo and \\x with two hex digits (four in an L"..." string), as code
    points."""
    raw = token
    wide = raw[0] in "Ll"
    if wide:
        raw = raw[1:]
    body = raw[1:-1]
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == '"' and i + 1 < len(body) and body[i + 1] == '"':
            out.append('"')
            i += 2
            continue
        if c != "\\" or i + 1 >= len(body):
            out.append(c)
            i += 1
            continue
        e = body[i + 1]
        i += 2
        if e == "\n":
            continue  # a line continuation: the string goes on on the next line
        simple = {"n": "\n", "t": "\t", "r": "\r", "a": "\x08", "\\": "\\", '"': '"'}
        if e in simple:
            out.append(simple[e])
        elif e in "xX":
            digits = ""
            while (i < len(body) and len(digits) < (4 if wide else 2) and
                   body[i] in "0123456789abcdefABCDEF"):
                digits += body[i]
                i += 1
            if not digits:
                raise RcError("%s:%d: \\x without digits" % (path, line))
            out.append(chr(int(digits, 16)))
        elif e in "01234567":
            digits = e
            while i < len(body) and len(digits) < 3 and body[i] in "01234567":
                digits += body[i]
                i += 1
            out.append(chr(int(digits, 8)))
        else:
            # rc.exe keeps an unknown escape as written.
            out.append("\\" + e)
    return "".join(out)


def number(word, path, line):
    """A numeric expression rc.exe accepts in these scripts: decimal or 0x hex, an L suffix, a
    leading minus, and '+' and '|' between terms ('+' binds tighter, as in C)."""
    def term(part):
        part = part.strip()
        if part.endswith(("L", "l")):
            part = part[:-1]
        try:
            return int(part, 16) if part.lower().lstrip("-").startswith("0x") else int(part, 10)
        except ValueError:
            raise RcError("%s:%d: not a number: %s" % (path, line, word))

    total = 0
    for alternative in word.split("|"):
        total |= sum(term(part) for part in alternative.split("+"))
    return total & 0xFFFF


def is_number(word):
    w = word.rstrip("Ll") if not word.lower().startswith("0x") else word.rstrip("Ll")
    if w.startswith("-"):
        w = w[1:]
    if w.lower().startswith("0x"):
        return len(w) > 2 and all(ch in "0123456789abcdefABCDEF" for ch in w[2:])
    return w.isdigit()


class Script:
    def __init__(self, path):
        self.path = path
        self.dir = os.path.dirname(os.path.abspath(path))
        data = open(path, "rb").read()
        if data.startswith(b"\xff\xfe"):
            text = data[2:].decode("utf-16-le")
        elif data.startswith(b"\xef\xbb\xbf"):
            text = data[3:].decode("utf-8")
        else:
            text = data.decode("utf-8")
        # CRLF scripts read as LF ones (a line continuation is then always a backslash and LF).
        self.tokens = tokenize(text.replace("\r\n", "\n"), path)
        self.pos = 0
        # rc.exe's /l0x0409 (cmake/Freelancer.cmake's CMAKE_RC_FLAGS), until a LANGUAGE.
        self.language = 0x0409
        # (type, name, language) -> bytes; a type or name is an int or an upper-case str.
        self.resources = {}
        # (block, language) -> {index: text}
        self.strings = {}
        self.inputs = []

    def peek(self, offset=0):
        p = self.pos + offset
        return self.tokens[p] if p < len(self.tokens) else (None, None, 0)

    def take(self):
        token = self.peek()
        if token[0] is None:
            raise RcError("%s: unexpected end of script" % self.path)
        self.pos += 1
        return token

    def expect_word(self, what):
        kind, value, line = self.take()
        if kind != "word":
            raise RcError("%s:%d: expected %s, found %s" % (self.path, line, what, value))
        return value, line

    def language_args(self):
        primary, line = self.expect_word("a language")
        if self.peek()[1] == ",":
            self.take()
        sub, _ = self.expect_word("a sublanguage")
        p = LANG.get(primary)
        s = SUBLANG.get(sub)
        if p is None and is_number(primary):
            p = number(primary, self.path, line)
        if s is None and is_number(sub):
            s = number(sub, self.path, line)
        if p is None or s is None:
            raise RcError("%s:%d: unknown language %s, %s" % (self.path, line, primary, sub))
        return (s << 10) | p

    def options(self, language):
        """A resource's memory flags and options (LANGUAGE, CHARACTERISTICS, VERSION)."""
        while True:
            kind, value, _ = self.peek()
            if kind != "word":
                return language
            upper = value.upper()
            if upper in MEMORY_FLAGS:
                self.take()
            elif upper == "LANGUAGE":
                self.take()
                language = self.language_args()
            elif upper in OPTIONS:
                self.take()
                self.take()
            else:
                return language

    def begin(self):
        kind, value, line = self.take()
        if not ((kind == "punct" and value == "{") or (kind == "word" and value.upper() == "BEGIN")):
            raise RcError("%s:%d: expected BEGIN, found %s" % (self.path, line, value))

    def is_end(self, token):
        kind, value, _ = token
        return (kind == "punct" and value == "}") or (kind == "word" and value.upper() == "END")

    def is_begin(self, token):
        kind, value, _ = token
        return (kind == "punct" and value == "{") or (kind == "word" and value.upper() == "BEGIN")

    def skip_block(self):
        """A skipped resource: its header up to the first BEGIN, then the block, nested."""
        while not self.is_begin(self.peek()):
            self.take()
        depth = 0
        while True:
            token = self.take()
            if self.is_begin(token):
                depth += 1
            elif self.is_end(token):
                depth -= 1
                if depth == 0:
                    return

    def string_table(self):
        language = self.options(self.language)
        self.begin()
        while not self.is_end(self.peek()):
            word, line = self.expect_word("a string id")
            ident = number(word, self.path, line)
            if self.peek()[1] == ",":
                self.take()
            kind, value, line = self.take()
            if kind != "str":
                raise RcError("%s:%d: expected a string" % (self.path, line))
            text = unquote(value, self.path, line)
            # Adjacent strings are joined.
            while self.peek()[0] == "str":
                _, more, more_line = self.take()
                text += unquote(more, self.path, more_line)
            table = self.strings.setdefault(((ident >> 4) + 1, language), {})
            if (ident & 15) in table:
                raise RcError("%s:%d: string %d defined twice" % (self.path, line, ident))
            table[ident & 15] = text
        self.take()

    def name_of(self, word, line):
        if is_number(word):
            return number(word, self.path, line)
        if word.startswith('"'):
            return unquote(word, self.path, line).upper()
        return word.upper()

    def file_bytes(self, token):
        kind, value, line = token
        name = unquote(value, self.path, line) if kind == "str" else value
        path = os.path.join(self.dir, name.replace("\\", os.sep))
        if not os.path.isfile(path):
            # The scripts' names are Windows': match the case on a case-sensitive disk.
            folder, base = os.path.split(path)
            matches = [f for f in os.listdir(folder or ".") if f.lower() == base.lower()]
            if not matches:
                raise RcError("%s:%d: missing file %s" % (self.path, line, name))
            path = os.path.join(folder, matches[0])
        self.inputs.append(path)
        return open(path, "rb").read()

    def add(self, rtype, name, language, data, line):
        key = (rtype, name, language)
        if key in self.resources:
            raise RcError("%s:%d: resource %r %r defined twice" % (self.path, line, rtype, name))
        self.resources[key] = data

    def parse(self):
        while self.peek()[0] is not None:
            kind, value, line = self.take()
            upper = value.upper() if kind == "word" else value
            if kind == "word" and upper == "LANGUAGE":
                self.language = self.language_args()
                continue
            if kind == "word" and upper == "STRINGTABLE":
                self.string_table()
                continue
            if kind not in ("word", "str"):
                raise RcError("%s:%d: unexpected %s" % (self.path, line, value))
            name = self.name_of(value, line)
            type_word, type_line = self.expect_word("a resource type")
            type_upper = type_word.upper()
            language = self.options(self.language)
            if type_upper in SKIPPED:
                if type_upper in FILE_SKIPPED:
                    self.take()  # the file name, quoted or not
                else:
                    self.skip_block()
                continue
            token = self.peek()
            if token[0] == "str" or (token[0] == "word" and not self.is_begin(token)):
                self.take()
                data = self.file_bytes(token)
            else:
                raise RcError("%s:%d: inline data for %s is not supported" %
                              (self.path, type_line, type_word))
            if type_upper == "BITMAP":
                if data[:2] != b"BM":
                    raise RcError("%s:%d: not a bitmap file" % (self.path, line))
                data = data[14:]
                rtype = TYPES["BITMAP"]
            elif type_upper in TYPES:
                rtype = TYPES[type_upper]
            elif is_number(type_word):
                rtype = number(type_word, self.path, type_line)
            else:
                rtype = self.name_of(type_word, type_line)
            self.add(rtype, name, language, data, line)

        for (block, language), table in sorted(self.strings.items()):
            data = bytearray()
            for index in range(16):
                units = table.get(index, "").encode("utf-16-le", "surrogatepass")
                data += struct.pack("<H", len(units) // 2) + units
            self.add(TYPES["STRINGTABLE"], block, language, bytes(data), 0)


# ---------------------------------------------------------------------------------------------
# Image writing
# ---------------------------------------------------------------------------------------------

SECTION_RVA = 0x1000
FILE_ALIGNMENT = 0x200
SECTION_ALIGNMENT = 0x1000


def align(value, to):
    return (value + to - 1) // to * to


def sort_key(key):
    # Named entries first, in upper-case order, then ids in ascending order.
    return (0, key, 0) if isinstance(key, str) else (1, "", key)


def resource_section(resources):
    """The .rsrc section's bytes, as cvtres lays it out: the directory tables (types, names,
    languages), the name strings, the data entries, then the data (8-byte aligned)."""
    tree = {}
    for (rtype, name, language), data in resources.items():
        tree.setdefault(rtype, {}).setdefault(name, {})[language] = data

    types = sorted(tree, key=sort_key)
    tables = []  # (key path, entries)
    tables.append(((), types))
    for rtype in types:
        tables.append(((rtype,), sorted(tree[rtype], key=sort_key)))
    for rtype in types:
        for name in sorted(tree[rtype], key=sort_key):
            tables.append(((rtype, name), sorted(tree[rtype][name])))

    offset = 0
    table_offsets = {}
    for path, entries in tables:
        table_offsets[path] = offset
        offset += 16 + 8 * len(entries)

    strings = sorted({k for k in list(tree) + [n for t in tree for n in tree[t]] if isinstance(k, str)})
    string_offsets = {}
    for s in strings:
        string_offsets[s] = offset
        offset += 2 + len(s.encode("utf-16-le", "surrogatepass"))
    offset = align(offset, 4)

    leaves = []
    for rtype in types:
        for name in sorted(tree[rtype], key=sort_key):
            for language in sorted(tree[rtype][name]):
                leaves.append((rtype, name, language))
    entry_offsets = {}
    for leaf in leaves:
        entry_offsets[leaf] = offset
        offset += 16

    data_offsets = {}
    for leaf in leaves:
        offset = align(offset, 8)
        data_offsets[leaf] = offset
        offset += len(tree[leaf[0]][leaf[1]][leaf[2]])
    size = offset

    out = bytearray(size)
    for path, entries in tables:
        base = table_offsets[path]
        named = sum(1 for e in entries if isinstance(e, str))
        struct.pack_into("<IIHHHH", out, base, 0, 0, 0, 0, named, len(entries) - named)
        for index, key in enumerate(entries):
            if isinstance(key, str):
                name_field = 0x80000000 | string_offsets[key]
            else:
                name_field = key
            child = path + (key,)
            if len(child) < 3:
                target = 0x80000000 | table_offsets[child]
            else:
                target = entry_offsets[child]
            struct.pack_into("<II", out, base + 16 + 8 * index, name_field, target)
    for s, at in string_offsets.items():
        units = s.encode("utf-16-le", "surrogatepass")
        struct.pack_into("<H", out, at, len(units) // 2)
        out[at + 2:at + 2 + len(units)] = units
    for leaf in leaves:
        data = tree[leaf[0]][leaf[1]][leaf[2]]
        struct.pack_into("<IIII", out, entry_offsets[leaf], SECTION_RVA + data_offsets[leaf],
                         len(data), 0, 0)
        out[data_offsets[leaf]:data_offsets[leaf] + len(data)] = data
    return bytes(out)


def image(section, machine):
    """A DLL with one section, .rsrc, and no code, imports or entry point (link /NOENTRY)."""
    pe32plus = machine == "amd64"
    optional_size = 0xF0 if pe32plus else 0xE0
    headers_size = align(0x40 + 4 + 20 + optional_size + 40, FILE_ALIGNMENT)
    raw_size = align(len(section), FILE_ALIGNMENT)
    image_size = SECTION_RVA + align(max(len(section), 1), SECTION_ALIGNMENT)

    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)

    characteristics = 0x0002 | 0x2000  # EXECUTABLE_IMAGE | DLL
    characteristics |= 0x0020 if pe32plus else 0x0100  # LARGE_ADDRESS_AWARE / 32BIT_MACHINE
    coff = struct.pack("<HHIIIHH", 0x8664 if pe32plus else 0x014C, 1, 0, 0, 0, optional_size,
                       characteristics)

    directories = [(0, 0)] * 16
    directories[2] = (SECTION_RVA, len(section))
    if pe32plus:
        optional = struct.pack("<HBBIIIII", 0x20B, 14, 0, 0, raw_size, 0, 0, SECTION_RVA)
        optional += struct.pack("<QIIHHHHHHIIIIHHQQQQII", 0x180000000, SECTION_ALIGNMENT,
                                FILE_ALIGNMENT, 6, 0, 0, 0, 6, 0, 0, image_size, headers_size, 0,
                                2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    else:
        optional = struct.pack("<HBBIIIIII", 0x10B, 6, 0, 0, raw_size, 0, 0, SECTION_RVA,
                               SECTION_RVA)
        optional += struct.pack("<IIIHHHHHHIIIIHHIIIIII", 0x10000000, SECTION_ALIGNMENT,
                                FILE_ALIGNMENT, 4, 0, 0, 0, 4, 0, 0, image_size, headers_size, 0,
                                2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    for rva, size in directories:
        optional += struct.pack("<II", rva, size)
    assert len(optional) == optional_size

    section_header = struct.pack("<8sIIIIIIHHI", b".rsrc", len(section), SECTION_RVA, raw_size,
                                 headers_size, 0, 0, 0, 0, 0x40000040)

    out = bytearray(dos + b"PE\0\0" + coff + optional + section_header)
    out += bytes(headers_size - len(out))
    out += section
    out += bytes(raw_size - len(section))
    return bytes(out)


def main(argv):
    parser = argparse.ArgumentParser(description="Build a resource-only DLL from an .rc script.")
    parser.add_argument("script")
    parser.add_argument("output")
    parser.add_argument("--machine", choices=("i386", "amd64"), default="i386",
                        help="PE32 (i386, as the Win32 build) or PE32+ (amd64)")
    parser.add_argument("--depfile", help="write the script's inputs as a Makefile depfile")
    args = parser.parse_args(argv)

    try:
        script = Script(args.script)
        script.parse()
    except (RcError, UnicodeDecodeError, OSError) as error:
        print("build_resource_dll: %s" % error, file=sys.stderr)
        return 1

    data = image(resource_section(script.resources), args.machine)
    folder = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(folder, exist_ok=True)
    temporary = args.output + ".tmp"
    with open(temporary, "wb") as f:
        f.write(data)
    os.replace(temporary, args.output)

    if args.depfile:
        def escape(path):
            return path.replace("\\", "/").replace(" ", "\\ ").replace("$", "$$").replace("#", "\\#")
        inputs = [os.path.abspath(args.script)] + [os.path.abspath(p) for p in script.inputs]
        with open(args.depfile, "w", encoding="utf-8") as f:
            f.write("%s: %s\n" % (escape(os.path.abspath(args.output)),
                                  " \\\n  ".join(escape(p) for p in inputs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
