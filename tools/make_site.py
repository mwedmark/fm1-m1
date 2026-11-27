#!/usr/bin/env python3
"""The M1's site (GitHub Pages): the emulator on the root page and the install page at /install/, both with the release notes.

  python3 tools/make_site.py FM1_BASE_DIR APP_DIR PACKAGE.fwsc VERSION RELEASE_NOTES.md OUT_DIR

OUT_DIR is the web emulator's build folder (tools/build.py APP_DIR --web: fm1base/build/emu). Writes OUT_DIR/install/index.html and
OUT_DIR/firmware/m1-VERSION.fwsc (like fm1-d50's tools/ci/make_install_page.py: fm1-base's install page with this build), stamps both
pages with the build (version and time) and adds the release notes (docs/RELEASE-X.Y.md, Markdown) as a section on both.
The package carries no Korg data (a release is built without a resource region) and neither do the pages: the user supplies their own file in the browser (INSTALL.md).
Requires fm1-base at the commit that the release notes name.
"""
import datetime
import hashlib
import json
import re
import shutil
import sys
from html import escape as esc
from pathlib import Path

base, app, pkg, version, notes_md, out = (Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4], Path(sys.argv[5]),
                                          Path(sys.argv[6]))
sys.path.insert(0, str(base / "tools"))
import make_site as M  # noqa: E402  (fm1-base:s: strip_module, product_of, res_pick)
from build import read_conf  # noqa: E402

REPO = "https://github.com/mwedmark/fm1-m1"


# --------------------------------------------------------------- the release notes as HTML ---
def inline(t):
    t = esc(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)

    def link(m):
        href = m.group(2)
        if not re.match(r"https?://", href):                         # a relative link (from docs/) becomes a link to the file on GitHub
            href = REPO + "/blob/main/" + re.sub(r"^(\.\./)+", "", href)
        return f'<a href="{href}" target="_blank" rel="noopener">{m.group(1)}</a>'
    return re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, t)


def md_to_html(md):
    out, par, items = [], [], []

    def flush():
        nonlocal par, items
        if par:
            out.append("<p>" + inline(" ".join(par)) + "</p>")
            par = []
        if items:
            out.append("<ul>" + "".join("<li>" + inline(" ".join(i)) + "</li>" for i in items) + "</ul>")
            items = []
    for line in md.splitlines():
        if not line.strip():
            flush()
        elif line.startswith("### "):
            flush()
            out.append("<h4>" + inline(line[4:]) + "</h4>")
        elif line.startswith("## "):
            flush()
            out.append("<h3>" + inline(line[3:]) + "</h3>")
        elif line.startswith("- "):
            if par:
                flush()
            items.append([line[2:].strip()])
        elif line.startswith("  ") and items:
            items[-1].append(line.strip())
        else:
            if items:
                flush()
            par.append(line.strip())
    flush()
    return "\n".join(out)


notes_html = md_to_html(notes_md.read_text(encoding="utf-8"))
style = ("<style>.relnotes{max-width:760px;margin:2.5rem auto 0;padding:0 16px;font:inherit;line-height:1.55}"
         ".relnotes h3{font-size:1.3rem;margin:0 0 1rem}.relnotes h4{font-size:1.05rem;margin:1.6rem 0 .6rem}"
         ".relnotes ul{padding-left:1.2rem}.relnotes li{margin:0 0 .6rem}.relnotes code{font-size:.9em}"
         ".relnotes a{color:inherit}</style>\n")
section = f'{style}<section class="relnotes" id="release-notes" aria-label="Release notes">\n{notes_html}\n</section>\n'

# ---------------------------------------------------------------------------------- the install page ---
conf = read_conf(app / "fm1.toml")
raw = pkg.read_bytes()
product = M.product_of(raw)
prefix = conf["id_prefix"]
if not re.fullmatch(re.escape(prefix) + r"(\d{2}|\d{6})", product):
    raise SystemExit(f"{pkg}: identity {product!r}, expected {prefix} and two or six digits")
if b"FELUCCA-LOADER-1" not in raw:
    raise SystemExit(f"{pkg}: no update loader in the package")
if len(raw) > 400_000:
    raise SystemExit(f"{pkg}: {len(raw)} B: the package probably has a resource region (Korg data) in it")
name = f"m1-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
for d in ("install", "firmware"):
    (out / d).mkdir(parents=True, exist_ok=True)
shutil.copy(pkg, out / "firmware" / name)
for lic in sorted((base / "LICENSES").glob("*.txt")):
    shutil.copy(lic, out / "firmware" / lic.name)

html = (base / "web" / "installer.html").read_text(encoding="utf-8")
lib = "\n".join(M.strip_module((base / "web" / f).read_text(encoding="utf-8")) for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name, "backup": bool(conf.get("backup")),
                   "res": None, "resPick": M.res_pick(app, conf), "boot": None})
for mark in ("/*LIB*/", "/*META*/", "/*APP*/"):
    if html.count(mark) != 1:
        raise SystemExit(f"web/installer.html must contain {mark} exactly once")
repo = conf.get("repo", REPO)
for key, val in (("RES", conf.get("resources_what", "M1 data")), ("RES_JA", "M1 のデータ"), ("NAME", conf["name"]),
                 ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)), ("LICENSE", conf.get("licence", "GPL-3.0-only")),
                 ("CREDITS", " The sample engine is fm1-sf2's. The M1's data is Korg's and is not part of this page.")):
    html = html.replace("{{" + key + "}}", val)
with open(out / "install" / "index.html", "w", encoding="utf-8", newline="\n") as f:
    f.write(html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*APP*/", "null"))

# the build, visible on both pages (as in fm1-d50: issue #109), and the release notes before it
built = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
stamp = f'<p class="small build" id="build" style="font-size:.85rem;opacity:.7;margin:.8rem 0 0">Build {esc(version)}, {built}</p>\n'
for page, close, notes_before in ((out / "install" / "index.html", "</footer>", '<footer>'), (out / "index.html", "</main>", "</main>")):
    text = page.read_text(encoding="utf-8")
    for need in (close, notes_before):
        if text.count(need) != 1:
            raise SystemExit(f"{page} must contain {need} exactly once")
    text = text.replace(notes_before, section + notes_before) if notes_before != close else text.replace(close, section + stamp + close)
    if notes_before != close:
        text = text.replace(close, stamp + close)
    with open(page, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
print(f"install page: {out / 'install' / 'index.html'} ({product}, {name}, sha256 {hashlib.sha256(raw).hexdigest()[:12]}); release notes on both pages")
