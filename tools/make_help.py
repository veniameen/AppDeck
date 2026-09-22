#!/usr/bin/env python3
"""Render docs/USER_GUIDE.md into the app's help page.

    python3 tools/make_help.py <Resources folder>      ->  <Resources>/Help.html

The converter covers the Markdown the guide uses (headings, paragraphs, lists, tables, code,
quotes, links, emphasis) and has no dependencies. Relative links to repository files become plain
text: the help page is read offline. The documentation is English; the app UI is English/Russian.
"""
from pathlib import Path
import html, re, sys

ROOT = Path(__file__).resolve().parents[1]
GUIDE = ROOT / 'docs/USER_GUIDE.md'
TITLE = 'AppDeck — User Guide'

CSS = """
:root{color-scheme:light dark;--bg:#fbfbfd;--fg:#1d1f24;--muted:#626873;--line:#e3e5ea;--code:#f1f2f5;--accent:#2f6fde}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--fg:#e8eaee;--muted:#9aa1ad;--line:#2a2e35;--code:#1c1f24;--accent:#7aa7f7}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.6 -apple-system,BlinkMacSystemFont,"SF Pro Text","Helvetica Neue",sans-serif}
main{max-width:840px;margin:0 auto;padding:48px 28px 80px}
h1{font-size:32px;line-height:1.2;margin:0 0 8px;letter-spacing:-.02em}
h2{font-size:22px;margin:44px 0 12px;padding-top:12px;border-top:1px solid var(--line)}
h3{font-size:17px;margin:28px 0 8px}
p,ul,ol,table,pre,blockquote{margin:0 0 14px}
ul,ol{padding-left:22px}li{margin:4px 0}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
code{font:13px/1.4 "SF Mono",Menlo,monospace;background:var(--code);padding:1px 5px;border-radius:5px}
pre{background:var(--code);padding:14px 16px;border-radius:10px;overflow:auto}pre code{padding:0;background:none}
table{border-collapse:collapse;width:100%;font-size:14px}
th,td{border:1px solid var(--line);padding:8px 10px;text-align:left;vertical-align:top}
th{background:var(--code);font-weight:600}
blockquote{border-left:3px solid var(--accent);padding:4px 14px;color:var(--muted)}
hr{border:0;border-top:1px solid var(--line);margin:28px 0}
.lead{color:var(--muted);font-size:16px;margin-bottom:24px}
"""


def slug(text):
    return re.sub(r'[^\w\- ]', '', text.lower()).strip().replace(' ', '-')


def inline(text):
    text = html.escape(text, quote=False)
    spans = []

    def keep(fragment):
        spans.append(fragment)
        return f'\0{len(spans) - 1}\0'

    text = re.sub(r'`([^`]+)`', lambda m: keep(f'<code>{m.group(1)}</code>'), text)
    text = re.sub(r'!\[([^\]]*)\]\([^)]*\)', '', text)  # screenshots live in the repository, not in the app

    def link(m):
        label, target = m.group(1), m.group(2)
        if re.match(r'https?://|mailto:', target):
            return keep(f'<a href="{target}">{label}</a>')
        if target.startswith('#'):
            return keep(f'<a href="{target}">{label}</a>')
        return label  # a repository file: not available offline

    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    text = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<em>\1</em>', text)
    return re.sub(r'\0(\d+)\0', lambda m: spans[int(m.group(1))], text)


def render(markdown):
    lines = markdown.splitlines()
    out, i = [], 0
    first_heading = True

    def table_row(line):
        return [cell.strip() for cell in line.strip().strip('|').split('|')]

    while i < len(lines):
        line = lines[i]
        if not line.strip() or line.lstrip().startswith(('<p', '</p', '<img')) or re.fullmatch(r'\s*!\[[^\]]*\]\([^)]*\)\s*', line):
            i += 1  # blank lines, raw HTML and image-only lines (screenshots are for GitHub)
            continue
        if line.startswith('```'):
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith('```'):
                body.append(html.escape(lines[i], quote=False))
                i += 1
            out.append('<pre><code>' + '\n'.join(body) + '</code></pre>')
            i += 1
            continue
        heading = re.match(r'(#{1,6})\s+(.*)', line)
        if heading:
            level, text = len(heading.group(1)), heading.group(2).strip()
            out.append(f'<h{level} id="{slug(text)}">{inline(text)}</h{level}>')
            i += 1
            if level == 1 and first_heading:
                first_heading = False
            continue
        if re.match(r'^(-{3,}|\*{3,})\s*$', line):
            out.append('<hr>')
            i += 1
            continue
        if line.lstrip().startswith('|') and i + 1 < len(lines) and re.match(r'^\s*\|?\s*:?-{2,}', lines[i + 1]):
            head = table_row(line)
            i += 2
            rows = []
            while i < len(lines) and lines[i].lstrip().startswith('|'):
                rows.append(table_row(lines[i]))
                i += 1
            out.append('<table><thead><tr>' + ''.join(f'<th>{inline(c)}</th>' for c in head) + '</tr></thead><tbody>'
                       + ''.join('<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>' for r in rows)
                       + '</tbody></table>')
            continue
        if line.startswith('>'):
            body = []
            while i < len(lines) and lines[i].startswith('>'):
                body.append(lines[i][1:].strip())
                i += 1
            out.append('<blockquote><p>' + inline(' '.join(body)) + '</p></blockquote>')
            continue
        bullet = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)', line)
        if bullet:
            ordered = bullet.group(2)[0].isdigit()
            items = []
            while i < len(lines):
                m = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)', lines[i])
                if m:
                    items.append(m.group(3))
                elif lines[i].startswith('  ') and lines[i].strip() and items:
                    items[-1] += ' ' + lines[i].strip()
                else:
                    break
                i += 1
            tag = 'ol' if ordered else 'ul'
            out.append(f'<{tag}>' + ''.join(f'<li>{inline(item)}</li>' for item in items) + f'</{tag}>')
            continue
        para = [line.strip()]
        i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r'(#{1,6}\s|```|>|\s*([-*]|\d+\.)\s|\s*\|)', lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append('<p>' + inline(' '.join(para)) + '</p>')
    return '\n'.join(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    resources = Path(sys.argv[1])
    body = render(GUIDE.read_text(encoding='utf-8'))
    page = ('<!doctype html>\n<html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{TITLE}</title><style>{CSS}</style></head>\n<body><main>\n{body}\n</main></body></html>\n')
    resources.mkdir(parents=True, exist_ok=True)
    (resources / 'Help.html').write_text(page, encoding='utf-8')
    print(f'Help.html: {len(page):,} bytes')


if __name__ == '__main__':
    main()
