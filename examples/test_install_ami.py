# Host-checkable install.ami / User-Startup upsert tests (no Amiga DOS).
import sys
sys.path.append("lib")
import install_ami
import install_startup

nl = chr(10)
semi = chr(59)
text = "NAME=DemoApp" + nl + "VERSION=1.0" + nl + "START=DemoApp" + nl + "DRAWER_NAME=DemoApp" + nl + "ASSIGN=DemoApp" + nl + "PATH_ADD=1" + nl + "USER_STARTUP=1" + nl
d = install_ami.parse_ami_text(text)
ok, msg = install_ami.validate_install_ami(d)
print("parse_ok", ok)
print("name", install_ami.ami_get(d, "NAME", ""))
print("path_add", install_ami.ami_get_int(d, "PATH_ADD", 0))

existing = semi + " other stuff" + nl
updated = install_startup.upsert_startup_text(existing, "DemoApp", "DemoApp", "DH0:DemoApp", 1)
begin = semi + "BEGIN pythonami-install DemoApp"
print("has_begin", updated.find(begin) >= 0)
print("has_assign", updated.find("Assign DemoApp: DH0:DemoApp") >= 0)
updated2 = install_startup.upsert_startup_text(updated, "DemoApp", "DemoApp", "DH1:DemoApp", 1)
count = 0
pos = 0
while 1:
    p = updated2.find(begin, pos)
    if p < 0:
        break
    count = count + 1
    pos = p + 1
print("begin_count", count)
print("new_path", updated2.find("DH1:DemoApp") >= 0)
print("ed_cmd", install_startup.ed_user_startup_command())
