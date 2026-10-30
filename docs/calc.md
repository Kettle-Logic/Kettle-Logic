# Calculations

Status: second version. Gravity, alcohol, bitterness, color, strike water, the boil's volumes, and water chemistry. Calories are not calculated yet.

The code is `core/src/calc/recipe.cpp`. Its entry point is `calc::calculate(const model::Recipe&)`.

Results are derived values in `double`, for display. They are never written back into the model as if the user had entered them. Each input that is missing or not supported leaves its result empty and adds a note.

## Formulas

| Result | Formula | Source |
|---|---|---|
| Points per pound per gallon, from a yield | sucrose 46.214 × yield fraction. If the yield is a gravity (`potential`), `(SG − 1) × 1000`. | Sucrose's yield is the standard reference. |
| Original gravity | `1 + Σ(lb × points) × brewhouse efficiency / (gal × 1000)` | Standard gravity points. |
| Final gravity | The recipe's `final_gravity` if given. Else `OG − (OG − 1) × attenuation`, using the first culture's attenuation, else the recipe's apparent attenuation. | Apparent attenuation definition. |
| Plato or brix to SG | `1 + P / (258.6 − (P / 258.2) × 227.1)` | Balling relation, as commonly used in brewing. |
| ABV | `(OG − FG) × 131.25` | Standard brewing approximation. |
| Bitterness (IBU), per addition | `U × α × mass_g × 1000 / volume_L`, where `U = 1.65 × 0.000125^(OG − 1) × (1 − e^(−0.04 × t)) / 4.15` | Tinseth. |
| Color (SRM) | `MCU = Σ(lb × Lovibond) / gal`, then `SRM = 1.4922 × MCU^0.6859` | Morey. |
| Color conversions | SRM from EBC: `EBC / 1.97`. Lovibond from SRM: `(SRM + 0.6) / 1.3`. | Standard conversions. |
| Strike water | `T_strike = T_mash + (0.2 / R) × (T_mash − T_grain)`, with `R` in quarts per pound | Thermal balance, as used in brewing texts. |
| Post-boil volume | `V_post = V_pre × (1 − r × t)`, with `r` the evaporation fraction per hour (default 10 %) and `t` the boil in hours | Linear boil-off model. |
| Pre-boil volume, from the batch | `V_pre = (V_batch + V_trub) / (1 − r × t)`, with trub loss `V_trub` (default 1 L) | Works back from the fermenter volume. |
| Residual alkalinity | `RA = alkalinity − (Ca / 3.5 + Mg / 7)`, in ppm as CaCO₃, where `alkalinity = HCO₃ × 50/61 + CO₃ × 50/30` | Kolbach. |

Water-to-grain ratios are converted to quarts per pound through liters per kilogram. Units supported: `qt/lb`, `l/kg`, `l/g`, `m^3/kg`, `gal/lb`, `gal/oz`, `floz/oz`, `ft^3/lb`.

### The boil model

The boil-off settings are `model::BoilOff`, stored on each recipe. The defaults are a typical homebrew kettle: 10 % of the pre-boil volume evaporates each hour, and 1 L is lost to trub and the chiller. They are set with `model::setBoilOff`, which is undoable, and kept in the sidecar (see [beerjson-profile.md](formats/beerjson-profile.md), section 8). A stated pre-boil size on the recipe is used directly. Bitterness uses the post-boil volume.

### Water chemistry

Each water addition is blended by volume: `C_mash = Σ(V_i × C_i) / Σ V_i`. Ions are in mg/L.

A miscellaneous ingredient is a salt only when its salt type is set (`model::SaltKind`: gypsum, calcium chloride, Epsom salt, or baking soda), by `model::setSaltType`. The name is never used. A salt counts as an addition in its own right, and its mass dissolves first, so a target only adds the shortfall. A water agent with no salt type is not counted, and a note says so.

With a target profile, four salts are sized in order, each for the ion it controls:

| Ion | Salt | Formula | Grams per gram of salt (ion) |
|---|---|---|---|
| sulfate | gypsum | CaSO₄ · 2 H₂O | SO₄: 96.061 / 172.169 |
| chloride | calcium chloride (anhydrous, as brewing water calculation has it) | CaCl₂ | Cl: 638.8 mg per g |
| magnesium | Epsom salt | MgSO₄ · 7 H₂O | Mg: 24.305 / 246.471 |
| bicarbonate | baking soda | NaHCO₃ | HCO₃: 61.017 / 84.007 |

The salt mass for an ion is `grams = (target − current) × V / (ion mg per gram)`. Calcium and sodium follow from these salts and are reported, not targeted. Nothing is removed: an ion already at or above its target gets no salt, and a note says so. The water builder and the water tools use per-gram salt values instead.

## Known simplifications

- **Boil-off is linear.** Real evaporation depends on the kettle and the boil's vigour. The defaults are a guide; set them from measurements.
- **Hop form.** No correction for pellets, whole hops, or extract. Hops measured by volume are not calculated, and a note says so.
- **Dry-extract and sugar yields.** Taken from the yield as given, without a moisture correction.
- **Color.** Each malt contributes its Lovibond value. There is no roasted-malt correction.
- **Coarse grind.** A coarse-grind yield is used as the fine-grind yield, with a note.
- **Final gravity from attenuation.** Uses the first culture that gives an attenuation. Mixed cultures are not modeled.
- **Water chemistry targets four ions only.** The mash pH is a linear estimate, not a measurement.
- **Original gravity uses the batch volume,** the fermenter volume, which is what is measured in practice.

## Cider, mead, and wine

Two layers. The recipe calculation (`calc::calculate`) works on a recipe whose type is `cider`, `mead`, or `wine`:

- Its ingredients are BeerJSON fermentables. Juice and fruit are measured by volume with a gravity yield. Honey and sugar are measured by mass with a percent yield. A volume with no gravity yield adds no gravity, and a note says so.
- Brewhouse efficiency does not apply, and no boil is assumed.
- ABV uses the advanced formula below, because drinks have higher gravities than beer.
- Sulfite additions (`Additive::Sulfite`) give the SO2 they add, in mg/L, as `ppm = grams / batch liters × 576`. Tartaric acid additions give the acid in g/L.
- The plan (`Results::drink`) gives the sugar that reaches the recipe's own targets, from `original_gravity` and `alcohol_by_volume`. For gravity, it's sucrose for cider and wine, and honey for mead, from the engines below; the honey is the difference between two gravities, since honey gives its points linearly. For alcohol, it's sucrose for cider and wine at the recipe's fermentation efficiency (`Recipe::fermentationEfficiency`, default 92 %, the typical yeast figure). Mead is planned by gravity only. A target that sugar cannot reach, below the drink's own value, is noted rather than planned.

The second layer is the engines, which take plain numbers and are used for planning. Shared conversions are in `core/src/calc/fermented.cpp`; each drink has its own file (`cider.cpp`, `mead.cpp`, `wine.cpp`). Invalid inputs throw `std::invalid_argument`.

| Result | Formula | Source |
|---|---|---|
| Points per g/L of sucrose | `46.214 × 0.001 × 2.20462 × 3.78541 = 0.38567` points per g/L | Sucrose yield, as above. |
| Alcohol from hexose | `ABV = S × eff × (2 × 46.069 / 180.156) / 0.789 / 10`, with `S` in g/L of glucose or fructose | Stoichiometry of C6H12O6 → 2 C2H5OH + 2 CO2; ethanol density 0.789 g/mL. |
| Alcohol from gravities | `76.08 × (OG − FG) / (1.775 − OG) × (FG / 0.794)` | The advanced formula. |
| Sucrose for a gravity rise | `Δpoints / 0.38567` g per liter | As above. |
| Honey for a gravity | `Δpoints / (0.38567 × yield)` g per liter, with honey yield 0.80 | Honey is about 80 % sugar, so it gives about 80 % of sucrose's points. |
| Honey for an alcohol target | Bisection on OG, so the alcohol formula and the honey agree | The alcohol rises with OG. |
| Sugar in a must | `10 × Brix × SG` g/L | A must of Brix B holds 10·B·S grams of sugar per liter. |
| Chaptalization | Hexose from the alcohol target, × 0.95 for sucrose | 1 g sucrose gives 1.0526 g of hexose. |
| Potassium metabisulfite | `ppm / 576` g per liter | The salt is 57.6 % SO2 by mass. |
| Molecular SO2 fraction | `1 / (1 + 10^(pH − 1.81))` | pKa of SO2 at 20 C. |
| Acid to raise TA | `(target − current) × L` g, with TA in g/L as tartaric acid | A gram of tartaric acid in a liter adds 1 g/L of TA. |

Simplifications:

- **Fermentation efficiency is an input, not a default.** 1.0 is the theoretical ceiling: no yeast reaches it. Real fermentations are a little under it, so check the result against a test.
- **Honey yield is typical.** Set it from measurements; it defaults to 0.80 of sucrose's points.
- **Sulfite binding is not modeled.** The metabisulfite amount is the theoretical addition, so the free SO2 you get is lower.
- **Cider and wine do not model malolactic conversion, sugar inversion, or nutrients.**

## Style comparison

`core/src/calc/style.cpp` holds the vital statistics of 86 styles from the BJCP 2021 Beer Style Guidelines, styles 1A to 26D. The source is https://www.bjcp.org/style/2021/beer/. Each range is the low and high end printed for the style. The comparison places the recipe's original gravity, final gravity, alcohol, bitterness, and color against those ranges. A value is below, within, or above its range, or unknown when the recipe has no value. Nothing is rounded before the comparison.

- **Styles 28 to 33 are not in the table yet.** Their statistics are set out differently in the guidelines, so they were left out rather than guessed.
- **Saison (25B) uses the standard gravities and color.** The guidelines print a "standard" and a "table" qualifier against some of its figures. The ABV range is as printed. Check it against the guidelines before relying on it.

## Tools

`core/src/calc/tools.cpp` holds the brewing tools and unit converters that work outside a recipe. The front end calls them by name through `kl_tool_json`. Inputs are decimal text, read exactly before any arithmetic, and results are numbers for display.

| Tool | Inputs | Results | Formula and limits |
|---|---|---|---|
| `water.profile` | `volume`, `roPercent`, base `calcium`/`magnesium`/`sodium`/`sulfate`/`chloride`/`bicarbonate`/`carbonate`, RO water `roCalcium`… (optional), salt grams `gypsum`/`calciumChloride`/`epsomSalt`/`magnesiumChloride`/`tableSalt`/`bakingSoda`/`chalk`/`picklingLime`, `mashPh` (optional), targets `targetCalcium`… (optional) | resulting ions, `residualAlkalinity`, `sulfateToChloride`, `difference*` (target less result) | water adjustment. |
| `brew.mash` | `grain1Kg`…`grain4Kg`, `grain1Lovibond`…, `grain1Category`, `waterLiters`, water ions, `bakingSodaGrams`, `targetPh`, `targetAcid`, `targetAcidStrength`, `mashAcid`, `mashAcidGrams`, `mashAcidStrength` | `maltAcidityMeq`, `quartsPerPound`, `phBefore`, `phAfter`, `residualAlkalinityBefore`, `acidGramsForTarget` | mash pH and the acid to a target pH. |
| `brew.sparge` | `alkalinity`, `startPh`, `targetPh`, `liters`, `acid`, `strength` | `milliliters`, `solutionGrams`, `pureGrams`, `acidMmolPerLiter` | sparge acid. |
| `water.infusion` | `grainKilograms`, `grainTemperature`, `mashTemperature`, `waterToGrain` (L/kg) | `strikeTemperature`, `waterLiters`, `quartsPerPound` | `T_strike = T_mash + (0.2 / R)(T_mash − T_grain)`, with R in quarts per pound. |
| `mash.decoction` | `mashLiters`, `currentTemperature`, `targetTemperature` | `decoctionLiters` | `V_d = V_m (T_2 − T_1) / (100 − T_1)`. The decoction leaves at T_1, boils to 100 °C, and returns. |
| `mash.temperature` | `mashLiters`, `mashTemperature`, `targetTemperature`, `waterTemperature` | `waterLiters`, `totalLiters` | `V_w = V_m (T_t − T_m) / (T_w − T_t)`. Equal heat capacities. Hot water raises the mash, cold water lowers it. |
| `mash.alkalinity` | `bicarbonate`, `calcium`, `magnesium` (mg/L) | `residualAlkalinity` | residual alkalinity. **Not a pH**: the pH needs the grist, which `brew.mash` takes. |
| `yeast.starter` | `batchLiters`, `originalGravity`, `lager`, `packCellsBillion`, `growth`, `starterLiters`, `starterGravity`, `twoStage`, `firstStageLiters` | cells, packs, starter volumes, dry malt | The starter plan of `calc::planStarter`. |
| `gravity.hydrometer` | `reading`, `sampleTemperature`, `calibrationTemperature` | `correctedGravity` | `SG_corr = SG_read × ρ_w(T_cal) / ρ_w(T_sample)`, with water density by Kell (1975). A warm sample reads low, so the correction raises it. Water's density stands in for the wort's, which is close but not exact. |
| `gravity.refractometer` | `brix` | `specificGravity` | Balling: `SG = 1 + P / (258.6 − P × 227.1 / 258.2)`. For wort before fermentation only. The alcohol correction for a fermenting or finished beer is not implemented. |
| `alcohol.gravities` | `originalGravity`, `finalGravity` | `standardAbv`, `advancedAbv` | `(OG − FG) × 131.25`, and the advanced formula in "Cider, mead, and wine". |
| `hop.bitterness` | `alphaPercent`, `grams`, `postBoilLiters`, `boilMinutes`, `gravity` | `utilizationPercent`, `ibu` | Tinseth, as in "Formulas". Hop form is not corrected. |
| `hop.age` | `alphaPercent`, `months`, `storageTemperature`, `annualLossPercent` | `remainingAlphaPercent`, `lostPercent` | `retention = (1 − loss)^(years × 2^((T − 20) / 10))`. **The annual loss is an assumption the user sets**, and the doubling every 10 °C is also assumed. |
| `mead.nutrient` | `volumeLiters`, `targetYan`, `currentYan` (mg/L) | `dapGrams`, `dapGramsPerLiter` | DAP is 21 % nitrogen, so 210 mg of YAN per gram. The YAN target is the user's. |
| `sweeten.backsweeten` | `volumeLiters`, `currentGravity`, `targetGravity`, `sugarYield` | `sugarGrams`, `sugarGramsPerLiter` | Points × volume / (0.38567 × yield). The yield is relative to sucrose: 1.0 for sucrose, about 0.8 for honey. |
| `sweeten.sulfite` | `volumeLiters`, `molecularSo2` (mg/L), `pH` | `freeSo2Ppm`, `metabisulfiteGrams`, `molecularFractionPercent` | Free SO₂ = molecular target / molecular fraction at pH. Metabisulfite as in "Cider, mead, and wine". At pH 3.5, 0.8 mg/L molecular needs about 40 mg/L free. |
| `boil.volume` | `preBoilLiters`, `boilMinutes`, `evaporationPercentPerHour` | `postBoilLiters`, `evaporatedLiters` | `V_post = V_pre (1 − r t)`, as in "The boil model". |
| `boil.rate` | `preBoilLiters`, `postBoilLiters`, `boilMinutes` | `evaporationPercentPerHour` | `r = (1 − V_post / V_pre) / t`. |
| `dilute.dilution` | `volumeLiters`, `currentValue`, `targetValue` | `waterLiters`, `finalLiters` | `V_water = V (C_1 / C_2 − 1)`, for any concentration. Ethanol contraction is ignored. |
| `gravity.adjust` | `volumeLiters`, `currentGravity`, `targetGravity`, `sugarYield` | `sugarGrams`, `waterLiters` | Sugar to raise, or water to dilute. Gravity points are proportional to extract. |
| `weight.mass` / `weight.volume` | `density` (g/mL) with `milliliters` or `grams` | `grams` or `milliliters` | Mass = density × volume. |
| `carbonation.pressure` | `volumes`, `temperature` | `gaugePsi`, `gaugeKilopascals` | Henry's law for pure water: `V = α(T) × P_atm`, with the Bunsen coefficient α(20 °C) = 0.878, and a van 't Hoff fit to α(0 °C) = 1.713, so `α(T) = 0.878 exp(2674 (1/T − 1/293.15))`. Beer differs by about a psi. |
| `carbonation.volumes` | `gaugePsi`, `temperature` | `volumes` | The inverse of the above. |
| `carbonation.priming` | `volumeLiters`, `targetVolumes`, `residualVolumes` | `dextroseGrams`, `sucroseGrams` | Dextrose gives 2 CO₂ per glucose and sucrose 4 CO₂ per sucrose, at 22.414 L/mol. One volume is about 4.02 g/L of dextrose and 3.82 g/L of sucrose. Assumes all the sugar ferments. |
| `convert.temperature`, `convert.weight`, `convert.volume` | `value`, `from`, `to` (BeerJSON unit names) | `value` | Exact, through `Quantity`. |
| `convert.gravity` | `value`, `from`, `to`: `sg`, `plato`, `brix`, `points` | `value` | Plato and Brix use the same sucrose scale here. |
| `convert.pressure` | `value`, `from`, `to`: `psi`, `kPa`, `bar`, `atm` | `value` | Through pascals, in double. Pressure is not a `Quantity` yet. |

## Water and mash planning

`core/src/calc/brew_plan.cpp` plans the brewing water, mash, and sparge. The mash water is the strike water, `mash ratio × grain kg`. The sparge makes up the pre-boil volume, less the first runnings. The mash tun volume is the mash volume plus 0.7 L per kg of grain, a rule of thumb for displacement that the brewer accepted.

- **Mash pH:** linear model, with the grist's acidity by malt category.
- **Water color range:** Palmer's rough mapping from residual alkalinity: low SRM = 0.082 × RA + 5.2, high SRM = (RA + 122.4) / 12.2, valid above RA −128. See [BeerSmith's residual alkalinity note](https://beersmith.com/blog/2016/02/23/residual-alkalinity-and-ph-for-all-grain-beer-brewing/). It is a rough guide.
- **Acid blend:** 45 % malic, 45 % citric, and 10 % tartaric, from [Maine Brewing Supply's acid blend](https://www.brewbrewbrew.com/ingredients/p/acid-blend-8jjfr). That is a wine blend, not a published brewing blend.
- **Acids:** lactic at 88 % and phosphoric at 85 %, each counted by the protons it gives at the target mash pH. One milliequivalent of protons removes one milliequivalent of alkalinity, which is 50 mg/L as CaCO₃.

## Brew journal

`core/src/calc/journal.cpp` works out what a session expected and measured.

- **Efficiency:** the potential points of the grain are the points at the recipe's efficiency divided by that efficiency. Measured mash and brewhouse efficiencies are the points measured, divided by the potential.
- **ABV:** gravity drop times 131.25. Above an original gravity of 1.070 the Hall form is used, `76.08 × (OG − FG) / (1.775 − OG) × FG / 0.794`, from Zymurgy (1995), via [the Gravity article](https://en.wikipedia.org/wiki/Gravity_(alcoholic_beverage)) and [Homebrew Academy](https://homebrewacademy.com/abv-calculator/).
- **Calories per 12 fl oz:** 3.55 × (6.9 × ABW + 4 × (RE − 0.1)), the Oxford Companion form, with ABW = 0.79 × ABV / FG and RE from the Balling relation. Papazian's form differs in how it treats alcohol, so the two can disagree. See [BYO, counting calories](https://byo.com/articles/counting-calories-carbs/).
- **Corn sugar:** 1.977 g of CO₂ per liter per volume, divided by 0.489 g of CO₂ per gram of dextrose, and by 0.70 for the share that stays in the beer. That gives about 2.8 g per liter per volume, which matches common priming charts.
- **Force carbonation pressure:** P = C × (0.0013493 T² + 0.094214 T + 4.81904) − 14.7, with T in °F and C in volumes, from a carbonation equation in a patent on beer carbonation. At 40 °F and 2.5 volumes it gives about 12.2 psi, which matches [Keg Outlet's chart](https://www.kegoutlet.com/keg-carbonation-chart.html).

## Brew Water

`core/src/calc/brew_water.cpp` holds the water calculations. Every number below was checked against a reference spreadsheet: the inputs were set, the spreadsheet was recalculated, and the cell was read.

- **Report.** The cation and anion sums (`balanceReport`) and the conversion from an alkalinity at a pH to bicarbonate and carbonate (`speciesFromAlkalinity`, using pK₂ 10.33).
- **Residual alkalinity.** `alkalinity × (1 + 2 × 10⁻⁴) − (0.7143 calcium + 0.5879 magnesium)`, in ppm as CaCO₃. The coefficients are calcium and magnesium as CaCO₃ over Kolbach's 3.5 and 7. The earlier code used calcium/3.5 on the ion, which underweighted the hardness about 2.5 times for calcium and 4 times for magnesium.
- **Malt acidity.** Per pound, base malt `0.28 × °L`, crystal `0.21 × °L + 2.5`, roast 38, and acid malt 95, in milliequivalents. The color is in Lovibond. EBC converts to SRM by `EBC / 1.97`, and SRM to Lovibond by `(SRM + 0.6) / 1.3`.
- **Mash pH.** `pH = 5.76 − 0.17 × (acid per liter − RA × q / 75)`, with q the water-to-grist ratio in quarts per pound and the acid per liter the malt acidity over the mash liters. A result below zero gives 0.1.
- **Baking soda.** Its bicarbonate is scaled by `2 − P(pH)`, with P a fourth-order fit in pH. At a mash pH of 5.82 about 73 % remains. The mash pH depends on this, so the planner and the mash tool solve the two together.
- **Salts and dilution.** Per gram of salt, the ions are the per-gram-per-gallon values times 3.785. RO water is mixed into the base first, then the salts are dissolved. Calcium chloride is anhydrous, so it gives more chloride per gram than the dihydrate.
- **Acids at a pH.** Protons per molecule, `frac = (r₁ + 2r₁r₂ + 3r₁r₂r₃) / (1 + r₁ + r₁r₂ + r₁r₂r₃)`, with `rₙ = 10^(pH − pKₙ)`, at the target mash pH (5.4 by default). An acid lowers the residual alkalinity by 50 ppm per milliequivalent of protons. The acid's density is the  polynomial in strength.
- **Mash acid to a target pH.** The residual alkalinity that gives the target pH is `75 × (malt acidity / V − (5.76 − pH) / 0.17) / q`. The acid removes the difference, found with the baking soda factor at the target pH.
- **Sparge acid.** Carbonate fractions at the start pH, the end point (pH 4.3), and the target pH give the acid requirement, `Ct × ((f₁c − f₁o) + (f₃o − f₃c)) + 10^−pH_target − 10^−pH_start + 0.01` in mmol/L. The `+0.01` is kept. The mmol divided by the acid's protons gives the acid, and its mass and volume follow.

**Differences from before, and simplifications.**

- The residual alkalinity changed (see above). The water builder's mash pH and its acid figures changed with it, and the planner's acid requirement now depends on pH, not on a fixed 88 % lactic acid at full protonation.
- Calcium chloride is anhydrous.
- Malt categories come from the BeerJSON grain group: caramel and specialty are crystal, roasted is roast, and the rest are base. A grain with no group is sorted by color: up to 5 °L is base, up to 150 °L is crystal, and darker is roast. Acid malt has no BeerJSON group, so the tool takes it by hand.
- The color range from the water builder is clamped at zero, since a strongly negative residual alkalinity would give a negative color.

## Checks

`core/tests/test_calc.cpp` checks the sample recipe against values worked by hand (the boil volumes, bitterness on the post-boil volume, residual alkalinity, and the gypsum target), and a strike-water case in both `qt/lb` and `l/kg`. The expected values were checked against an independent computation, not only against the code. Missing inputs leave results empty with a note, and a hop with no boil time adds no bitterness.
