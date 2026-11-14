#!/usr/bin/env python3
"""Doc checks: every ```c block in README.md, docs/usage.md and docs/wiring.md must compile,
a block with /* docs:run */ is linked, run and its stdout compared with the
following ```text block, and every mbus_/MBUS_ name used in docs/api.md must
exist in include/."""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.environ.get("BUILD", os.path.join(ROOT, "build_host"))
CC = os.environ.get("CC", "gcc")
FLAGS = ["-std=gnu99", "-Wall", "-Wextra", "-Wno-pointer-sign", "-Wno-unused-function",
         "-Werror", "-I" + ROOT + "/include", "-I" + ROOT + "/test/host/stubs",
         "-I" + ROOT + "/test/host", "-I" + ROOT + "/src"]
PRELUDE = ('#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n'
           '#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n'
           '#include "mbus.h"\n')
RUN_SRC = ["src/mbus-meter.c", "src/mbus-meter-record.c", "src/mbus-meter-frame.c",
           "src/mbus-link.c", "src/mbus-protocol.c", "src/mbus-protocol-aux.c",
           "test/host/fake_slave.c", "test/host/host_stubs.c", "test/host/host_port.c"]

fails = 0


def fail(msg):
    global fails
    fails += 1
    print("FAIL " + msg)


def blocks(path):
    """yield (lang, text, line)"""
    out, cur, lang, start = [], None, None, 0
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        m = re.match(r"^```(\w*)\s*$", line)
        if m and cur is None:
            cur, lang, start = [], m.group(1), n
        elif line.startswith("```") and cur is not None:
            out.append((lang, "".join(cur), start))
            cur = None
        elif cur is not None:
            cur.append(line)
    return out


os.makedirs(BUILD, exist_ok=True)
n_blocks = n_run = 0

for rel in ("README.md", "docs/usage.md", "docs/wiring.md"):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        fail(rel + " missing")
        continue
    bl = blocks(path)
    for i, (lang, text, line) in enumerate(bl):
        if lang != "c":
            continue
        n_blocks += 1
        src = os.path.join(BUILD, "docs_snip.c")
        run = "docs:run" in text
        body = PRELUDE + text
        if run:
            body += "\nint main(void) { offline_example(); return 0; }\n"
        open(src, "w").write(body)
        tag = "%s:%d" % (rel, line)
        if not run:
            r = subprocess.run([CC] + FLAGS + ["-fsyntax-only", src],
                               capture_output=True, text=True)
            if r.returncode:
                fail(tag + " does not compile\n" + r.stderr[:1500])
            continue
        exe = os.path.join(BUILD, "docs_run")
        r = subprocess.run([CC] + [f for f in FLAGS if f not in ("-Werror", "-Wextra")] + [src] + [os.path.join(ROOT, s) for s in RUN_SRC]
                           + ["-o", exe, "-lm"], capture_output=True, text=True)
        if r.returncode:
            fail(tag + " does not build\n" + r.stderr[:1500])
            continue
        r = subprocess.run([exe], capture_output=True, text=True, timeout=20)
        want = bl[i + 1][1] if i + 1 < len(bl) and bl[i + 1][0] == "text" else None
        if r.returncode or want is None or r.stdout.strip() != want.strip():
            fail(tag + " output differs\n--- got\n" + r.stdout + "--- want\n" + str(want))
        n_run += 1

hdr = ""
for h in glob.glob(os.path.join(ROOT, "include", "*.h")):
    hdr += open(h, encoding="utf-8", errors="replace").read()
names = set(re.findall(r"\b(?:mbus|MBUS)_[A-Za-z0-9_]+\b",
                       open(os.path.join(ROOT, "docs/api.md"), encoding="utf-8").read()))
missing = sorted(n for n in names if not re.search(r"\b" + re.escape(n) + r"\b", hdr))
if missing:
    fail("docs/api.md names not in include/: " + ", ".join(missing))

print("docs: %d c blocks compiled, %d run, %d api names" % (n_blocks, n_run, len(names)))
sys.exit(1 if fails else 0)
