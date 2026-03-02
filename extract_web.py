import re
import os

cpp_path = "src/web_console_ui_assets.cpp"

with open(cpp_path, "r") as f:
    content = f.read()

# Find the monolith
match = re.search(r'const char kIndexHtml\[\] PROGMEM =\s*R"HTML\((.*?)\)HTML";', content, re.DOTALL)
if not match:
    print("Could not find kIndexHtml in cpp file.")
    exit(1)

html_content = match.group(1).strip()

# Extract CSS
css_match = re.search(r'<style>(.*?)</style>', html_content, re.DOTALL)
if css_match:
    with open("web/app.css", "w") as f:
        f.write(css_match.group(1).strip())
    # remove CSS from HTML
    html_content = html_content.replace(f'<style>{css_match.group(1)}</style>', '<link rel="stylesheet" href="app.css">')

# Extract JS
js_match = re.search(r'<script>(.*?)</script>', html_content, re.DOTALL)
if js_match:
    with open("web/app.js", "w") as f:
        f.write(js_match.group(1).strip())
    # remove JS from HTML
    html_content = html_content.replace(f'<script>{js_match.group(1)}</script>', '<script src="app.js"></script>')

# Save HTML
with open("web/index.html", "w") as f:
    f.write(html_content)

print("Successfully extracted web assets.")
