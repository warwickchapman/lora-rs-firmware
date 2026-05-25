def version_parts(version):
    parts = []
    value = ""
    raw = (version or "").lstrip("v")
    if "-dev" in raw:
        raw = raw.split("-dev", 1)[0]
        values = raw.split(".")
        if len(values) >= 4:
            raw = ".".join(values[:3])
    for char in raw:
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


def version_dev_build(version):
    raw = (version or "").strip().lstrip("v")
    if "~" not in raw:
        return 0
    suffix = raw.split("~", 1)[1]
    value = ""
    for char in suffix:
        if char.isdigit():
            value += char
            continue
        break
    if not value:
        return 0
    return min(int(value), 65535)
