#!/usr/bin/env python3
"""Where the tools find the term lists (public release Task 5).

The lists the trademark scans use live only in private modules beside this file (tools/<name>.py, such as dw_terms
and rd_terms), which the public export leaves out. A public tree is marked by a PUBLIC_TREE file at its root (written
only by the export). There a missing module means "skip that scan"; anywhere else it is a failure, as before.
A stray marker never disables a scan: a module that is present is always loaded, marker or not (and check_apps static
fails on a checkout that holds both).

NEUTRAL_MARK is the tests' neutral word: the packers refuse it in a public tree (where it is all they refuse), so the
guard tests never need a real term.
"""
import importlib.util
import os
import sys

NEUTRAL_MARK = 'zzneutralmarkzz'
MARKER = 'PUBLIC_TREE'


class TermsMissing(Exception):
    """A term module is missing outside a public tree."""


def public_tree(repo):
    """True when <repo>/PUBLIC_TREE exists (a public export)."""
    return os.path.isfile(os.path.join(repo, MARKER))


def module_path(repo, name):
    return os.path.join(repo, 'tools', name + '.py')


def load(repo, name):
    """tools/<name>.py under repo as a module (not registered in sys.modules, no bytecode written) whenever it exists;
    otherwise None in a public tree, and TermsMissing anywhere else."""
    p = module_path(repo, name)
    if not os.path.isfile(p):
        if public_tree(repo):
            return None
        raise TermsMissing('tools/%s.py is missing (and %s holds no %s marker: not a public tree)' % (name, repo, MARKER))
    spec = importlib.util.spec_from_file_location(name, p)
    mod = importlib.util.module_from_spec(spec)
    keep, sys.dont_write_bytecode = sys.dont_write_bytecode, True   # no tools/__pycache__ left behind
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.dont_write_bytecode = keep
    return mod
