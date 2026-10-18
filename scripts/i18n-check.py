#!/usr/bin/env python3
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TABLE = ROOT / "src" / "core" / "I18nSpanish.cpp"
SOURCES = ["src/core", "src/platform/win", "src/platform/mac"]
EXTENSIONS = {".cpp", ".h", ".mm"}

LIT = r'"(?:[^"\\]|\\.)*"'
LITS = r'(?:' + LIT + r'\s*)+'
CALL = r'\b(?:tr|trf|trw|trfw|NDTr|NDTrf|N_)\(\s*'
DIRECT = re.compile(CALL + r'(' + LITS + r')')
TERNARY = re.compile(CALL + r'[^?"();]*\?\s*(' + LITS + r')\s*:\s*(' + LITS + r')')
ENTRY = re.compile(r'\{\s*(' + LITS + r'),\s*(' + LITS + r')\}')


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


def table_keys():
    text = TABLE.read_text(encoding="utf-8")
    return {unquote(m.group(1)): unquote(m.group(2)) for m in ENTRY.finditer(text)}


def main():
    parser = argparse.ArgumentParser(description="Check that every translatable UI string has a Spanish entry.")
    parser.add_argument("--list", action="store_true", help="print every key found in the sources")
    parser.add_argument("--missing", action="store_true", help="print only the keys without a translation")
    args = parser.parse_args()

    keys = source_keys()
    table = table_keys()
    if args.list:
        for k in sorted(keys):
            print(repr(k))
        return 0
    missing = sorted(k for k in keys if k not in table)
    unused = sorted(k for k in table if k not in keys)
    if args.missing:
        for k in missing:
            print(repr(k))
        return 0
    for k in unused:
        print(f"i18n: unused translation: {k!r}", file=sys.stderr)
    for k in missing:
        print(f"i18n: missing Spanish translation ({keys[k]}): {k!r}", file=sys.stderr)
    if missing:
        print(f"i18n: {len(missing)} key(s) missing from {TABLE.relative_to(ROOT)}", file=sys.stderr)
        return 1
    print(f"i18n: {len(keys)} keys, all translated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
