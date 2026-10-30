# Model Layer

Status: first slice. The recipe graph is typed and round-trips. Catalog records, style, packaging, and tasting are not typed yet.

## Layers

```
BeerJSON text  ──readBeerJson──▶  model::Document  ──writeBeerJson──▶  BeerJSON text
                                       │
                                       ▲ edits, via CommandStack<Document>
                                       │
                               model commands (commands.hpp)
```

The JSON layer (`kettlelogic/json.hpp`) sits underneath. It parses strictly, rejects duplicate keys, and represents numbers as exact rationals, so no value passes through a `double`.

## Public interface

Header: `kettlelogic/model/beerjson.hpp`

| Function | Behavior |
|---|---|
| `Document readBeerJson(std::string_view)` | Parses and maps a BeerJSON document. Throws `json::ParseError` for malformed JSON and `ReadError` for a schema violation. |
| `std::string writeBeerJson(const Document&)` | Writes pretty-printed BeerJSON. Output validates against the vendored schema. Foreign properties are not written. |

Header: `kettlelogic/model/sidecar.hpp`

| Function | Behavior |
|---|---|
| `Saved saveDocument(const Document&)` | Returns the BeerJSON text and the sidecar text. The sidecar is always written, because it holds entity UUIDs. |
| `Document openDocument(std::string_view beerJson, std::optional<std::string_view> sidecar)` | Reads the BeerJSON, matches entities to their stored UUIDs, then restores the foreign properties to their owners. An entry whose owner is gone, or whose place no longer fits, is kept with a diagnostic. |

`ReadError::pointer()` is the RFC 6901 JSON Pointer of the offending value, such as `/beerjson/recipes/0/batch_size/unit`. An empty pointer means the document root.

Header: `kettlelogic/model/model.hpp`

- `EntityId` (`std::uint64_t`) identifies an entity for the life of a session. The `Document` allocates ids, and they are never reused. Commands refer to entities by id, never by pointer or index. Ids are not written to the file. Each entity also has a `Uuid` (`kettlelogic/uuid.hpp`), which the sidecar stores, so identity survives saves and reloads (see [beerjson-profile.md](formats/beerjson-profile.md), section 6). Commands that add an entity assign its UUID on first apply, so redo keeps it.
- `Document` holds `version`, `recipes`, and `preserved`, the root-level keys the model does not type.

## Typed and preserved

Every model struct has a `preserved` list. The reader puts into it each key it does not type, in its original order, and the writer writes it back after the typed keys. A read-then-write therefore keeps everything, even fields the model doesn't know about.

| Area | Status |
|---|---|
| Recipe: name, type, author, coauthor, created, batch size, efficiency | Typed |
| Recipe: notes, gravities, ABV, color, pH, attenuation, carbonation, calories | Typed |
| Ingredients: fermentables, hops, miscellaneous, cultures, water additions | Typed |
| Mash (and steps), fermentation (and steps), boil (and steps) | Typed |
| Hop `beta_acid` | Typed, as an optional percentage. The calculations do not use it. |
| Ingredient price, recipe currency | Typed, but Kettle Logic data: stored in the sidecar, not BeerJSON (see [beerjson-profile.md](formats/beerjson-profile.md), section 8) |
| Recipe `style`, `ibu_estimate`, `packaging`, `taste` | Preserved |
| Root `profiles`, `styles`, `hop_varieties`, `fermentables`, `cultures`, `miscellaneous_ingredients`, `equipments`, `mashes`, `fermentations`, `boil`, `packaging` | Preserved |
| Culture `zymocide`, hop `oil_content`, inventory blocks, `timing` detail beyond the typed keys | Preserved |

Typed quantities are `Quantity` values in SI canonical units with their authored unit kept (see [units.md](units.md)). Gravity, color, pH, pressure, specific volume, and bitterness are `Measure` values: exact, with the unit as authored and never converted.

## Edits

Header: `kettlelogic/model/commands.hpp`. Each function returns a `Command<Document>` for `CommandStack<Document>`.

| Function | Undo | Notes |
|---|---|---|
| `addRecipe(recipe)` | removes it | Assigns a fresh id and UUID on first apply |
| `removeRecipe(id)` | reinserts at the old index | Deletes for good. Labelled "Delete Permanently". |
| `trashRecipe(id)` | files it back where it was, and removes the Recently Deleted folder if this edit made it | Moves the recipe into the Recently Deleted folder (a `Folder` with `trash` set), making the folder if the document has none. Refused if the recipe is already there. The folder cannot be renamed or removed. |
| `duplicateRecipe(id)` | removes the copy | Places a copy after the recipe, named "<name> copy". The copy and each of its ingredients get fresh ids and UUIDs on first apply, and redo keeps them. Foreign properties owned by the original's entities are not copied. |
| `duplicateFermentable`, `duplicateHop`, `duplicateCulture`, `duplicateWater`, `duplicateMiscellaneous` | removes the copy | Places a copy after the ingredient, with a fresh id and UUID. The copy keeps every field, including a miscellaneous ingredient's salt. |
| `setBrewhouseEfficiency(id, quantity)` | restores the old efficiency | Must be above 0 % and up to 100 % |
| `applyEquipment(id, batch, efficiency, boilOff)` | restores all three | One undo step ("Apply Equipment Profile"), built from the three commands above. Refused as a whole if any part is. |
| `setFermentable`, `setHop`, `setCulture`, `setWater`, `setMiscellaneous` | restores the old item | Replace an item in place, keeping its position, id, and UUID. Checked by the same rules as the add command, so an invalid edit is refused and nothing is recorded. |
| `setPrice(id, kind, ingredientId, price)` | restores the old price | `price` is an amount per one unit, such as 4.50 per kg, or nullopt to clear it. Refused if it is negative, or the unit does not measure the same thing as the ingredient's amount. Fermentables, hops, cultures, and miscellaneous ingredients only. |
| `setCurrency(id, code)` | restores the old code | A three-letter code, stored in upper case. Prices are not converted. |
| `setRecipeName(id, name)` | restores the old name | Merges with repeated renames into one step |
| `setBatchSize(id, quantity)` | restores the old size | |
| `addFermentable(id, fermentable)` | removes it | Assigns a fresh id on first apply. Refused if the yield is empty, the type is not a fermentable type, or the amount is not a mass or volume. |
| `removeFermentable(id, fermentableId)` | reinserts at the old index | |
| `addHop(id, hop)` / `removeHop(id, hopId)` | removes it / reinserts at the old index | Creates the list if absent, and undo removes a list the edit created. Refused unless the acids are percentages, the amount is a mass or volume, and the form and timing are valid. A timing time or duration must be a whole number. |
| `addCulture(id, culture)` / `removeCulture(id, cultureId)` | same | Refused unless the type and form are BeerJSON values and the amount is a mass, volume, or count |
| `addWater(id, water)` / `removeWater(id, waterId)` | same | Refused unless each mineral is a concentration and the amount, if any, is a volume |
| `setIngredientListPresent(id, list, present)` | restores the previous presence | Adds an empty list, or removes one. Removing a list that still has items is refused. |
| `setBoilStepsPresent(id, present)` | restores the previous presence | Same rule as the ingredient lists. Refused if the recipe has no boil procedure. |

A command throws `std::invalid_argument` from `apply()` when its target does not exist or its input would produce an invalid document. `CommandStack` does not record a command that throws.

## Optional ingredient lists

The schema makes hop, miscellaneous, culture, and water additions optional. The model keeps the difference between an absent list and an empty one:

- An absent list is `std::nullopt`. The writer leaves it out.
- A present list is engaged, even when empty. The writer writes it as `[]`, so a UI can show it as an empty list.
- `setIngredientListPresent` adds an empty list or removes one. Removal is refused while the list has items.

Fermentable additions are required, so they are always present. The boil steps follow the same rule, through `setBoilStepsPresent`.

## Rules the reader enforces

- Required properties per the schema's `required` lists (for example, a recipe needs `batch_size`, and a hop addition needs `timing`).
- Value types: strings, numbers, booleans, arrays, and objects.
- Enum values for the typed enums. These are generated from the vendored schema, not typed by hand (see Known gaps).
- Unit names exist, and each unit's dimension matches what the property accepts. For example, `batch_size` takes volume only.
- Time values are integers.
- Quantity objects have exactly `value` and `unit`.
- A yield has at least one property.
- The version is `2.01`, `2.06`, or `3`.

Unknown keys are split by the schema, not rejected (see [beerjson-profile.md](formats/beerjson-profile.md), section 7):

- A key the schema defines but the model doesn't type is kept in `preserved` and written back.
- A key the schema does not define, in an object it closes, becomes a `Diagnostic` and a `ForeignProperty` on the `Document`. It is not written to the BeerJSON output. `saveDocument` writes it to the sidecar, and `openDocument` restores it, checking each entry against the document (see [beerjson-profile.md](formats/beerjson-profile.md), section 8).
- A key in an open object is written back.

The table of defined properties comes from `core/tools/gen_schema.py`, the same generator as the enums. A read result with diagnostics is still a successful read, so check `Document::diagnostics` after reading.

## Known gaps

- Only the recipe graph is typed. The catalog records and style are preserved as raw JSON, so they can't yet be edited through commands.
- Enum tables and unit lists are generated from the vendored schema by `core/tools/gen_schema.py` into `core/include/kettlelogic/model/schema.hpp`. Regenerate after any schema update. CTest (`schema_current`) fails if the header is stale, and `testSchemaUnitsAreKnown` fails if the schema has a convertible unit the unit table lacks.
- The reader doesn't check older-version differences: every version is checked against the current schema.
- Derived values (OG, IBU, color) are not calculated yet. That is the calc layer's job.
- There's no undo-stack persistence yet. The command log is in memory only.
- Entity matching is heuristic. Two entities of the same kind with the same name, edited outside the app, cannot be told apart by name, so they fall back to pointer and may get fresh UUIDs.
- File writing (atomic write of the document and sidecar together) is not implemented. `saveDocument` returns the two texts only.

## Testing

- `core/tests/test_model.cpp` reads `core/tests/data/sample_recipe.json`, checks its values, and confirms a write-then-read keeps the JSON structurally equal to the input.
- Error tests check the JSON Pointer of each kind of rejection, and that an invalid edit is never recorded.
- CTest `schema_conformance` (`core/tests/schema/check_writer_output.py`) runs the writer on the sample and validates both the input and the output against the vendored schema. It needs Python 3 with `jsonschema`.

## Files

Header: `kettlelogic/io/document_file.hpp`

A recipe on disk is a BeerJSON file, its sidecar, and two backup slots:

| File | Role |
|---|---|
| `<stem>.json` | The document |
| `<stem>.kettlelogic.json` | The sidecar: entity UUIDs and foreign properties, bound to the document by its SHA-256 |
| `<stem>.previous.json` (+ sidecar) | A copy of the last consistent pair, kept before each save |
| `<stem>.autosave.json` (+ sidecar) | The latest unsaved work, written by `autosave()`, removed by a successful save |

| Function | Behavior |
|---|---|
| `loadDocument(path)` | Returns the document and where it came from (`Source::Current`, `Autosave`, or `Previous`). Falls back in that order, and fails with the reason for each candidate if none is usable. |
| `saveDocument(document, path)` | Keeps the current pair as the previous copy (if it is consistent), writes the BeerJSON then the sidecar, each atomically, and removes the autosave. |
| `autosave(document, path)` | Writes the autosave pair. Safe to call often. |
| `discardAutosave(path)` | Removes the autosave pair. |

### Crash safety

Each file is written to a temporary file in the same folder, flushed to disk (`F_FULLFSYNC` on macOS), and renamed over the old file. The sidecar records the SHA-256 of the BeerJSON it belongs to. So a save interrupted between the two renames leaves a pair that does not match, and loading detects that instead of silently using it.

What the tests check (`core/tests/test_io.cpp`):

- **Crash at each step.** A child process runs a save and exits, with `_exit`, at each named step. The parent then loads and checks the result. Before the BeerJSON is replaced, the old state is returned. After it is replaced but before the sidecar, the previous copy is returned, and the new state is not. After the sidecar is replaced, the new state is returned. Retrying the save then leaves no temporary files.
- **Crash after an autosave.** The autosave is returned, not the older files.
- **Torn file.** A BeerJSON truncated after a successful save falls back to the previous copy.
- **Nothing usable.** Loading fails and names each reason. It never returns a guess.

What the tests do not cover, and where that matters:

- **Power loss is not simulated.** `_exit` simulates a crash of the process. The kernel still holds every write it accepted, so the tests cannot see data that a power cut would lose from the drive's cache. That is the job of the flush calls, which are not testable here.
- **Windows is not exercised.** The flush and rename code has a Windows branch, but it has not been compiled or run on Windows.
- **A BeerJSON file with no sidecar is read as a plain file.** A first save that crashes after the BeerJSON rename therefore loads as that file, with fresh identities. The data is valid, but the sidecar's identities and foreign properties are lost.
