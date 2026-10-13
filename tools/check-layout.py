#!/usr/bin/env python3
"""Compare layout_*/offset_* probe symbols produced by host and target compilers.

A probe declares char layout_type[sizeof(type)] and
char offset_type_field[offsetof(type, field) + 1] for each serialized item.
Only compare types/configurations actually used by the data writer and reader.
"""
import argparse
import subprocess


def sizes(nm, path):
    result = {}
    for line in subprocess.check_output([nm, "-S", path], text=True).splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3].startswith(("layout_", "offset_")):
            result[parts[3]] = int(parts[1], 16)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-object", required=True)
    parser.add_argument("--target-object", required=True)
    parser.add_argument("--host-nm", default="nm")
    parser.add_argument("--target-nm", required=True)
    args = parser.parse_args()
    host = sizes(args.host_nm, args.host_object)
    target = sizes(args.target_nm, args.target_object)
    if not host or host != target:
        differences = [f"{key}: host={host.get(key)}, target={target.get(key)}"
                       for key in sorted(host.keys() | target.keys()) if host.get(key) != target.get(key)]
        parser.exit(1, "data layout check failed: " + ("; ".join(differences) or "no probe symbols") + "\n")
    print(f"PASS: {len(host)} serialized structure sizes/offsets match target")


if __name__ == "__main__":
    main()
