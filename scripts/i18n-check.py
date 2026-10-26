#!/usr/bin/env python3
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TABLE = ROOT / "src" / "core" / "I18nTable.cpp"
HEADER = ROOT / "src" / "core" / "I18n.h"
SOURCES = ["src/core", "src/platform/win", "src/platform/mac"]
EXTENSIONS = {".cpp", ".h", ".mm"}

LIT = r'"(?:[^"\\]|\\.)*"'
LITS = r'(?:' + LIT + r'\s*)+'
CALL = r'\b(?:tr|trf|trw|trfw|NDTr|NDTrf|N_)\(\s*'
DIRECT = re.compile(CALL + r'(' + LITS + r')')
TERNARY = re.compile(CALL + r'[^?"();]*\?\s*(' + LITS + r')\s*:\s*(' + LITS + r')')
ENTRY = re.compile(r'\{\s*(' + LITS + r'),\s*\{((?:\s*' + LITS + r',?)+)\}\s*\}')
CELL = re.compile(r'(' + LITS + r')\s*(?:,|$)')
LANGUAGE = re.compile(r'\{\s*Language::\w+,\s*"([\w-]+)"')


def unquote(chunk):
    out = []
    for lit in re.findall(LIT, chunk):
        body = lit[1:-1]
        body = re.sub(r'\\(["\\nrt])', lambda m: {'"': '"', '\\': '\\', 'n': '\n', 'r': '\r', 't': '\t'}[m.group(1)], body)
        out.append(body)
    return ''.join(out)


def source_keys():
    keys = {}
    for folder in SOURCES:
        for path in sorted((ROOT / folder).iterdir()):
            if path.suffix not in EXTENSIONS or path == TABLE:
                continue
            text = path.read_text(encoding="utf-8")
            for m in DIRECT.finditer(text):
                keys.setdefault(unquote(m.group(1)), path.relative_to(ROOT))
            for m in TERNARY.finditer(text):
                for g in (1, 2):
                    keys.setdefault(unquote(m.group(g)), path.relative_to(ROOT))
    return keys


def languages():
    return LANGUAGE.findall(HEADER.read_text(encoding="utf-8"))[1:]


def table_keys():
    text = TABLE.read_text(encoding="utf-8")
    return {unquote(m.group(1)): [unquote(c) for c in CELL.findall(m.group(2).strip())] for m in ENTRY.finditer(text)}


def main():
    parser = argparse.ArgumentParser(description="Check that every translatable UI string has a translation in every language.")
    parser.add_argument("--list", action="store_true", help="print every key found in the sources")
    parser.add_argument("--tr", nargs=2, metavar=("LANG", "KEY"), help="print KEY translated into LANG (the key itself when there is none)")
    parser.add_argument("--missing", nargs="?", const="", metavar="LANG", help="print only the keys without a complete translation (or missing LANG)")
    args = parser.parse_args()

    table = table_keys()
    langs = languages()
    if args.tr:
        lang, key = args.tr
        row = table.get(key, [])
        i = langs.index(lang) if lang in langs else len(row)
        print(row[i] if i < len(row) and row[i] else key)
        return 0
    keys = source_keys()
    if args.list:
        for k in sorted(keys):
            print(repr(k))
        return 0

    def gaps(k):
        row = table.get(k)
        if row is None:
            return list(langs)
        return [lang for i, lang in enumerate(langs) if i >= len(row) or not row[i]]

    if args.missing is not None:
        for k in sorted(keys):
            g = gaps(k)
            if g and (not args.missing or args.missing in g):
                print(f"{','.join(g)}: {k!r}")
        return 0
    unused = sorted(k for k in table if k not in keys)
    for k in unused:
        print(f"i18n: unused translation: {k!r}", file=sys.stderr)
    bad = 0
    for k in sorted(keys):
        g = gaps(k)
        if g:
            bad += 1
            print(f"i18n: missing {','.join(g)} translation ({keys[k]}): {k!r}", file=sys.stderr)
    for k, row in sorted(table.items()):
        if len(row) > len(langs):
            bad += 1
            print(f"i18n: {len(row)} columns for {len(langs)} languages: {k!r}", file=sys.stderr)
    if bad:
        print(f"i18n: {bad} key(s) incomplete in {TABLE.relative_to(ROOT)} (languages: {', '.join(langs)})", file=sys.stderr)
        return 1
    print(f"i18n: {len(keys)} keys, all translated into {', '.join(langs)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
