#!/usr/bin/env python3
"""Checks that the BeerJSON writer's output validates against the vendored schema.

Usage: check_writer_output.py <kettlelogic_roundtrip binary> <input.json>

Reads the input through the engine, writes it back, and validates the output
against core/schemas/beerjson/beer.json. The input is not validated, because it
may deliberately contain properties the schema does not define; those must be
left out of the output. Requires the Python package `jsonschema`
(pip install jsonschema).
"""

import json
import pathlib
import subprocess
import sys

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

HERE = pathlib.Path(__file__).resolve().parent
SCHEMA_DIR = HERE.parent.parent / "schemas" / "beerjson"


def make_validator():
    registry = Registry().with_resources(
        [
            (p.name, Resource.from_contents(json.loads(p.read_text()), default_specification=DRAFT202012))
            for p in SCHEMA_DIR.glob("*.json")
        ]
    )
    root = json.loads((SCHEMA_DIR / "beer.json").read_text())
    return Draft202012Validator(root, registry=registry)


def check(validator, label, document):
    errors = sorted(validator.iter_errors(document), key=lambda e: list(e.absolute_path))
    for e in errors[:10]:
        path = "/".join(str(p) for p in e.absolute_path)
        print(f"{label}: {path}: {e.message[:200]}")
    return len(errors)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    binary, source = sys.argv[1], sys.argv[2]

    result = subprocess.run([binary, source], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"roundtrip failed: {result.stderr.strip()}")
        return 1

    validator = make_validator()
    errors = check(validator, "output", json.loads(result.stdout))

    print(f"checked the writer output for {source}: {errors} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
