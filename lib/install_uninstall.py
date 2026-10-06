# Write/read uninstall.ami and run uninstall (delete drawer; ed User-Startup).

import os
import install_ami
import install_startup
import install_fs


def write_uninstall_ami(install_path, name, version, start, assign_name, path_add, user_startup):
    d = {}
    d["NAME"] = name
    d["VERSION"] = version
    d["INSTALL_PATH"] = install_path
    d["START"] = start
    d["ASSIGN"] = assign_name
    if path_add:
        d["PATH_ADD"] = "1"
    else:
        d["PATH_ADD"] = "0"
    if user_startup:
        d["USER_STARTUP"] = "1"
        d["MARKER_BEGIN"] = install_ami.marker_begin(name)
        d["MARKER_END"] = install_ami.marker_end(name)
    else:
        d["USER_STARTUP"] = "0"
    text = install_ami.render_ami(d)
    path = install_fs.join_drawer(install_path, "uninstall.ami")
    f = fopen(path, "w")
    if not f:
        return (0, "cannot write uninstall.ami")
    fwrite(f, text)
    fclose(f)
    return (1, path)


def read_uninstall_ami(drawer_or_file):
    path = drawer_or_file
    if not path.endswith("uninstall.ami"):
        path = install_fs.join_drawer(drawer_or_file, "uninstall.ami")
    if not exists(path):
        return (0, {}, "uninstall.ami not found")
    f = fopen(path, "r")
    if not f:
        return (0, {}, "cannot open uninstall.ami")
    text = fread(f, 65536)
    fclose(f)
    d = install_ami.parse_ami_text(text)
    ok, msg = install_ami.validate_uninstall_ami(d)
    if not ok:
        return (0, d, msg)
    return (1, d, "")


def run_uninstall(d):
    name = install_ami.ami_get(d, "NAME", "")
    path = install_ami.ami_get(d, "INSTALL_PATH", "")
    assign_name = install_ami.ami_get(d, "ASSIGN", "")
    user_startup = install_ami.ami_get_int(d, "USER_STARTUP", 0)
    ok, rc, msg = install_fs.delete_tree(path)
    if not ok:
        return (0, "delete failed: " + msg)
    if assign_name != "":
        assign_remove(assign_name)
    if user_startup:
        os.system(install_startup.ed_user_startup_command())
    return (1, "uninstalled " + name)
