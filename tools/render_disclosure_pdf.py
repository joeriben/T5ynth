#!/usr/bin/env python3
"""Render the LRO technical disclosure to a searchable, dated PDF.

The Markdown in docs/ is the source of truth; this produces the file format an
archive record (Zenodo report, conference submission) wants, with the PDF
metadata a search index reads.

    python3 tools/render_disclosure_pdf.py [-o OUT.pdf]

Needs `markdown` and `weasyprint`. WeasyPrint must be new enough for the
installed pydyf — 60.x against pydyf 0.11+ raises `PDF.__init__() takes 1
positional argument`. A throwaway venv is the least invasive fix:

    python3 -m venv /tmp/pdfvenv && /tmp/pdfvenv/bin/pip install 'weasyprint>=62' markdown
    /tmp/pdfvenv/bin/python tools/render_disclosure_pdf.py
"""

import argparse
import pathlib
import re
import sys

import markdown
import weasyprint

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "docs" / "LRO_TECHNICAL_DISCLOSURE.md"

TITLE = "The Language-Resonant Oscillator — technical disclosure"
AUTHOR = "Benjamin Jörissen"
KEYWORDS = (
    "sound synthesis, oscillator, large language model, program synthesis, "
    "runtime program generation, language-model-generated synthesis code, "
    "Csound, digital musical instrument, defensive publication, prior art"
)

CSS = """
@page {
  size: A4;
  margin: 22mm 20mm 20mm 20mm;
  @bottom-center {
    content: counter(page) " / " counter(pages);
    font: 8.5pt "Helvetica Neue", Helvetica, sans-serif;
    color: #666;
  }
  @bottom-right {
    content: "akroasys — LRO technical disclosure";
    font: 8pt "Helvetica Neue", Helvetica, sans-serif;
    color: #999;
  }
}
body {
  font: 10pt/1.45 "Palatino", "Palatino Linotype", Georgia, serif;
  color: #111;
  hyphens: auto;
}
h1 { font-size: 17pt; line-height: 1.25; margin: 0 0 0.2em 0; }
h2 {
  font-size: 12pt; margin: 1.5em 0 0.4em 0;
  padding-bottom: 0.15em; border-bottom: 0.5pt solid #bbb;
  page-break-after: avoid;
}
h3 { font-size: 10.5pt; margin: 1.1em 0 0.3em 0; page-break-after: avoid; }
p, li { orphans: 3; widows: 3; }
ul, ol { padding-left: 1.3em; }
li { margin-bottom: 0.25em; }
a { color: #14506b; text-decoration: none; }
code {
  font: 8.8pt "SF Mono", Menlo, Consolas, monospace;
  background: #f2f2f0; padding: 0 0.2em; border-radius: 2px;
}
pre {
  font: 8.2pt/1.35 "SF Mono", Menlo, Consolas, monospace;
  background: #f7f7f5; border-left: 2pt solid #ccc;
  padding: 0.7em 0.9em; white-space: pre-wrap;
  page-break-inside: avoid;
}
pre code { background: none; padding: 0; font-size: inherit; }
table {
  border-collapse: collapse; width: 100%;
  font-size: 8.8pt; margin: 0.8em 0;
  page-break-inside: avoid;
}
th, td {
  border: 0.5pt solid #ccc; padding: 0.35em 0.5em;
  text-align: left; vertical-align: top;
}
th { background: #f0f0ee; font-weight: 600; }
hr { border: none; border-top: 0.5pt solid #ddd; margin: 1.6em 0; }
.titleblock {
  margin-bottom: 1.6em; padding-bottom: 0.9em;
  border-bottom: 1.2pt solid #333;
}
.titleblock p { font-size: 9pt; line-height: 1.5; margin: 0.35em 0 0 0; color: #333; }
"""


def to_html(md_text: str) -> str:
    # The first heading and the author block become a title block; the rest of
    # the document renders as written.
    lines = md_text.splitlines()
    assert lines[0].startswith("# "), "expected an H1 on the first line"
    heading = lines[0][2:].strip()

    rest = "\n".join(lines[1:]).lstrip("\n")
    split = re.search(r"^## ", rest, flags=re.M)
    front_matter, body_md = rest[: split.start()], rest[split.start() :]

    md = markdown.Markdown(extensions=["tables", "fenced_code", "sane_lists"])
    front_html = md.convert(front_matter.strip())
    md.reset()
    body_html = md.convert(body_md)

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{heading}</title>
<meta name="author" content="{AUTHOR}">
<meta name="keywords" content="{KEYWORDS}">
<meta name="description" content="Technical description of the Language-Resonant \
Oscillator implemented in akroasys, published as a defensive publication.">
<style>{CSS}</style>
</head>
<body>
<div class="titleblock"><h1>{heading}</h1>{front_html}</div>
{body_html}
</body>
</html>"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--out", type=pathlib.Path,
                    default=ROOT / "docs" / "LRO_TECHNICAL_DISCLOSURE.pdf")
    args = ap.parse_args()

    html = to_html(SOURCE.read_text(encoding="utf-8"))
    weasyprint.HTML(string=html, base_url=str(SOURCE.parent)).write_pdf(args.out)
    print(f"{args.out}  ({args.out.stat().st_size / 1024:.0f} kB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
