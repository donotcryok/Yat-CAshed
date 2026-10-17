#!/usr/bin/env python3
"""
Normalize CRLF -> LF across a working tree.

Safety:
  - Files containing a NUL byte in the first 8 KiB are treated as binary and skipped.
  - Files with no CR at all are skipped (no rewrite, keeps mtime stable).
  - Only the working tree is touched; staging/index is handled by `git add --renormalize`.

Usage:
  python3 scripts/normalize_eol.py <root> [--dry-run]
"""
import os
import sys
import multiprocessing as mp

SKIP_DIRS = {'.git', 'node_modules', '__pycache__', '.venv', 'venv'}
SKIP_EXT = {'.jar', '.class', '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.ico',
            '.pdf', '.pptx', '.docx', '.xlsx', '.zip', '.gz', '.xz', '.bz2',
            '.7z', '.tar', '.o', '.a', '.so', '.exe', '.dll', '.woff', '.woff2',
            '.ttf', '.otf', '.ttc', '.eot', '.pyc', '.bin', '.dtb'}


def conv(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in SKIP_EXT:
        return None
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except (OSError, PermissionError):
        return None

    if b'\x00' in data[:8192]:
        return 'binary'
    if b'\r' not in data:
        return None

    new = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    if new == data:
        return None
    try:
        with open(path, 'wb') as f:
            f.write(new)
        return 'converted'
    except (OSError, PermissionError):
        return 'failed'


def walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            yield os.path.join(dirpath, name)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    dry = '--dry-run' in sys.argv

    paths = list(walk(root))
    print(f'scanned {len(paths)} files under {root}', flush=True)
    if dry:
        hits = [p for p in paths if conv(p) is None and b'\r' in open(p, 'rb').read()[:65536]]
        print(f'[dry-run] {len(hits)} files contain CR')
        return 0

    with mp.Pool(min(12, os.cpu_count() or 4)) as pool:
        results = pool.map(conv, paths, chunksize=200)

    converted = sum(1 for r in results if r == 'converted')
    binary = sum(1 for r in results if r == 'binary')
    failed = sum(1 for r in results if r == 'failed')
    print(f'DONE: converted={converted} binary_skipped={binary} failed={failed}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
