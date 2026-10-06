# LHA extract helpers for the Amiga GUI installer (shell out to lha).

import os


def lha_extract(archive_path, dest_drawer):
    # Returns (ok, rc, message). ok is 1 on DOS RETURN_OK (0).
    cmd = 'lha x -n "' + archive_path + '" "' + dest_drawer + '"'
    rc = os.system(cmd)
    if rc == 0:
        return (1, rc, "ok")
    return (0, rc, "lha failed with code " + str(rc))


def lha_available():
    rc = os.system(" whichtool lha >NIL: ")
    if rc == 0:
        return 1
    rc = os.system(" which lha >NIL: ")
    if rc == 0:
        return 1
    # Best-effort: try running lha with no args (often prints usage, rc may be non-zero).
    rc = os.system("lha >NIL:")
    if rc == 0 or rc == 5 or rc == 10:
        return 1
    return 0
