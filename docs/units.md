# Units and Exact Quantities

## Goal

A value entered as `1 lb` must be stored, displayed, and exported as `1 lb`. It must never become `0.99999999 lb` after a round trip through kilograms or ounces.

## Why floating point fails

`1 lb` is `0.45359237 kg`. That decimal isn't representable in binary floating point, so converting `1 lb → kg → lb` in `double` can land a hair off `1.0`. Each conversion can add error, and displayed values drift.

## Rule set

1. **Canonical storage is exact.** Every quantity is stored as an exact rational in its SI canonical unit: kilograms for mass, liters for volume, degrees Celsius for temperature. No `double` is ever stored.
2. **Input is parsed exactly.** Text entered by a user, or a JSON or XML number, is parsed as a decimal into an exact rational. `"1.1"` becomes `11/10`, not the nearest binary double.
3. **Conversion is exact.** Every unit has an exact scale and offset to its canonical unit (see the table below). Converting to and from a unit is rational arithmetic.
4. **Rounding happens only at display or export.** `Rational::toFixed(places)` rounds half away from zero using integer arithmetic. Nothing else rounds.
5. **Floating point is for calculation only.** The calc engine takes `toDouble()` values, produces derived results, and doesn't write back to stored quantities. Derived results can be shown as is; if the user edits one, that edit is new exact input.

## Canonical units and exact definitions

| BeerJSON unit | Dimension | Scale to canonical | Offset | Exact definition |
|---|---|---|---|---|
| `mg` | mass | 1/1 000 000 kg | 0 | |
| `g` | mass | 1/1000 kg | 0 | |
| `kg` | mass | 1 | 0 | canonical |
| `lb` | mass | 0.45359237 kg | 0 | international avoirdupois pound |
| `oz` | mass | 0.45359237 / 16 kg | 0 | 1/16 lb |
| `ml` | volume | 1/1000 L | 0 | |
| `l` | volume | 1 L | 0 | canonical |
| `floz` | volume | 0.0295735295625 L | 0 | US fluid ounce = 1/128 US gallon |
| `tsp` | volume | (1/6) floz | 0 | US teaspoon |
| `tbsp` | volume | (1/2) floz | 0 | US tablespoon |
| `cup` | volume | 8 floz | 0 | US customary cup |
| `pt` | volume | 16 floz | 0 | US pint |
| `qt` | volume | 32 floz | 0 | US quart |
| `gal` | volume | 128 floz | 0 | US gallon = 231 in³ = 3.785411784 L |
| `bbl` | volume | 31 gal | 0 | US beer barrel (confirmed) |
| `ifloz` | volume | (1/160) igal | 0 | imperial fluid ounce |
| `ipt` | volume | 20 ifloz | 0 | imperial pint |
| `iqt` | volume | 40 ifloz | 0 | imperial quart |
| `igal` | volume | 4.54609 L | 0 | imperial gallon (exact by definition) |
| `ibbl` | volume | 36 igal | 0 | imperial barrel |
| `C` | temperature | 1 | 0 | canonical |
| `F` | temperature | 5/9 | −160/9 | `C = F·5/9 − 160/9` |

Every scale above is a finite decimal or a small rational, so conversions are exact. The engine computes them as `Rational` expressions at startup, not as literals, so the definitions read the same way the table does.

## Conversion

```
canonical = value × scale + offset
value     = (canonical − offset) / scale
```

Example, exact at every step:

```
1 lb → kg:   1 × 0.45359237                 = 0.45359237 kg
kg → oz:     0.45359237 / (0.45359237/16)   = 16 oz
16 oz → lb:  16 × (0.45359237/16) / 0.45359237 = 1 lb
```

## Display

- Each unit has a display precision chosen by the UI (for example, 2 decimals for lb, 1 for °F, 0 for ml). Display precision is presentation only and never changes stored values.
- Use `toFixed(places)`. A value of exactly `1` prints `1.00`, never `0.99999999`.
- Locale formatting (decimal separator, grouping) is applied by the front end on top of the fixed-decimal string.

## Non-terminating conversions

Some conversions repeat, for example 5 ml expressed in US teaspoons (5 / 4.92892159375… ). These stay exact in storage. The display rounds them. Repeating values only lose precision at export, and only when the target format requires a decimal (see [beerjson-profile.md](formats/beerjson-profile.md)).

## Testing requirements

The engine test suite must include:

- Round trips through every unit pair for a set of values: `x → unit → x` equals `x` exactly.
- The explicit cases: `1 lb ↔ 16 oz`, `1 gal = 128 floz`, `212 °F = 100 °C`, `68 °F = 20 °C`.
- Parsing `"1.1"` produces `11/10` exactly.
- `toFixed` rounding on half-way values (`0.125` at 2 places is `0.13`).
