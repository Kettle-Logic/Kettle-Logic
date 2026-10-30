# BeerXML Import and Export

Status: draft 0.1. BeerXML exists so that users can move recipes to and from BeerSmith and other tools. It is an adapter. BeerJSON remains the primary format.

Export is implemented in `core/src/io/recipe_export.cpp`, as BeerXML 2.01 with recipe, fermentables, hops, miscellaneous, yeast, water, and mash. Ingredients that BeerXML cannot hold (a count of yeast cells, say, or a hop measured by volume) are left out, and an XML comment at the top of the file lists each one.

Import is implemented in `core/src/io/beerxml_import.cpp` (`io::importBeerXml`). It reads a BeerXML file into the same model a BeerJSON file is read into, and `writeBeerJson` then gives BeerJSON. The C API exposes this as `kl_beerxml_to_beerjson`, which returns the BeerJSON document and the warnings. The apps do not yet offer a BeerXML import in their open dialogs.

## 1. Scope

- **Import:** BeerXML 1.0 and 2.01 documents, including files exported by BeerSmith. A root of `<RECIPES>` or a single `<RECIPE>`.
- **Export:** BeerXML 2.01, for compatibility with BeerSmith and others.
- **Not in scope:** BeerSmith's own `.bsmx` file format, which is proprietary. Users who have only `.bsmx` files should export BeerXML from BeerSmith first.

The references are the BeerXML specification at <https://www.beerxml.com/beerxml.htm>, which is the 1.0 spec with the display extensions, and the 2.01 PDF at <http://users.speakeasy.net/~antonw/beer_xml/BeerXML_v2_01.pdf>. The tag names and enumerated values in section 3 were checked against the first. The 2.01 PDF could not be read as text, so 2.01-only tags are not yet verified.

## 2. Units

BeerXML is metric. Quantities are plain numbers with units implied by the tag:

| Quantity | BeerXML unit |
|---|---|
| Weight | kilograms |
| Volume | liters |
| Temperature | degrees Celsius |
| Time | minutes |

Import converts each value to exact canonical units with `Rational::fromDecimal` (see [units.md](../units.md)). Export writes the canonical value rounded to the BeerXML field's precision.

## 3. Entity mapping

| BeerXML record | Kettle Logic entity | Notes |
|---|---|---|
| `RECIPE` | Recipe | Top-level; contains the child lists below. TYPE maps to the recipe type |
| `FERMENTABLE` | Fermentable | TYPE, AMOUNT, YIELD (fine grind), COLOR, ORIGIN, SUPPLIER (as producer) |
| `HOP` | Hop | ALPHA, BETA, AMOUNT, FORM, USE (as timing), TIME, ORIGIN. BeerXML has no supplier for hops |
| `YEAST` | Culture | TYPE, FORM, AMOUNT, ATTENUATION, LABORATORY (as producer), PRODUCT_ID |
| `MISC` | Miscellaneous ingredient | TYPE, AMOUNT, USE and TIME (as timing) |
| `MASH` and `MASH_STEP` | Mash and mash steps | Step order is preserved in the file. INFUSE_AMOUNT, RAMP_TIME, END_TEMP are read |
| `WATER` | Water addition | The six salts and AMOUNT. There is no pH on a water addition, so PH is noted and skipped |

Not read, and noted in the warnings: `STYLE`, `EQUIPMENT`, `FERMENTATION`, and every other tag the table above does not name. The recipe's `EST_*` and `IBU` values are recalculated by Kettle Logic, so they are recognized and not read.

Fields in the source file that have no Kettle Logic equivalent are kept in the Kettle Logic extension (see [beerjson-profile.md](beerjson-profile.md), section 8) and written back on export when a BeerXML file is re-exported from the same recipe. This is not done yet: the import keeps no extension data.

## 4. Import behavior

- **Parse numbers exactly.** Read each numeric element's text and parse it with `fromDecimal`. Don't convert through `double`.
- **Tolerant of errors.** Unknown tags are skipped and reported as warnings. Missing optional fields get defaults and a warning. Only a malformed document, or a missing required field, fails the import.
- **Required fields** are the ones Kettle Logic cannot make a sane value for: `NAME` on a recipe, an ingredient, a mash, or a mash step; `BATCH_SIZE`; `AMOUNT` on ingredients; `ALPHA` on hops; `STEP_TEMP` and `STEP_TIME` on mash steps. Other spec-required fields that are missing (`YIELD`, `COLOR`, `TYPE`, `EFFICIENCY`, `GRAIN_TEMP`, and the water salts) get a default and a warning.
- **Warnings** are `document.diagnostics`, with a path such as `RECIPES/RECIPE[0]/HOPS/HOP[1]/USE`. The C API returns them in its `warnings` list.
- **Exact values.** `1.5` kg is 1500 g. Minutes are kept as minutes when whole. A fraction of a minute, such as a 2.5-minute step, is written in seconds, because BeerJSON times are whole numbers.
- **Units.** Fermentable COLOR is in degrees Lovibond, except a liquid extract (TYPE `Extract`), whose COLOR is in SRM. Percent values are percent. Gravity from OG and FG is read as specific gravity.
- **Import is one undoable step.** Wrap the whole import in a `CompositeCommand` so a single undo removes it. The engine's import returns a new document. The app-side step is not written yet.
- **Encoding.** Accept UTF-8 (with or without a BOM) and ISO-8859-1, which is converted to UTF-8. Reject anything else, and UTF-16, with a clear message.
- **Duplicates.** Recipes whose name and key fields match an existing recipe are imported as new copies, not merged. Ask the user before overwriting anything.

## 5. Export behavior

- Write BeerXML 2.01 with the XML declaration and UTF-8 encoding.
- Write one `RECIPE` per recipe, with its ingredient lists nested as the specification requires.
- Round values to the BeerXML field's expected precision. Unlike BeerJSON, BeerXML has no unit tag, so every value is written in the BeerXML unit and rounded at export.
- A fermentable's COLOR is written in degrees Lovibond, or in SRM for a liquid extract (TYPE `Extract`). Colors held in SRM or EBC are converted to Lovibond with L = (SRM + 0.6) / 1.3.
- Warn the user about any Kettle Logic feature that BeerXML can't represent, listing what will be dropped before the file is written.

## 6. Test requirements

- Import a file exported by BeerSmith, with recipes, mash steps, and a water profile. Compare the values with what BeerSmith shows.
- Round trip: BeerXML → Kettle Logic → BeerXML produces the same values to the export precision.
- A malformed file produces a clear error without crashing.
- An unknown tag produces a warning and doesn't stop the import.
- Import then undo removes every imported entity.

## Open items

1. Confirm the 2.01-only tags and required elements against the 2.01 PDF. The beerxml.com page is the 1.0 spec.
2. Collect sample files from BeerSmith and at least one other tool, and check them into `core/tests/data/`. `sample_beerxml.xml` is hand-written in BeerSmith's layout, not taken from BeerSmith.
3. Confirm the export's COLOR units with a BeerSmith file. The export now writes Lovibond, and SRM for a liquid extract, as the spec gives them, and the test checks only Kettle Logic's own round trip.
4. Import into the apps: the open dialogs, and the single undo step for an import.
