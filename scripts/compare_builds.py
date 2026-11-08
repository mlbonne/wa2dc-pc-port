#!/usr/bin/env python3
"""Compare two WA2DC build folders file-by-file (SHA-256).

Usage:
    python -I compare_builds.py --reference <build\\WA2DC> --candidate <other\\WA2DC>
        [--exclude-root-png] [--report <tsv>]

Runtime/evidence files are excluded by default: root-level PNGs (screenshots),
root Documents/, WA2DC_Data/output_log.txt, port-shot requests/results.
"""
import argparse
import hashlib
import os

DEFAULT_FILE_EXCLUDES = {
    "WA2DC_Data/output_log.txt",
}
DEFAULT_DIR_EXCLUDES = {
    "Documents",
}


def relative_files(root, exclude_root_png):
    out = {}
    for base, dirs, names in os.walk(root):
        rel_base = os.path.relpath(base, root)
        if rel_base == ".":
            dirs[:] = [d for d in dirs if d not in DEFAULT_DIR_EXCLUDES]
        for name in names:
            rel = os.path.normpath(os.path.join(rel_base, name)) if rel_base != "." else name
            rel = rel.replace(os.sep, "/")
            if rel in DEFAULT_FILE_EXCLUDES:
                continue
            if rel.startswith("port-shot") or rel.endswith(".request"):
                continue
            if exclude_root_png and rel_base == "." and name.lower().endswith(".png"):
                continue
            out[rel] = os.path.join(base, name)
    return out


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--exclude-root-png", action="store_true")
    parser.add_argument("--report", default="")
    args = parser.parse_args()
    reference = relative_files(os.path.abspath(args.reference), args.exclude_root_png)
    candidate = relative_files(os.path.abspath(args.candidate), args.exclude_root_png)

    only_ref = sorted(set(reference) - set(candidate))
    only_cand = sorted(set(candidate) - set(reference))
    common = sorted(set(reference) & set(candidate))
    differing = []
    for rel in common:
        a = sha256(reference[rel])
        b = sha256(candidate[rel])
        if a != b:
            differing.append((rel, os.path.getsize(reference[rel]), os.path.getsize(candidate[rel]), a, b))

    print("reference files:", len(reference))
    print("candidate files:", len(candidate))
    print("same:", len(common) - len(differing))
    print("only-reference:", len(only_ref))
    for rel in only_ref:
        print("  -", rel)
    print("only-candidate:", len(only_cand))
    for rel in only_cand:
        print("  +", rel)
    print("different:", len(differing))
    for rel, size_a, size_b, hash_a, hash_b in differing:
        print("  ~ {0} ({1} vs {2} bytes)".format(rel, size_a, size_b))

    if args.report:
        with open(args.report, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("status\tpath\treference_bytes\tcandidate_bytes\treference_sha256\tcandidate_sha256\n")
            for rel in only_ref:
                handle.write("only-reference\t{0}\t{1}\t\t{2}\t\n".format(rel, os.path.getsize(reference[rel]), sha256(reference[rel])))
            for rel in only_cand:
                handle.write("only-candidate\t{0}\t\t{1}\t\t{2}\n".format(rel, os.path.getsize(candidate[rel]), sha256(candidate[rel])))
            for rel, size_a, size_b, hash_a, hash_b in differing:
                handle.write("different\t{0}\t{1}\t{2}\t{3}\t{4}\n".format(rel, size_a, size_b, hash_a, hash_b))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
