# Kettle Logic BeerJSON Profile

Status: draft 0.2. This document specifies how Kettle Logic reads and writes BeerJSON. The upstream schema, vendored at [core/schemas/beerjson/](../../core/schemas/beerjson/) (pinned to commit `3aedaf77352fdc56db60c536926a1ad8302b045b`), is authoritative for structure. Where this profile is stricter or adds behavior, this document governs Kettle Logic's output.

## 1. Scope

- **Read:** any BeerJSON document that validates against the vendored schema. Its `version` must be one the schema accepts (see section 2).
- **Write:** Kettle Logic always writes documents that validate against the vendored schema. Kettle Logic's own data never goes inside the BeerJSON document (section 8).
- **Not in scope:** BrewJSON or other derivative schemas, BeerProto, and BeerXML (see [beerxml-import.md](beerxml-import.md)).

## 2. Document shape

The root object has one required property, `beerjson`, and no other properties (`additionalProperties: false`). Inside `beerjson`:

| Property | Contents |
|---|---|
| `version` | **required**; a number, not a string (see below) |
| `fermentables` | array |
| `miscellaneous_ingredients` | array |
| `hop_varieties` | array |
| `cultures` | array |
| `profiles` | water profiles |
| `styles` | array |
| `mashes` | array |
| `fermentations` | array |
| `recipes` | array |
| `equipments` | array |
| `boil` | array |
| `packaging` | array |

`beerjson` also forbids extra properties, so no collection outside this list can be added.

### 2.1 Version

`version` is a number from the upstream `VersionType` enum: `2.01`, `2.06`, or `3`. The schema accepts its own version and the earlier ones it can still validate. JSON has no decimal type, so version 3.0 is written as `3`. Kettle Logic writes the highest version it implements. **Confirm which version the vendored schema targets** before the reader is written; the schema's own `version` text describes the format, and it should be checked against the upstream README.

## 3. Quantities

A measured value is an object with exactly two required properties and no others:

```json
{ "value": 1.1, "unit": "lb" }
```

- `value` is a JSON number. `unit` is a string from the enum for the quantity's dimension.
- `additionalProperties` is `false`. Unknown properties in a quantity are a validation error.
- `TimeType` values must be integers.

### 3.1 Units

These enums come from `measureable_units.json` in the vendored schema.

| Dimension | Enum | Values |
|---|---|---|
| Mass | `MassUnitType` | `mg`, `g`, `kg`, `lb`, `oz` |
| Volume | `VolumeUnitType` | `ml`, `l`, `tsp`, `tbsp`, `floz`, `cup`, `pt`, `qt`, `gal`, `bbl`, `ifloz`, `ipt`, `iqt`, `igal`, `ibbl` |
| Temperature | `TemperatureUnitType` | `C`, `F` |

Exact definitions for every unit are in [units.md](../units.md). Notes:

- `bbl` is the US beer barrel: **31 US gallons** (confirmed).
- `floz` is the US fluid ounce. `ifloz` is the imperial fluid ounce. They differ, so don't assume one for the other.

### 3.2 Other measurable types

Kettle Logic follows the upstream `$defs` for the other quantity types: `DiastaticPowerType`, `PressureType`, `AcidityType`, `TimeType`, `ColorType`, `CarbonationType`, `BitternessType`, `GravityType`, `SpecificHeatType`, `ConcentrationType`, `SpecificVolumeType`, `ViscosityType`, and the range types. Their structure is the same `{ value, unit }` form. Upstream's unit enums for each are authoritative.

## 4. Exact number handling

JSON numbers are the one place where a float could sneak in, so Kettle Logic reads them specially.

1. Read the number's **lexeme** (its original text) from the parser. Do not convert it to `double` first.
2. Parse the lexeme into an exact rational with `Rational::fromDecimal`. `1.1` becomes `11/10`.
3. Convert to canonical units exactly (see [units.md](../units.md)).

A JSON parser that only exposes `double` values breaks this rule. The Kettle Logic JSON reader must expose lexemes for numeric tokens.

Writing follows the reverse process:

- **Values are written as exact minimal decimals.** The reader keeps every value as an exact rational, and the writer emits it in the same unit with no trailing zeros. So `1.10` is written as `1.1`. The two are numerically equal, and a schema-valid file reads back to the same model. Byte-for-byte lexeme preservation was considered and rejected: it would need a second copy of every number, and it would never match an edited value anyway.
- **Edited values are written in the unit the user chose,** rounded to that unit's export precision. Export precision is 6 significant digits for `lb`, `oz`, `kg`, `g`, `l`, `ml`, and `gal`, and 2 decimal places for `C` and `F`. These are defaults; they're configurable in the export options.
- **Repeating decimals are rounded at export.** Storage stays exact. The sidecar file (section 8) records each rounded field so the loss is visible.

## 5. Units on export

Kettle Logic exports each quantity in the unit it was authored in. If the user has no preference, mass uses `kg` (or `g` for small weights), volume uses `l` (or `ml` for small volumes), and temperature uses `C`. Imperial and US units stay exact in storage and convert on export.

## 6. Identity

Upstream has **no identifier properties**. Entities are nested inline, so nothing in the schema gives them a stable key. Kettle Logic gives each entity a UUID (version 7, RFC 9562, time-ordered).

- Entities with UUIDs: recipes, fermentable, hop, miscellaneous, culture, and water additions.
- UUIDs live in the sidecar (section 8), not in the BeerJSON file.
- Matching on open, in order: the stored entity with the same kind and a unique name (this survives insertions and moves); failing that, the same kind at the same JSON Pointer (this survives renames); otherwise the entity gets a fresh UUID.
- Each foreign property records the UUID of the entity that contains it, and the path from that entity to its parent. Identity therefore follows the entity, not the file layout.
- Undo, redo, and in-app references use UUIDs or session ids, never pointers.

## 7. Validation

Kettle Logic validates on read and on write against the vendored schema. A file that fails validation can be opened in a read-only "repair" mode, which reports every error with its JSON pointer. The user can then choose to repair or cancel.

Status of what is implemented:

- **Reader** (`readBeerJson`) enforces the structural rules for every typed property: required properties, value types, enum values, unit dimensions, integer-valued times, the quantity shape (`additionalProperties: false`), and the non-empty `yield` rule. Every violation is a `ReadError` with a JSON Pointer.
- **Unknown properties are split by what the schema says.** The generated schema table (`core/include/kettlelogic/model/schema.hpp`) lists each object's defined properties and whether the schema closes the object.
  - A property the schema defines but the model does not type yet (for example `style` or `taste`) is kept in `preserved` and written back. The output stays valid.
  - A property the schema does not define, in an object it closes (for example `"bogus"` in a recipe), is recorded as a warning in `Document::diagnostics` and kept in `Document::foreign`. It is not written to the BeerJSON file, because writing it would make the file invalid. The sidecar (section 8) is where it will be saved.
  - A property in an open object (water additions) is always written back, because the schema allows it.
- Full schema validation is a separate check. The reader does not reject these files; it reports them.
- **Writer** (`writeBeerJson`) output is checked against the vendored schema in development. An optional ingredient list is written exactly when it is present, so a present but empty list is written as `[]`. Boil steps follow the same rule.
- **Version** is read as a number and must be one of `2.01`, `2.06`, or `3`. Documents are checked against the current vendored schema for every version. Differences between older versions are not yet modeled.

## 8. Sidecar file

Upstream forbids extra properties on `beerjson`, and most typed objects are closed to properties the schema does not define. So Kettle Logic keeps those properties, and the UUIDs of its entities, in a sidecar file. The sidecar is never written into the BeerJSON document.

```
My Recipe.json              BeerJSON document (validates against upstream)
My Recipe.kettlelogic.json  sidecar: entity UUIDs and foreign properties
```

Format (schema version 7):

```json
{
  "schema": 7,
  "beerJsonSha256": "<64 hex characters>",
  "entities": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f50", "kind": "recipe",
      "pointer": "/beerjson/recipes/0", "name": "Sample Pale Ale" }
  ],
  "foreign": [
    { "owner": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f50", "path": "",
      "key": "bogus_recipe_key", "parentName": null, "value": 1 }
  ],
  "recipeSettings": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f50",
      "boilOff": { "evaporationPercentPerHour": 10, "trubLossLiters": 1 },
      "fermentationEfficiencyPercent": 92,
      "folderUuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f53",
      "bjcpStyle": "21A",
      "currency": "USD" }
  ],
  "folders": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f53", "name": "Summer" }
  ],
  "saltTypes": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f51", "salt": "gypsum" }
  ],
  "additiveRoles": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f52", "additive": "sulfite" }
  ],
  "prices": [
    { "uuid": "0195e6f0-3c4a-7d2b-9a1f-6c3e8b2d4f54", "amount": 4.5, "per": "kg" }
  ]
}
```

- `entities` lists every entity with its kind (`recipe`, `fermentable`, `hop`, `miscellaneous`, `culture`, `water`), its pointer and name at save time.
- `foreign` entries link to their owner by `owner` (a UUID). `path` is the parent's pointer relative to the owner (`""` for the owner itself, `"/batch_size"` for a quantity inside it). When `owner` is `null`, `path` is an absolute pointer and `parentName` is used to check it.
- On open, an entry is restored when its owner is in the document and the key is still absent. If the owner is gone, or the key is now defined, the entry is still kept, with a diagnostic, so saving does not drop it.
- `recipeSettings` holds each recipe's boil-off (`model::BoilOff`), in fixed units: a percentage of the pre-boil volume per hour, and liters of trub loss. BeerJSON has no place for these: its equipment list is not linked to recipes. Every recipe is written, so the defaults are visible too.
- `recipeSettings` also holds each recipe's fermentation efficiency, in percent, above 0 and up to 100. It's used to plan drinks.
- `recipeSettings` holds each recipe's folder, by the folder's UUID (`folderUuid`, or `null` for the top level), and its BJCP style, by guideline id (`bjcpStyle`, or `null`). BeerJSON has no place for either: the app's own organization and comparison choice.
- `folders` lists the recipe folders in the order the user made them, each with its UUID and name. A recipe's `folderUuid` that matches no folder is ignored, with a diagnostic, and the recipe stays at the top level. The Recently Deleted folder, which holds the recipes the user deleted, has `"trash": true`. Other folders omit the key. Older readers ignore it and see an ordinary folder.
- `userStyles` lists the beer styles the user defined, in the order they were made. Each has an `id` that begins with `custom-`, a `name`, and a `{"low", "high"}` range for `og`, `fg`, `ibu`, `srm`, and `abv`. A recipe's `bjcpStyle` may name one of these ids. The key is written only when there are user styles, and a document with none is still written as schema 7. A schema 8 sidecar is the only one with `userStyles`.
- Schema 6 sidecars, written before folders and styles existed, still open. They have no `folders` list, and their recipes start at the top level with no style.
- `saltTypes` holds the salt type (`gypsum`, `calcium chloride`, `epsom salt`, or `baking soda`) of each miscellaneous ingredient that has one, by UUID. The name is never used to decide a salt. An ingredient with no salt type has no entry.
- `additiveRoles` holds what a miscellaneous ingredient does in a drink, where BeerJSON cannot say: `sulfite` or `tartaric acid`. An ingredient with no role has no entry.
- `prices` holds what a fermentable, hop, miscellaneous ingredient, or culture costs, by UUID: `amount` for each one `per`, where `per` is a BeerJSON unit name that measures the same thing as the ingredient's amount (a price per `kg` for an amount in `lb`, a price per `pkg` for a count). BeerJSON has no price field. An ingredient with no price has no entry, and water is never priced.
- `recipeSettings` also holds each recipe's `currency`, the three-letter code every price in the recipe is in. The engine never converts between currencies. It is optional: a sidecar written before prices existed has no `currency` or `prices`, and opens with `USD` and no prices, so the sidecar schema stays at 7.
- On open, a setting whose entity is no longer in the document is ignored, with a diagnostic.
- `beerJsonSha256` is the SHA-256 of the BeerJSON text the sidecar was written with. A pair whose hashes do not match was caught mid-save, and it is not used as it stands (see `docs/model.md`, Files).
- The sidecar is written on every save, because it holds entity identity. A document with no undefined properties still gets one.
- An unsupported `schema` version, or a malformed entry, is an error when opened.

Rules:

- The BeerJSON document stays valid on its own. Other tools ignore the sidecar.
- Properties the schema defines but the model does not type yet are not in the sidecar. They stay in the BeerJSON file, in `preserved`.
- Not implemented yet: UI state and export precision choices. The sidecar will grow to hold them, under a later schema version.
- A drink's kind is BeerJSON's recipe `type` (`cider`, `mead`, `wine`, and the others), so it is in the BeerJSON file, not the sidecar. Its ingredients are BeerJSON fermentables: `juice` and `fruit` by volume with a gravity yield, `honey` and `sugar` by mass with a percent yield.
- Write the document and sidecar together, through a temporary file and an atomic rename, so a crash cannot leave them out of step. This belongs to the file layer, not the model.

## 9. Conformance tests

The test suite must include:

- Round-trip: read → write produces a file that validates against the vendored schema and is structurally equal to the input (JSON comparison, order-insensitive). This is implemented in `core/tests/test_model.cpp`.
- Exactness: `"value": 1.1, "unit": "lb"` reads as `11/10 lb` and writes back as `1.1`.
- Schema conformance: every output file validates against `core/schemas/beerjson/beer.json`.
- Sidecar: removing the sidecar still opens the document; a document edited externally re-matches entities by fingerprint.
- Unknown-field preservation.
- Each unit enum value parses and converts.
- At least one real file exported by BeerSmith, and one by another BeerJSON tool, for interoperability.

## Open items

1. Confirm the schema version target (section 2.1) against the upstream README.
2. Decide the fingerprint content (section 6) once the entity models exist.
3. Collect sample BeerJSON files from other tools for the conformance suite.
