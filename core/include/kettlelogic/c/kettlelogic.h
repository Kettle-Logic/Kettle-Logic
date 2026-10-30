#ifndef KETTLELOGIC_C_H
#define KETTLELOGIC_C_H

/* A thin C facade over the engine, for front ends that cannot use C++ directly
 * (SwiftUI). It is the only surface the Swift code sees. See docs/architecture.md.
 *
 * Conventions:
 *  - Functions that can fail take `char **error`. On failure it is set to a
 *    message owned by the caller (free it with kl_string_free). It may be NULL.
 *  - Functions that return int return 0 on success and -1 on failure, unless
 *    they say otherwise.
 *  - Strings returned by the facade are owned by the caller: free them with
 *    kl_string_free. A NULL return means failure.
 *  - Numbers that the user types (values, units) are passed as exact decimal
 *    text, such as "5.25", so no value passes through a double on the way in.
 *  - Snapshots are JSON. The engine's own types never leave the library.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kl_document kl_document;

/* Stable recipe identity within a session. */
typedef uint64_t kl_recipe_id;

/* Stable identity of an ingredient (fermentable, hop, culture, water, ...) within a session. */
typedef uint64_t kl_ingredient_id;

/* Stable identity of a folder within a session. */
typedef uint64_t kl_folder_id;

/* A new document with one empty all-grain recipe, not yet saved anywhere. */
kl_document *kl_document_new(char **error);

/* Opens a BeerJSON recipe and its sidecar, with the fallbacks in docs/model.md. */
kl_document *kl_document_open(const char *path, char **error);

/* Saves the recipe, and its sidecar, to `path`. */
int kl_document_save(kl_document *document, const char *path, char **error);

void kl_document_free(kl_document *document);

/* JSON describing every recipe and its calculated results. See docs/architecture.md. */
char *kl_document_snapshot_json(const kl_document *document);

/* Commands. Each one is undoable. */

/* Adds a recipe with a default name, type, and batch size. Its id is written to `created`. */
int kl_add_recipe(kl_document *document, kl_recipe_id *created, char **error);

/* Adds a fermentable. `type` is a BeerJSON fermentable type, `amount` is a mass or volume in
 * `amount_unit`, `yield` is the potential in `yield_unit` (such as "1.036" and "sg"), and `color`
 * is the color in `color_unit` (such as "2" and "SRM"). */
int kl_add_fermentable(kl_document *document, kl_recipe_id recipe, const char *name, const char *type,
                       const char *amount, const char *amount_unit, const char *yield, const char *yield_unit,
                       const char *color, const char *color_unit, char **error);
int kl_remove_fermentable(kl_document *document, kl_recipe_id recipe, kl_ingredient_id fermentable, char **error);

/* Adds a miscellaneous ingredient. `type` is a BeerJSON miscellaneous type, and `amount` is a
 * mass, volume, or count in `amount_unit`. */
int kl_add_miscellaneous(kl_document *document, kl_recipe_id recipe, const char *name, const char *type,
                         const char *amount, const char *amount_unit, char **error);
int kl_remove_miscellaneous(kl_document *document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                            char **error);

/* Adds a hop. `alpha_percent` is the alpha acid in percent, `beta_percent` is the beta acid in
 * percent and may be NULL, and `amount` is a mass or volume in `amount_unit`. `form` (a BeerJSON hop form), `use` (a BeerJSON timing use, such as
 * "add_to_boil"), and `time` may be NULL. `time` is a whole number in `time_unit`, such as
 * "60" and "min", and is the time the bitterness calculation reads. */
int kl_add_hop(kl_document *document, kl_recipe_id recipe, const char *name, const char *alpha_percent,
               const char *beta_percent, const char *amount, const char *amount_unit, const char *form, const char *use, const char *time,
               const char *time_unit, char **error);
int kl_remove_hop(kl_document *document, kl_recipe_id recipe, kl_ingredient_id hop, char **error);

/* Adds a culture, such as a yeast or a bacterium. `type` and `form` are BeerJSON culture values
 * ("ale" and "dry", say), and `amount` is a mass, volume, or count in `amount_unit`.
 * `attenuation_percent` may be NULL. */
int kl_add_culture(kl_document *document, kl_recipe_id recipe, const char *name, const char *type,
                   const char *form, const char *amount, const char *amount_unit, const char *attenuation_percent,
                   char **error);
int kl_remove_culture(kl_document *document, kl_recipe_id recipe, kl_ingredient_id culture, char **error);

/* Adds a water. Each mineral is in mg/L. `amount_liters` may be NULL. */
int kl_add_water(kl_document *document, kl_recipe_id recipe, const char *name, const char *calcium,
                 const char *bicarbonate, const char *sulfate, const char *chloride, const char *sodium,
                 const char *magnesium, const char *amount_liters, char **error);
int kl_remove_water(kl_document *document, kl_recipe_id recipe, kl_ingredient_id water, char **error);

/* Sets the batch size (`batch_value` in `batch_unit`), the brewhouse efficiency (a percentage), and
 * the boil-off (an evaporation rate in percent per hour, and trub loss in liters), as one undo step. */
int kl_apply_equipment(kl_document *document, kl_recipe_id recipe, const char *batch_value, const char *batch_unit,
                       const char *efficiency_percent, const char *evaporation_percent_per_hour,
                       const char *trub_loss_liters, char **error);

/* A brew-day procedure, as JSON: an array of {"phase", "minute", "text", "warning"}. `minute` is the
 * minute into the boil for a boil step, or null. `system` is "separate tun" or "all in one". The boil
 * length is given by the user. Returns NULL on failure, with the message in `error`. Free the result
 * with kl_string_free. */
char *kl_procedure_json(const kl_document *document, kl_recipe_id recipe, int boil_minutes, const char *system,
                        char **error);

/* Deletes a recipe for good, with no folder to restore it from. Undo puts it back where it was. */
int kl_remove_recipe(kl_document *document, kl_recipe_id recipe, char **error);

/* Moves a recipe to the Recently Deleted folder, which is made if the document has none. Undo files the
 * recipe where it was. A recipe already there is refused. */
int kl_trash_recipe(kl_document *document, kl_recipe_id recipe, char **error);

/* Copies a recipe after the original, named "<name> copy". Its id is written to `created`. Undo removes the copy. */
int kl_duplicate_recipe(kl_document *document, kl_recipe_id recipe, kl_recipe_id *created, char **error);

/* Copies an ingredient within its recipe, after the original. Undo removes the copy. */
int kl_duplicate_fermentable(kl_document *document, kl_recipe_id recipe, kl_ingredient_id fermentable, char **error);
int kl_duplicate_hop(kl_document *document, kl_recipe_id recipe, kl_ingredient_id hop, char **error);
int kl_duplicate_culture(kl_document *document, kl_recipe_id recipe, kl_ingredient_id culture, char **error);
int kl_duplicate_water(kl_document *document, kl_recipe_id recipe, kl_ingredient_id water, char **error);
int kl_duplicate_miscellaneous(kl_document *document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                               char **error);

/* Replace an existing ingredient in place. Fields that the facade does not
 * edit keep their values. Each takes the same units and rules as its add
 * function, and `yield` and `color` are required for a fermentable. */
int kl_set_fermentable(kl_document *document, kl_recipe_id recipe, kl_ingredient_id fermentable, const char *name,
                       const char *type, const char *amount, const char *amount_unit, const char *yield,
                       const char *yield_unit, const char *color, const char *color_unit, char **error);
int kl_set_hop(kl_document *document, kl_recipe_id recipe, kl_ingredient_id hop, const char *name,
               const char *alpha_percent, const char *beta_percent, const char *amount, const char *amount_unit, const char *form,
               const char *use, const char *time, const char *time_unit, char **error);
int kl_set_culture(kl_document *document, kl_recipe_id recipe, kl_ingredient_id culture, const char *name,
                   const char *type, const char *form, const char *amount, const char *amount_unit,
                   const char *attenuation_percent, char **error);
int kl_set_water(kl_document *document, kl_recipe_id recipe, kl_ingredient_id water, const char *name,
                 const char *calcium, const char *bicarbonate, const char *sulfate, const char *chloride,
                 const char *sodium, const char *magnesium, const char *amount_liters, char **error);
int kl_set_miscellaneous(kl_document *document, kl_recipe_id recipe, kl_ingredient_id miscellaneous,
                         const char *name, const char *type, const char *amount, const char *amount_unit,
                         char **error);

int kl_set_recipe_name(kl_document *document, kl_recipe_id recipe, const char *name, char **error);
int kl_set_recipe_type(kl_document *document, kl_recipe_id recipe, const char *type_name, char **error);
int kl_set_batch_size(kl_document *document, kl_recipe_id recipe, const char *value, const char *unit, char **error);
int kl_set_fermentation_efficiency(kl_document *document, kl_recipe_id recipe, const char *percent, char **error);
int kl_set_boil_off(kl_document *document, kl_recipe_id recipe, const char *evaporation_percent_per_hour,
                    const char *trub_loss_liters, char **error);

/* Sets the style a recipe is compared against, by its id: a BJCP guideline id such as "21A", or a user style
 * id such as "custom-...". NULL means none. */
int kl_set_recipe_style(kl_document *document, kl_recipe_id recipe, const char *style_id, char **error);

/* User styles are beer styles the user defines, alongside the BJCP ones. A style is JSON:
 * {"name", "og", "fg", "ibu", "srm", "abv"}, where each range is {"low", "high"}. Each command is undoable.
 * kl_add_user_style sets *created_id to the new id, which begins with "custom-"; free it with kl_string_free.
 * Removing a style leaves any recipe that was compared against it with no style. */
int kl_add_user_style(kl_document *document, const char *style_json, char **created_id, char **error);
int kl_set_user_style(kl_document *document, const char *style_id, const char *style_json, char **error);
int kl_remove_user_style(kl_document *document, const char *style_id, char **error);
/* The document's user styles, as JSON: an array of {"id", "name", "og", "fg", "ibu", "srm", "abv"}.
 * Free the result with kl_string_free. NULL on failure. */
char *kl_user_styles_json(const kl_document *document, char **error);

/* Folders group recipes in the app. Each command is undoable. `folder` 0 means no folder. */
int kl_add_folder(kl_document *document, const char *name, kl_folder_id *created, char **error);
int kl_rename_folder(kl_document *document, kl_folder_id folder, const char *name, char **error);
/* Removes a folder. Its recipes move to the top level. Undo puts them back in it. */
int kl_remove_folder(kl_document *document, kl_folder_id folder, char **error);
/* Files a recipe in `folder`, or at the top level when `folder` is 0. */
int kl_move_recipe_to_folder(kl_document *document, kl_recipe_id recipe, kl_folder_id folder, char **error);

/* The engine's version, in semantic versioning, such as "0.1.0". The string belongs to the library: do not free it. */
const char *kl_engine_version(void);

/* The facade's binary interface. A front end built against one number can load only an engine that reports the
 * same number, so the front end and the engine can be updated apart. Bump it whenever a function is added, removed,
 * or changes its arguments or meaning. The macOS app's loader (see core/tools/gen_engine_loader.py) checks it. */
#define KL_ENGINE_ABI_VERSION 1
int kl_engine_abi_version(void);

/* The BJCP 2021 beer styles, as JSON: an array of {"id", "name", "og", "fg", "ibu", "srm", "abv"}, where
 * each range is {"low", "high"}. Free the result with kl_string_free. */
char *kl_styles_json(void);

/* The built-in ingredients that prefill the add dialogs, as JSON: {"fermentables", "hops", "cultures",
 * "waters", "miscellaneous"}, each an array of entries in menu order. Every entry has "name" and "group".
 * A fermentable adds "type", "yield_sg", and "color_srm". A hop adds "alpha_percent" and, when known,
 * "beta_percent". A culture adds "type", "form", and, when known, "attenuation_percent". A water adds
 * "calcium", "bicarbonate", "sulfate", "chloride", "sodium", and "magnesium", in mg/L. A miscellaneous
 * ingredient adds "type" and "unit". Values are decimal text. Free the result with kl_string_free. */
char *kl_ingredient_catalog_json(void);

/* Searches, filters, and sorts a list of ingredients. `request_json` is
 *   {"kind": "fermentable"|"hop"|"culture"|"water"|"miscellaneous",
 *    "entries": [{"id", "name", "group", "type", "form", "custom" (bool), "primary", "secondary"}],
 *    "text", "groups": [], "types": [], "forms": [], "source": "any"|"builtIn"|"custom",
 *    "minPrimary", "maxPrimary", "sort": "catalog"|"name"|"group"|"primary"|"secondary", "descending"}
 * Only "entries" is required. "type", "form", "primary", and "secondary" may be left out or null; the two numbers are
 * in the units the kind's metrics name. The result is {"indices": [...], "facets": {"groups", "types", "forms",
 * "primaryMin", "primaryMax"}, "metrics": {"primary": {"label", "unit"}|null, "secondary": ...}}, where "indices"
 * are positions in "entries" in the order to show them, and the facets and metrics describe all the entries, not
 * only those that passed. Free the result with kl_string_free. NULL when the request is not valid. */
char *kl_ingredient_query_json(const char *request_json);

/* The other built-in datasets, as JSON, by name. Free the result with kl_string_free. NULL for an unknown name.
 *  "equipment"            [{"name", "batchLiters", "efficiencyPercent", "evaporationPercent", "trubLossLiters",
 *                           "boilMinutes", "mashSystem", "note"}], the brewing systems. The figures are decimal text.
 *  "recipe_templates"     [{"id", "name", "summary", "bjcpStyle", "targetOG", "targetIBU", "boilMinutes",
 *                           "grains": [{"name", "percent"}], "boilHops": [{"name", "minutes", "ibuPercent"}],
 *                           "aromaHops": [{"name", "gramsPerLiter", "timing": {"kind": "boil"|"flameout"|"dryHop",
 *                           "value"}}], "yeast", "yeastAttenuationPercent" (when set), "mashTempC",
 *                           "fermentTempC": {"lower", "upper"}, "usesWhirlfloc", "tips"}], the starter recipes.
 *  "fermentation_presets" [{"name", "steps": [{"days", "tempC"}]}]
 *  "glossary"             [{"term", "explanation"}], the brewing terms, by their stable names.
 *  "tools"                {"calculators": [name], "converters": [name], "specs": {name: {"title", "sections":
 *                           [{"title", "footer" (when set), "tool", "fields": [{"key", "label", "kind":
 *                           "number"|"toggle"|"choice", "defaultValue", "choices": [{"value", "label"}],
 *                           "enabledBy"}], "readouts": [{"key", "label", "unit", "enabledBy"}]}]}}}
 *  "options"              {"mashStepKinds", "carbonationMethods", "currencyCodes", "recipeTypes", "fermentableTypes", "fermentableUnits",
 *                           "miscellaneousTypes", "miscellaneousUnits", "hopForms", "hopUses", "hopUnits",
 *                           "hopTimeUnits", "cultureTypes", "cultureForms", "cultureUnits"}, each a list of
 *                           BeerJSON names. The first entry is the default. */
char *kl_builtin_data_json(const char *dataset);

/* The units a price can be per, for an ingredient measured in `amount_unit` (such as "lb" or "pkg"), as a JSON
 * array of BeerJSON unit names. A price measures the same thing as the amount: mass, volume, or a count. An
 * unknown unit can only be priced per itself. Free the result with kl_string_free. NULL on failure. */
char *kl_price_units_json(const char *amount_unit);

/* A recipe's calculated values placed against its BJCP style, as JSON: {"style": {"id", "name"},
 * "checks": [...]}. Each check is {"metric", "value", "low", "high", "fit"}, where `metric` is
 * "originalGravity", "finalGravity", "abv", "ibu", or "srm", and `fit` is "below", "within", "above",
 * or "unknown". The result is "null" when the recipe has no style. Returns NULL on failure, with the
 * message in `error`. Free the result with kl_string_free. */
char *kl_style_comparison_json(const kl_document *document, kl_recipe_id recipe, char **error);

/* The brewing water, mash, and sparge plan for a recipe, as JSON. `inputs_json` is an object with
 * "baseWater" (calcium, magnesium, sodium, sulfate, chloride, bicarbonate, carbonate, in mg/L), "mashRatioLPerKg",
 * "absorptionLPerKg", "grainTempC", "tunTempC", "mashTempC", "spargeTempC", "deadspaceLiters",
 * "adjustForDeadspace", "leaveOutSalts", "holdSpargeSaltsUntilBoil", "salts" (kind, grams, sparge),
 * "acids" (kind, grams), "acidForTarget", "targetMashPh", "spargeAcid", "spargeAcidStrengthPercent",
 * "spargeStartPh", "spargeTargetPh", "roPercent" (the RO share of the base water), "roWater" (its ions), and
 * "targetWater" (the desired mash water, for "mashDifference").
 * Returns NULL on failure, with the message in `error`. Free the result with kl_string_free. */
char *kl_brewing_plan_json(const kl_document *document, kl_recipe_id recipe, const char *inputs_json, char **error);

/* The brew water calculations, as JSON, for water that is not in a recipe. `inputs_json` may hold any of:
 * "water" (the report, in mg/L: calcium, magnesium, sodium, sulfate, chloride, bicarbonate, carbonate, and
 * potassium, iron, nitrate, nitrite, fluoride), "conversion" ("alkalinity" as CaCO3 and "pH"), "mash"
 * ("waterLiters", "targetPh", "grains" of {"kilograms", "color", "colorUnit" ("Lovi", "EBC", or "SRM"),
 * "category" ("base", "crystal", "roast", or "acid")}, "acids" of {"kind", "strength", "grams"}, and
 * "targetAcid" of {"kind", "strength"}), and "sparge" ("liters", "startPh", "targetPh", "acid", "strength").
 * Each value is a JSON number, and the result holds the parts you asked for:
 * "report", "conversion", "mash", and "sparge". Returns NULL on failure, with the message in `error`. Free the
 * result with kl_string_free. */
char *kl_brew_water_json(const char *inputs_json, char **error);

/* The brew journal's calculations for a recipe, as JSON. `inputs_json` holds the measured values:
 * "postMashGravity", "preBoilLiters", "preBoilGravity", "batchLiters", "originalGravity", "finalGravity",
 * "bottlingLiters", and "carbonationVolumes". Any may be null. Free the result with kl_string_free. */
char *kl_journal_plan_json(const kl_document *document, kl_recipe_id recipe, const char *inputs_json,
                           char **error);

/* Sets what an ingredient costs: `amount` for each one `per_unit`, such as "4.5" and "kg". `kind` is
 * "fermentable", "hop", "culture", or "miscellaneous". `per_unit` must measure the same thing as the
 * ingredient's amount. A NULL `amount` clears the price. One undo step. */
int kl_set_price(kl_document *document, kl_recipe_id recipe, const char *kind, kl_ingredient_id ingredient,
                 const char *amount, const char *per_unit, char **error);

/* Sets the currency the recipe's prices are in, a three-letter code such as "USD". One undo step. */
int kl_set_recipe_currency(kl_document *document, kl_recipe_id recipe, const char *currency, char **error);

/* Stores the app's planning JSON for a recipe, as one undo step. The engine does not read it. */
int kl_set_recipe_planning(kl_document *document, kl_recipe_id recipe, const char *planning, char **error);

/* A yeast plan for the recipe, as JSON. The numbers are exact decimal text, such as "100" or "1.040".
 * `lager` and `two_stage` and `add_to_bottling` are 0 or 1. Returns NULL on failure, with the message
 * in `error`. Free the result with kl_string_free. */
char *kl_starter_plan_json(const kl_document *document, kl_recipe_id recipe, int lager,
                           const char *pack_cells_billion, const char *growth_billion_per_liter_plato,
                           const char *starter_liters, const char *starter_gravity, int two_stage,
                           const char *first_stage_liters, int add_to_bottling, char **error);

/* Groups the edits made between kl_group_begin and kl_group_end into one undo step, called `label`.
 * kl_group_cancel undoes them instead, and leaves no redo. Groups do not nest: kl_group_begin fails
 * while a group is open, and the other two fail when none is. A group left open when the document is
 * freed is simply dropped. */
int kl_group_begin(kl_document *document, char **error);
int kl_group_end(kl_document *document, const char *label, char **error);
int kl_group_cancel(kl_document *document, char **error);

/* Undo and redo return 1 when they did something, 0 when there was nothing to do. */
int kl_undo(kl_document *document, char **error);
int kl_redo(kl_document *document, char **error);
int kl_can_undo(const kl_document *document);
int kl_can_redo(const kl_document *document);

/* Menu labels, such as "Set Batch Size". An empty string means nothing to undo or redo. */
char *kl_undo_label(const kl_document *document);
char *kl_redo_label(const kl_document *document);

/* A brewing calculator or unit converter, by name, such as "water.profile", "hop.bitterness", or
 * "convert.volume". `inputs_json` is a JSON object whose values are text: a number as decimal text
 * ("5.25") and a unit by its BeerJSON or tool name ("F", "gal", "psi"). Returns a JSON object
 * {"results": {name: number}, "notes": [text]}, or NULL on failure with the message in `error`. The
 * names and their inputs are in docs/calc.md, "Tools". Free the result with kl_string_free. */
char *kl_tool_json(const char *tool, const char *inputs_json, char **error);

/* A recipe exported as text, in `format`: "beerxml" (BeerXML 2.01), "beerjson" (BeerJSON), or "text"
 * (a readable summary). Returns NULL on failure, with the message in `error`. Free the result with
 * kl_string_free. */
char *kl_recipe_export(const kl_document *document, kl_recipe_id recipe, const char *format, char **error);

/* Writes a recipe to `path` as a PDF: its estimates, ingredients, mash, fermentation, and notes, for reading and
 * printing. It is not the brew sheet, which kl_brew_sheet_write_pdf writes. Returns 0 on success, or nonzero with
 * the message in `error`. */
int kl_recipe_write_pdf(const kl_document *document, kl_recipe_id recipe, const char *path, char **error);

/* Reads a BeerXML 1.0 or 2.01 document (a RECIPES root, or one RECIPE) and converts it to BeerJSON. Returns
 * JSON {"beerjson": {...}, "warnings": [{"pointer", "message"}]}: the BeerJSON document, and the tags that were
 * skipped and the optional fields that were filled in. Returns NULL on failure, with the message in `error`,
 * for example for a malformed file or a missing required field. Free the result with kl_string_free. */
char *kl_beerxml_to_beerjson(const char *beerxml, char **error);

/* The brew sheet for a recipe: the boxes a brewer fills in on brew day, in order, with their codes, kinds,
 * units, and the figures the recipe expects. `imperial` is nonzero for gallons and degrees Fahrenheit. Returns
 * JSON {"recipeName", "batchText", "units", "sections": [{"code", "title", "boxes": [{"code", "label", "kind",
 * "unit", "expected", "expectedText"}]}]}, or NULL on failure. Free the result with kl_string_free. */
char *kl_brew_sheet_json(const kl_document *document, kl_recipe_id recipe, int imperial, char **error);

/* Writes the brew sheet for a recipe to `path` as a PDF. Returns 0 on success, or nonzero with the message in
 * `error`. */
int kl_brew_sheet_write_pdf(const kl_document *document, kl_recipe_id recipe, int imperial, const char *path,
                            char **error);

/* Reads the text typed into the boxes of a brew sheet. `entries_json` is a JSON object from box code to the
 * text in the box. Returns JSON {"numbers": {code: value in SI}, "dates": {code: "yyyy-MM-dd"}, "issues":
 * [{"code", "message"}]}, or NULL on failure. Volumes are in liters and temperatures in degrees C. Free the
 * result with kl_string_free. */
char *kl_brew_sheet_read_json(const kl_document *document, kl_recipe_id recipe, int imperial,
                              const char *entries_json, char **error);

void kl_string_free(char *string);

#ifdef __cplusplus
}
#endif

#endif /* KETTLELOGIC_C_H */
