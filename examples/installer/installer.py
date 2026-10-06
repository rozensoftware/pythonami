# Amiga GUI installer (pythonami). HD only. Extracts an LHA, writes uninstall.ami,
# optionally patches S:User-Startup. Uninstall deletes the drawer; User-Startup
# is offered in ed (never auto-rewritten).
#
# Amiga:
#   pythonami installer.py
# Requires PROGDIR:ext/gui_intuition/gui_intuition.py68k and PROGDIR:ext/asl/asl.py68k
# and `lha` on PATH.

import sys
sys.path.append("lib")
import os
import install_ami
import install_startup
import install_lha
import install_fs
import install_uninstall

GUI_PLUGIN = "PROGDIR:ext/gui_intuition/gui_intuition.py68k"
ASL_PLUGIN = "PROGDIR:ext/asl/asl.py68k"

GUI_EVT_CLOSE = 1
GUI_EVT_BUTTON = 2

ID_LBL_TITLE = 1
ID_LBL_STATUS = 2
ID_EDIT_PATH = 3
ID_BTN_BROWSE = 4
ID_BTN_INSTALL = 5
ID_BTN_UNINSTALL = 6
ID_BTN_QUIT = 7
ID_PRG = 8
ID_CHK_ASSIGN = 9
ID_CHK_PATH = 10
ID_CHK_STARTUP = 11

WIN_X = 40
WIN_Y = 30
WIN_W = 360
WIN_H = 180
FORM_IDCMP = 2097772
FORM_FLAGS = 4110

PACKAGE_LHA = "demo_package.lha"
PACKAGE_AMI = "demo_package/install.ami"


def load_package_info():
    path = PACKAGE_AMI
    if not exists(path):
        return (0, {}, "missing " + path)
    f = fopen(path, "r")
    if f == 0:
        return (0, {}, "cannot read install.ami")
    text = fread(f, 65536)
    fclose(f)
    d = install_ami.parse_ami_text(text)
    ok, msg = install_ami.validate_install_ami(d)
    if not ok:
        return (0, d, msg)
    return (1, d, "")


def set_status(gui, text):
    gui.set_label_text(ID_LBL_STATUS, text)


def do_install(gui, asl, info):
    parent = gui.get_edit_text(ID_EDIT_PATH)
    if parent == "":
        set_status(gui, "Choose an install drawer")
        return
    name = install_ami.ami_get(info, "NAME", "")
    version = install_ami.ami_get(info, "VERSION", "")
    start = install_ami.ami_get(info, "START", "")
    drawer_name = install_ami.ami_get(info, "DRAWER_NAME", name)
    assign_name = install_ami.ami_get(info, "ASSIGN", "")
    path_add = gui.get_checkbox(ID_CHK_PATH)
    user_startup = gui.get_checkbox(ID_CHK_STARTUP)
    if gui.get_checkbox(ID_CHK_ASSIGN) == 0:
        assign_name = ""
    target = install_fs.join_drawer(parent, drawer_name)
    gui.enable_widget(ID_BTN_INSTALL, 0)
    gui.enable_widget(ID_BTN_UNINSTALL, 0)
    set_status(gui, "Creating drawer...")
    gui.set_progress(ID_PRG, 10)
    ok, rc, msg = install_fs.make_drawer(target)
    if not ok:
        set_status(gui, "makedir failed")
        gui.enable_widget(ID_BTN_INSTALL, 1)
        gui.enable_widget(ID_BTN_UNINSTALL, 1)
        return
    set_status(gui, "Extracting LHA...")
    gui.set_progress(ID_PRG, 40)
    if not exists(PACKAGE_LHA):
        set_status(gui, "missing " + PACKAGE_LHA)
        gui.enable_widget(ID_BTN_INSTALL, 1)
        gui.enable_widget(ID_BTN_UNINSTALL, 1)
        return
    ok, rc, msg = install_lha.lha_extract(PACKAGE_LHA, target)
    if not ok:
        set_status(gui, msg)
        gui.enable_widget(ID_BTN_INSTALL, 1)
        gui.enable_widget(ID_BTN_UNINSTALL, 1)
        return
    set_status(gui, "Writing uninstall.ami...")
    gui.set_progress(ID_PRG, 70)
    w_ok = install_uninstall.write_uninstall_ami(target, name, version, start, assign_name, path_add, user_startup)
    ok = w_ok[0]
    umsg = w_ok[1]
    if ok == 0:
        set_status(gui, umsg)
        gui.enable_widget(ID_BTN_INSTALL, 1)
        gui.enable_widget(ID_BTN_UNINSTALL, 1)
        return
    if assign_name != "":
        assign_add(assign_name, target)
    if user_startup != 0:
        set_status(gui, "Updating User-Startup...")
        gui.set_progress(ID_PRG, 85)
        path = "S:User-Startup"
        existing = ""
        if exists(path):
            f = fopen(path, "r")
            if f:
                existing = fread(f, 65536)
                fclose(f)
        new_text = install_startup.upsert_startup_text(existing, name, assign_name, target, path_add)
        f = fopen(path, "w")
        if f:
            fwrite(f, new_text)
            fclose(f)
    gui.set_progress(ID_PRG, 100)
    set_status(gui, "Installed to " + target)
    gui.enable_widget(ID_BTN_INSTALL, 1)
    gui.enable_widget(ID_BTN_UNINSTALL, 1)


def do_uninstall(gui, asl):
    drawer = asl.ask_drawer("Installed app drawer", gui.get_edit_text(ID_EDIT_PATH))
    if drawer == "":
        set_status(gui, "Uninstall cancelled")
        return
    ok, d, msg = install_uninstall.read_uninstall_ami(drawer)
    if not ok:
        set_status(gui, msg)
        return
    set_status(gui, "Removing " + install_ami.ami_get(d, "INSTALL_PATH", ""))
    gui.set_progress(ID_PRG, 50)
    ok, msg = install_uninstall.run_uninstall(d)
    if not ok:
        set_status(gui, msg)
        return
    gui.set_progress(ID_PRG, 100)
    set_status(gui, msg)


def main():
    ok, info, msg = load_package_info()
    if not ok:
        print(msg)
        return 20
    gui = load_library(GUI_PLUGIN)
    asl = load_library(ASL_PLUGIN)
    if gui.init() != 0:
        return 20
    title = install_ami.ami_get(info, "NAME", "Installer")
    status = gui.begin_window(title + " Installer", WIN_X, WIN_Y, WIN_W, WIN_H, FORM_IDCMP, FORM_FLAGS)
    if status != 0:
        gui.shutdown()
        return 20
    gui.add_label(ID_LBL_TITLE, 12, 20, title + " " + install_ami.ami_get(info, "VERSION", ""))
    gui.add_label(ID_LBL_STATUS, 12, 148, "Ready")
    gui.add_editbox(ID_EDIT_PATH, 12, 36, 240, 14, "SYS:", 96)
    gui.add_button(ID_BTN_BROWSE, 260, 34, 80, 18, "Browse")
    gui.add_checkbox(ID_CHK_ASSIGN, 12, 56, 100, 14, "ASSIGN", 1)
    gui.add_checkbox(ID_CHK_PATH, 120, 56, 80, 14, "PATH", 1)
    gui.add_checkbox(ID_CHK_STARTUP, 210, 56, 120, 14, "User-Startup", 1)
    gui.add_progress(ID_PRG, 12, 78, 330, 12, 0)
    gui.add_button(ID_BTN_INSTALL, 12, 100, 88, 18, "Install")
    gui.add_button(ID_BTN_UNINSTALL, 110, 100, 88, 18, "Uninstall")
    gui.add_button(ID_BTN_QUIT, 260, 100, 80, 18, "Quit")
    win = gui.show()
    if win == 0 or win == -1:
        gui.shutdown()
        return 20
    running = 1
    while running != 0:
        evt = gui.wait_event()
        eid = gui.get_event_id()
        if evt == GUI_EVT_CLOSE:
            running = 0
        if evt == GUI_EVT_BUTTON:
            if eid == ID_BTN_QUIT:
                running = 0
            if eid == ID_BTN_BROWSE:
                drawer = asl.ask_drawer("Install into", gui.get_edit_text(ID_EDIT_PATH))
                if drawer != "":
                    gui.set_edit_text(ID_EDIT_PATH, drawer)
            if eid == ID_BTN_INSTALL:
                do_install(gui, asl, info)
            if eid == ID_BTN_UNINSTALL:
                do_uninstall(gui, asl)
    gui.close_window()
    gui.shutdown()
    return 0


if __name__ == "__main__":
    exit(main())
