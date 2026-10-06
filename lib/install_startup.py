# User-Startup helpers for the Amiga GUI installer.
# Append/upsert marked Assign/Path blocks on install only.
# Never auto-delete User-Startup lines on uninstall.

import install_ami


def build_startup_block(name, assign_name, install_path, path_add):
    begin = install_ami.marker_begin(name)
    end = install_ami.marker_end(name)
    nl = chr(10)
    body = ""
    if assign_name != "":
        body = body + "Assign " + assign_name + ": " + install_path + nl
    if path_add:
        if assign_name != "":
            body = body + "Path " + assign_name + ": ADD" + nl
        else:
            body = body + "Path " + install_path + " ADD" + nl
    return begin + nl + body + end + nl


def upsert_startup_text(existing, name, assign_name, install_path, path_add):
    begin = install_ami.marker_begin(name)
    end = install_ami.marker_end(name)
    block = build_startup_block(name, assign_name, install_path, path_add)
    nl = chr(10)
    start = existing.find(begin)
    if start < 0:
        if existing != "" and existing[len(existing) - 1] != nl:
            existing = existing + nl
        return existing + block
    stop = existing.find(end, start)
    if stop < 0:
        if existing != "" and existing[len(existing) - 1] != nl:
            existing = existing + nl
        return existing + block
    stop = stop + len(end)
    if stop < len(existing) and existing[stop] == nl:
        stop = stop + 1
    return existing[0:start] + block + existing[stop:]


def ed_user_startup_command():
    return "ed S:User-Startup"
