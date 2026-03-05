import os
import re

def minify_css(css):
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
    css = re.sub(r'\s+', ' ', css)
    css = re.sub(r'\s*([\{\}\:\;\,\>])\s*', r'\1', css)
    return css.strip()

def minify_js(js):
    # Keep JS syntax intact. The previous regex-based minifier corrupted valid
    # code inside template literals/URLs (for example "http://..."), which can
    # break the status page at runtime.
    return "\n".join(line.rstrip() for line in js.splitlines()).strip()

def build_web_assets(env, target, source):
    print("Building Web Console Assets...")
    
    web_dir = os.path.join(env.get("PROJECT_DIR"), "web")
    src_dir = os.path.join(env.get("PROJECT_DIR"), "src")
    out_file = os.path.join(src_dir, "web_console_index_gen.h")
    
    html_path = os.path.join(web_dir, "index.html")
    css_path = os.path.join(web_dir, "app.css")
    js_path = os.path.join(web_dir, "app.js")
    
    with open(css_path, "r") as f:
        css = minify_css(f.read())
        
    with open(js_path, "r") as f:
        js = minify_js(f.read())
        
    with open(html_path, "r") as f:
        html = f.read()
        
    # Remove HTML structural whitespace, but CAREFUL with C++ macros
    # Split by lines, strip, and rejoin, but keep newlines around C++ macros
    lines = html.split('\n')
    cleaned_lines = []
    current_line = []
    
    for line in lines:
        s = line.strip()
        if not s: continue
        # If it contains a C++ macro we extracted, flush current line and preserve the macro
        if ')HTML"' in s or 'R"HTML(' in s or s.startswith('#if') or s.startswith('#else') or s.startswith('#endif'):
            if current_line:
                cleaned_lines.append("".join(current_line))
                current_line = []
            cleaned_lines.append(s)
        else:
            current_line.append(s)
            
    if current_line:
        cleaned_lines.append("".join(current_line))
        
    html = "\n".join(cleaned_lines)
    
    # Inject CSS and JS
    html = html.replace('<link rel="stylesheet" href="app.css">', f'<style>{css}</style>')
    html = html.replace('<script src="app.js"></script>', f'<script>{js}</script>')
    
    # Ensure C++ macros are on their own lines
    html = html.replace(')HTML"', ')HTML"\n')
    html = html.replace('R"HTML(', '\nR"HTML(')
    html = html.replace('#if ', '\n#if ')
    html = html.replace('#else', '\n#else\n')
    html = html.replace('#endif', '\n#endif\n')
    
    # Cleanup any double newlines we introduced
    html = re.sub(r'\n+', '\n', html)
    
    header_content = f"""// AUTO GENERATED FILE. DO NOT EDIT.
// Built from individual files in the /web directory

#pragma once

const char kIndexHtml[] PROGMEM = R"HTML({html})HTML";
"""
    
    with open(out_file, "w") as f:
        f.write(header_content)
        
    print(f"Generated {out_file} (HTML size: {len(html)} bytes)")

try:
    Import("env")
    env.AddPreAction("$BUILD_DIR/src/web_console_ui_assets.cpp.o", build_web_assets)
except NameError:
    pass
