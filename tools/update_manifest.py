#!/usr/bin/env python3
"""Sets one platform's entry in updates.json, the update list that the Kettle Logic apps read.

  update_manifest.py <manifest> <entry> --version 0.2.0 --url <address> [--file <download>] [--extra name=value]

<entry> is one of: app, engine (macOS), windows, linux. The file is created if it does not exist, and the other
entries are kept, so each platform's release can update its own entry in the same list. An entry with --file gets
the file's SHA-256. The apps check that hash before they run a download, so a download that has changed after the
list was written is refused. --extra adds a field, such as engineAbi=1 for the macOS app; a number is written as a
number.

See docs/updates.md for the format and the release steps.
"""

import argparse
import hashlib
import json
import pathlib
import re
import sys

ENTRIES = ("app", "engine", "windows", "linux")
VERSION = re.compile(r"^\d{1,6}(\.\d{1,6}){0,2}$")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_extra(text):
    name, _, value = text.partition("=")
    if not name or not _:
        raise argparse.ArgumentTypeError(f"--extra needs name=value, not '{text}'")
    return name, int(value) if value.isdigit() else value


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("entry", choices=ENTRIES)
    parser.add_argument("--version", required=True)
    parser.add_argument("--url", required=True)
    parser.add_argument("--file", type=pathlib.Path, help="the download, whose SHA-256 is recorded")
    parser.add_argument("--extra", type=parse_extra, action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args(argv)

    if not VERSION.match(args.version):
        parser.error(f"'{args.version}' is not a version such as 0.2.0")
    if not args.url.startswith("https://"):
        parser.error("the address must start with https://")

    manifest = {"schema": 1}
    if args.manifest.exists():
        manifest = json.loads(args.manifest.read_text())
        if manifest.get("schema") != 1:
            parser.error(f"{args.manifest} has schema {manifest.get('schema')}, which this tool does not write")

    entry = {"version": args.version, "url": args.url}
    if args.file is not None:
        if not args.file.is_file():
            parser.error(f"{args.file} is not a file")
        entry["sha256"] = sha256_of(args.file)
    entry.update(dict(args.extra))
    manifest[args.entry] = entry

    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Set {args.entry} {args.version} in {args.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
