def version_parts(version):
    parts = []
    value = ""
    for char in version or "":
        if char.isdigit():
            value += char
            continue
        if value:
            parts.append(min(int(value), 255))
            value = ""
            if len(parts) == 3:
                break
        if char == "-":
            break
    if value and len(parts) < 3:
        parts.append(min(int(value), 255))
    while len(parts) < 3:
        parts.append(0)
    return parts[:3]
