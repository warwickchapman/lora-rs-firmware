import os
import re
import sys

FEATURE_DEFINES = {
    "LRS_ENABLE_AUTOMATIONS": True,
}

def minify_css(css):
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
    css = re.sub(r'\s+', ' ', css)
    css = re.sub(r'\s*([\{\}\:\;\,\>])\s*', r'\1', css)
    return css.strip()

def minify_js(js):
    # Keep JS syntax intact. The previous regex-based minifier corrupted valid
    # code inside template literals/URLs (for example "http://..."), which can
    # break the status page at runtime. Use a conservative pass that removes
    # indentation, blank lines, and standalone comments only.
    out = []
    for raw_line in js.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("//"):
            continue
        out.append(line)
    return "\n".join(out).strip()


def preprocess_web_source(text, defines):
    out = []
    stack = []
    active = True

    for raw_line in text.splitlines():
        stripped = raw_line.strip()
        if stripped.startswith("#if "):
            name = stripped[4:].strip()
            cond = bool(defines.get(name, False))
            stack.append((active, cond))
            active = active and cond
            continue
        if stripped == "#else":
            if not stack:
                raise ValueError("Unexpected #else without matching #if")
            parent_active, cond = stack[-1]
            active = parent_active and (not cond)
            continue
        if stripped == "#endif":
            if not stack:
                raise ValueError("Unexpected #endif without matching #if")
            parent_active, _ = stack.pop()
            active = parent_active
            continue
        if not active:
            continue

        line = raw_line.replace('R"HTML(', '').replace(')HTML"', '')
        out.append(line)

    if stack:
        raise ValueError("Unclosed #if block in web source")
    return "\n".join(out)

_built_once = False


def _generate_web_console(project_dir):
    print("Building Web Console Assets...")

    web_dir = os.path.join(project_dir, "web")
    src_dir = os.path.join(project_dir, "src")
    out_file = os.path.join(src_dir, "web_console_index_gen.h")
    
    html_path = os.path.join(web_dir, "index.html")
    css_path = os.path.join(web_dir, "app.css")
    js_path = os.path.join(web_dir, "app.js")
    
    with open(css_path, "r") as f:
        css = minify_css(f.read())
        
    with open(js_path, "r") as f:
        js = minify_js(preprocess_web_source(f.read(), FEATURE_DEFINES))
        
    with open(html_path, "r") as f:
        html = preprocess_web_source(f.read(), FEATURE_DEFINES)
        
    # Remove HTML structural whitespace.
    lines = html.split('\n')
    cleaned_lines = []
    current_line = []
    
    for line in lines:
        s = line.strip()
        if not s: continue
        current_line.append(s)
            
    if current_line:
        cleaned_lines.append("".join(current_line))
        
    html = "\n".join(cleaned_lines)
    
    # Inject CSS and JS
    html = html.replace('<link rel="stylesheet" href="app.css">', f'<style>{css}</style>')
    html = html.replace('<script src="app.js"></script>', f'<script>{js}</script>')
    
    header_content = f"""// AUTO GENERATED FILE. DO NOT EDIT.
// Built from individual files in the /web directory

#pragma once

const char kIndexHtml[] PROGMEM = R"HTML({html})HTML";
"""
    
    current = None
    if os.path.exists(out_file):
        with open(out_file, "r") as f:
            current = f.read()

    if current == header_content:
        print(f"Web Console Assets unchanged: {out_file}")
        return

    with open(out_file, "w") as f:
        f.write(header_content)

    print(f"Generated {out_file} (HTML size: {len(html)} bytes)")


def build_web_assets(env, target, source):
    global _built_once
    if _built_once:
        return
    _built_once = True
    _generate_web_console(env.get("PROJECT_DIR"))

try:
    Import("env")
    _generate_web_console(env.get("PROJECT_DIR"))
except NameError:
    pass


if __name__ == "__main__":
    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if len(sys.argv) > 1:
        project_dir = os.path.abspath(sys.argv[1])
    _generate_web_console(project_dir)
