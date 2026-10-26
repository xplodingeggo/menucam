"""Copy the BakkesMod SDK headers and add symlinks for every #include whose
casing differs from the file on disk. The SDK is written on Windows, where
paths are case-insensitive."""
import os
import re
import shutil
import sys

src, dst = sys.argv[1], sys.argv[2]
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(src, dst, symlinks=True)

INCLUDE = re.compile(r'#\s*include\s*[<"]([^>"]+)[>"]')


def resolve(base, rel):
    """Find rel under base ignoring case. Returns the real path or None."""
    path = base
    for part in rel.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            path = os.path.dirname(path)
            continue
        try:
            entries = os.listdir(path)
        except OSError:
            return None
        if part in entries:
            path = os.path.join(path, part)
            continue
        match = [e for e in entries if e.lower() == part.lower()]
        if not match:
            return None
        path = os.path.join(path, match[0])
    return path


def link(base, rel, real):
    """Create the requested spelling next to the real file, directory by directory."""
    path = base
    for part in rel.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            path = os.path.dirname(path)
            continue
        want = os.path.join(path, part)
        if not os.path.exists(want):
            entries = os.listdir(path)
            match = [e for e in entries if e.lower() == part.lower()][0]
            os.symlink(match, want)
        path = want


changed = True
while changed:
    changed = False
    for root, _, files in os.walk(dst):
        for name in files:
            if not name.endswith((".h", ".hpp", ".inl")):
                continue
            here = os.path.join(root, name)
            with open(here, encoding="utf-8", errors="ignore") as f:
                text = f.read()
            for rel in INCLUDE.findall(text):
                for base in (root, dst):
                    exact = os.path.join(base, rel.replace("\\", "/"))
                    if os.path.exists(exact):
                        break
                    real = resolve(base, rel)
                    if real:
                        link(base, rel, real)
                        changed = True
                        break
