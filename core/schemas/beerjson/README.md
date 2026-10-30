# Vendored BeerJSON schema

Source: <https://github.com/beerjson/beerjson>, directory `json/`
Pinned commit: `3aedaf77352fdc56db60c536926a1ad8302b045b` (committed 2026-10-22)
License: see `LICENSE` in this directory (copied from the upstream repository)

Files are copied unmodified. To update, re-download from a new pinned commit, diff the result, and update the commit above in the same change.

Entry point: `beer.json`. Shared unit and measurement types: `measureable_units.json`.

Root constraints worth knowing: `beerjson` has `additionalProperties: false`, and `version` is a number from `VersionType` (`2.01`, `2.06`, `3`). See [beerjson-profile.md](../../../docs/formats/beerjson-profile.md).
