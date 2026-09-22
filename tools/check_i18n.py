#!/usr/bin/env python3
"""Localization check for the app sources (run by scripts/test.sh and CI).

Every user-visible string is written as T("English", "Русский"). This check fails when
  * Cyrillic text appears in code outside the Russian half of a T(...) pair,
  * the English half contains Cyrillic or either half is empty,
  * the two halves use different printf conversions (both feed the same snprintf arguments).
Comments are ignored.
"""
from pathlib import Path
import re, sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = sorted((ROOT / 'src').glob('*.hpp')) + [ROOT / 'src/main.cpp']
LITERAL = r'"((?:[^"\\]|\\.)*)"'
PAIR = re.compile(r'\bT\(\s*' + LITERAL + r'\s*,\s*' + LITERAL + r'\s*\)')
CYRILLIC = re.compile('[Ѐ-ӿ]')
CONVERSION = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGcsp@])')


def code_only(line):
    """The line without a trailing // comment (a // inside a string literal is kept)."""
    out, in_string, i = [], False, 0
    while i < len(line):
        c = line[i]
        if c == '\\' and in_string:
            out.append(line[i:i + 2])
            i += 2
            continue
        if c == '"':
            in_string = not in_string
        elif not in_string and line.startswith('//', i):
            break
        out.append(c)
        i += 1
    return ''.join(out)


def main():
    problems, pairs = [], 0
    for path in SOURCES:
        for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            code = code_only(line)
            where = f'{path.relative_to(ROOT)}:{number}'
            for match in PAIR.finditer(code):
                pairs += 1
                english, russian = match.group(1), match.group(2)
                if not english or not russian:
                    problems.append(f'{where}: empty half in T(...)')
                if CYRILLIC.search(english):
                    problems.append(f'{where}: Cyrillic in the English half: {english[:60]}')
                if CONVERSION.findall(english) != CONVERSION.findall(russian):
                    problems.append(f'{where}: printf conversions differ: {CONVERSION.findall(english)} vs {CONVERSION.findall(russian)}')
            rest = PAIR.sub('T()', code)
            if CYRILLIC.search(rest):
                problems.append(f'{where}: Cyrillic outside T(en, ru): {rest.strip()[:100]}')
    for problem in problems:
        print('FAIL', problem)
    if problems:
        sys.exit(1)
    print(f'{pairs} localized strings checked (English and Russian, printf conversions match)')


if __name__ == '__main__':
    main()
