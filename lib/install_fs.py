# Filesystem helpers for the Amiga GUI installer.

import os


def join_drawer(parent, leaf):
    if parent == "":
        return leaf
    last = parent[len(parent) - 1]
    if last == ":" or last == "/":
        return parent + leaf
    return parent + "/" + leaf


def make_drawer(path):
    if exists(path):
        return (1, 0, "exists")
    rc = os.system('makedir "' + path + '"')
    if rc == 0:
        return (1, rc, "ok")
    return (0, rc, "makedir failed")


def delete_tree(path):
    if not exists(path):
        return (1, 0, "missing")
    rc = os.system('delete "' + path + '" all quiet')
    if rc == 0:
        return (1, rc, "ok")
    rc = os.system('delete "' + path + '" all')
    if rc == 0:
        return (1, rc, "ok")
    return (0, rc, "delete failed")


def path_exists(path):
    if exists(path):
        return 1
    return 0
