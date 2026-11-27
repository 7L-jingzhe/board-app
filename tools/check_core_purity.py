#!/usr/bin/env python3
"""Fail if a core module accidentally depends on LVGL.

The build already separates board_core from lvgl, but a text check gives a
fast, explicit error before compilation.

Usage:
  python3 tools/check_core_purity.py [project-root]

SPDX-License-Identifier: MIT
Copyright (c) 2026 Board App contributors
"""
import os
import re
import sys

CALL_RE = re.compile(r"\blv_[A-Za-z0-9_]+\s*\(")
INCLUDE_RE = re.compile(r'#\s*include\s*[<"](?:lvgl|lv_[^">]+)')


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..")
    root = os.path.abspath(root)
    scan_dirs = [
        os.path.join(root, "core"),
        os.path.join(root, "app"),
    ]
    violations = []
    for base in scan_dirs:
        for dirpath, dirnames, filenames in os.walk(base):
            # Vendored third-party code is allowed to be whatever it is; the
            # boundary applies to our own core modules. Skip UI/tests too.
            parts = set(dirpath.split(os.sep))
            if "third_party" in parts or "ui" in parts or "tests" in parts:
                continue
            for name in filenames:
                if not name.endswith((".c", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    text = open(path, encoding="utf-8").read()
                except OSError:
                    continue
                for lineno, line in enumerate(text.splitlines(), 1):
                    if CALL_RE.search(line) or INCLUDE_RE.search(line):
                        violations.append(f"{os.path.relpath(path, root)}:{lineno}: {line.strip()}")
    if violations:
        print("core purity violations:")
        for v in violations:
            print("  " + v)
        return 1
    print("core purity OK: no LVGL calls/includes in core modules")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
