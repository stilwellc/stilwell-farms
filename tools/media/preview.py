#!/usr/bin/env python3
"""Render README.md roughly as GitHub does (python-markdown + GitHub-like CSS) to build/readme_preview.html and
screenshot it with headless Chrome:  .venv/bin/python tools/media/preview.py  (needs `pip install markdown`)"""
import os, subprocess, markdown
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CSS = """body{background:#fff;margin:0}.markdown-body{box-sizing:border-box;max-width:980px;margin:0 auto;padding:45px;
font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;color:#1f2328}
h2{border-bottom:1px solid #d1d9e0;padding-bottom:.3em;font-size:1.5em}h3{font-size:1.25em}
table{border-collapse:collapse;display:block;width:max-content;max-width:100%;overflow:auto}
td,th{border:1px solid #d1d9e0;padding:6px 13px}tr:nth-child(2n){background:#f6f8fa}img{max-width:100%;box-sizing:content-box}
code{background:#eff1f3;padding:.2em .4em;border-radius:6px;font-size:85%}pre{background:#f6f8fa;padding:16px;border-radius:6px}"""
import re
src = open(os.path.join(ROOT, 'README.md')).read()
# GitHub renders markdown inside HTML blocks once there's a blank line; python-markdown wants markdown="1"
src = re.sub(r'<(div|td|table|tr)((?: [^>]*)?)>', lambda m: f'<{m.group(1)}{m.group(2)} markdown="1">', src)
html = markdown.markdown(src, extensions=['tables', 'fenced_code', 'md_in_html'])
out = os.path.join(ROOT, 'readme_preview.html')     # beside README so docs/media paths resolve
open(out, 'w').write(f'<!doctype html><meta charset=utf-8><style>{CSS}</style><article class="markdown-body">{html}</article>')
chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
for name, w, h in (('desktop', 1280, 16000), ('phone', 420, 16000)):
    png = os.path.join(ROOT, 'build', f'readme_{name}.png')
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', f'--window-size={w},{h}',
                    f'--screenshot={png}', 'file://' + out], check=True, capture_output=True, timeout=120)
    print(png)
os.remove(out)
