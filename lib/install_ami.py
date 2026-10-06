# install.ami / uninstall.ami KEY=VALUE parser for the Amiga GUI installer.
# Python68K: top-level defs only; no classes; one physical line per call.

def parse_ami_text(text):
    result = {}
    nl = chr(10)
    semi = chr(59)
    lines = text.split(nl)
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i = i + 1
        if line == "":
            continue
        if line[0] == "#" or line[0] == semi:
            continue
        eq = line.find("=")
        if eq <= 0:
            continue
        key = line[0:eq].strip()
        val = line[eq + 1:].strip()
        result[key] = val
    return result


def ami_get(d, key, default):
    if key in d:
        return d[key]
    return default


def ami_get_int(d, key, default):
    if key not in d:
        return default
    s = d[key]
    if s == "":
        return default
    return int(s)


def validate_install_ami(d):
    if ami_get(d, "NAME", "") == "":
        return (0, "NAME missing")
    if ami_get(d, "START", "") == "":
        return (0, "START missing")
    if ami_get(d, "DRAWER_NAME", "") == "":
        return (0, "DRAWER_NAME missing")
    return (1, "")


def validate_uninstall_ami(d):
    if ami_get(d, "NAME", "") == "":
        return (0, "NAME missing")
    if ami_get(d, "INSTALL_PATH", "") == "":
        return (0, "INSTALL_PATH missing")
    return (1, "")


def render_ami(d):
    keys = ["NAME", "VERSION", "START", "DRAWER_NAME", "ASSIGN", "PATH_ADD", "USER_STARTUP", "README", "MIN_OS", "INSTALL_PATH", "MARKER_BEGIN", "MARKER_END", "INSTALLED_UTC"]
    out = ""
    nl = chr(10)
    i = 0
    while i < len(keys):
        k = keys[i]
        if k in d:
            out = out + k + "=" + d[k] + nl
        i = i + 1
    return out


def marker_begin(name):
    return chr(59) + "BEGIN pythonami-install " + name


def marker_end(name):
    return chr(59) + "END pythonami-install " + name
