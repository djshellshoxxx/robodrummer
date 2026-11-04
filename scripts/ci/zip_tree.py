#!/usr/bin/env python3
"""Write <archive>.zip holding <directory> (kept as the top-level folder).

Used by package.sh when 7z is unavailable. zipfile always writes POSIX '/'
separators, matching validate_packages.py's member-path safety check.
usage: zip_tree.py <directory> <archive.zip>
"""
from pathlib import Path
import sys
import zipfile


def zip_tree(directory, archive):
    directory = Path(directory)
    if not directory.is_dir(): raise SystemExit(f"zip_tree: not a directory: {directory}")
    archive = Path(archive)
    if archive.exists(): raise SystemExit(f"zip_tree: refusing to overwrite {archive}")
    base = directory.parent
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(directory.rglob("*")):
            if path.is_symlink(): raise SystemExit(f"zip_tree: symbolic links are not packaged: {path}")
            name = path.relative_to(base).as_posix()
            if path.is_dir(): output.writestr(name + "/", b"")
            else: output.write(path, name)


if __name__ == "__main__":
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    zip_tree(sys.argv[1], sys.argv[2])
