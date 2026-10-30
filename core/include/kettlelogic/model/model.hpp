#pragma once

#include "kettlelogic/json.hpp"
#include "kettlelogic/measure.hpp"
#include "kettlelogic/quantity.hpp"
#include "kettlelogic/rational.hpp"
#include "kettlelogic/styles/style.hpp"
#include "kettlelogic/uuid.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The recipe model. Field names follow the BeerJSON schema (see
// core/schemas/beerjson/). Fields typed here are read and written by the
// BeerJSON reader and writer. Keys the model does not type yet are kept in
// each struct's `preserved` list, so a read-then-write never drops data.
namespace kettlelogic::model {

// Stable identity for entities that commands edit. Ids are allocated by the
// Document and never reused within a session.
using EntityId = std::uint64_t;

struct Timing {
    std::optional<Quantity> time;
    std::optional<Quantity> duration;
    std::optional<bool> continuous;
    std::optional<Measure> specificGravity;
    std::optional<Measure> pH;
    std::optional<long long> step;
    std::optional<std::string> use;
    json::Object preserved;
};

struct Yield {
    std::optional<Quantity> fineGrind;
    std::optional<Quantity> coarseGrind;
    std::optional<Quantity> fineCoarseDifference;
    std::optional<Measure> potential;
    json::Object preserved;

    // The schema requires at least one property in a yield.
    bool empty() const {
        return !fineGrind && !coarseGrind && !fineCoarseDifference && !potential && preserved.empty();
    }
};

// Kettle Logic data, not BeerJSON: what an ingredient costs, as `amount` for each one `per`,
// such as 4.50 for each kg or 3.25 for each package. Stored in the sidecar, in the recipe's
// currency. `per` has the same dimension as the ingredient's amount.
struct Price {
    Rational amount;
    Unit per = Unit::Kilogram;
};

struct Fermentable {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    std::string type;
    std::optional<std::string> origin;
    std::optional<std::string> producer;
    std::optional<std::string> productId;
    std::optional<std::string> grainGroup;
    Yield yield;
    Measure color;
    Quantity amount;
    std::optional<Timing> timing;
    std::optional<Price> price;  // Kettle Logic data; see Price
    json::Object preserved;
};

struct Hop {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    Quantity alphaAcid;
    std::optional<Quantity> betaAcid;
    std::optional<std::string> producer;
    std::optional<std::string> productId;
    std::optional<std::string> origin;
    std::optional<std::string> year;
    std::optional<std::string> form;
    Quantity amount;
    Timing timing;
    std::optional<Price> price;  // Kettle Logic data; see Price
    json::Object preserved;
};

struct Culture {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    std::string type;
    std::string form;
    std::optional<std::string> producer;
    std::optional<std::string> productId;
    std::optional<Quantity> attenuation;
    std::optional<long long> timesCultured;
    std::optional<long long> cellCountBillions;
    Quantity amount;
    std::optional<Timing> timing;
    std::optional<Price> price;  // Kettle Logic data; see Price
    json::Object preserved;
};

// The water salts the water calculation knows how to add.
enum class SaltKind { Gypsum, CalciumChloride, EpsomSalt, BakingSoda };

// What a miscellaneous ingredient does in a drink, where BeerJSON cannot say.
// Kettle Logic data, stored in the sidecar.
enum class Additive {
    Sulfite,        // potassium metabisulfite, 57.6 % SO2 by mass
    TartaricAcid,   // adds titratable acidity, as tartaric acid
};

struct Miscellaneous {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    std::string type;
    std::optional<std::string> producer;
    std::optional<std::string> productId;
    Quantity amount;
    std::optional<Timing> timing;
    // Kettle Logic data, not BeerJSON: which salt this is, if any. Stored in the
    // sidecar. The name is never used to decide it.
    std::optional<SaltKind> salt;
    // Kettle Logic data: what the ingredient does in a drink. Stored in the sidecar.
    std::optional<Additive> additive;
    std::optional<Price> price;  // Kettle Logic data; see Price
    json::Object preserved;
};

// Water chemistry. Used for root water profiles and for water additions.
struct WaterProfile {
    std::string name;
    std::optional<std::string> producer;
    Quantity calcium;
    Quantity bicarbonate;
    Quantity sulfate;
    Quantity chloride;
    Quantity sodium;
    Quantity magnesium;
    std::optional<Quantity> carbonate;
    std::optional<Quantity> potassium;
    std::optional<Quantity> iron;
    std::optional<Quantity> nitrate;
    std::optional<Quantity> nitrite;
    std::optional<Quantity> fluoride;
    json::Object preserved;
};

struct WaterAddition {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    WaterProfile water;
    std::optional<Quantity> amount;
};

struct MashStep {
    std::string name;
    std::string type;
    std::optional<Quantity> amount;
    Quantity stepTemperature;
    Quantity stepTime;
    std::optional<Quantity> rampTime;
    std::optional<Quantity> endTemperature;
    std::optional<std::string> description;
    std::optional<Measure> waterGrainRatio;
    std::optional<Quantity> infuseTemperature;
    std::optional<Measure> startPh;
    std::optional<Measure> endPh;
    json::Object preserved;
};

struct Mash {
    std::string name;
    Quantity grainTemperature;
    std::optional<std::string> notes;
    std::vector<MashStep> steps;
    json::Object preserved;
};

struct FermentationStep {
    std::string name;
    std::optional<std::string> description;
    std::optional<Quantity> startTemperature;
    std::optional<Quantity> endTemperature;
    std::optional<Quantity> stepTime;
    std::optional<bool> freeRise;
    std::optional<Measure> startGravity;
    std::optional<Measure> endGravity;
    std::optional<Measure> startPh;
    std::optional<Measure> endPh;
    std::optional<std::string> vessel;
    std::optional<Measure> vesselPressure;
    json::Object preserved;
};

struct Fermentation {
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> notes;
    std::vector<FermentationStep> steps;
    json::Object preserved;
};

struct BoilStep {
    std::string name;
    std::optional<std::string> description;
    std::optional<Quantity> startTemperature;
    std::optional<Quantity> endTemperature;
    std::optional<Quantity> rampTime;
    std::optional<Quantity> stepTime;
    std::optional<Measure> startGravity;
    std::optional<Measure> endGravity;
    std::optional<Measure> startPh;
    std::optional<Measure> endPh;
    std::optional<std::string> chillingType;
    json::Object preserved;
};

struct Boil {
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> notes;
    std::optional<Quantity> preBoilSize;
    Quantity boilTime;
    // Optional in the schema, so it follows the same presence rule as the
    // ingredient lists: nullopt is absent, an empty list is present and empty.
    std::optional<std::vector<BoilStep>> steps;
    json::Object preserved;
};

struct Efficiency {
    std::optional<Quantity> conversion;
    std::optional<Quantity> lauter;
    std::optional<Quantity> mash;
    // A percentage, like the others. The default is a typical brewhouse, the same as a new recipe in the app.
    Quantity brewhouse = Quantity::fromUnit(Rational(72), Unit::Percent);
    json::Object preserved;
};

// How the boil loses volume, for working out the boil's volumes. BeerJSON has
// no place for these, so they are Kettle Logic data, kept in the sidecar. The
// defaults are a typical homebrew kettle; set them from measurements.
struct BoilOff {
    Quantity evaporationRate = Quantity::fromUnit(Rational(10), Unit::Percent);  // of the pre-boil volume, per hour
    Quantity trubLoss = Quantity::fromUnit(Rational(1), Unit::Liter);            // to trub and the chiller, after the boil
};

// The kind of beverage a recipe makes. The names are BeerJSON's recipe types.
enum class RecipeType { Cider, Kombucha, Soda, Other, Mead, Wine, Extract, PartialMash, AllGrain };

// BeerJSON's name for a recipe type, and the reverse.
std::string_view recipeTypeName(RecipeType type);
std::optional<RecipeType> recipeTypeFromName(std::string_view name);

struct Recipe {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    RecipeType type = RecipeType::AllGrain;
    std::string author;
    std::optional<std::string> coauthor;
    std::optional<std::string> created;
    Quantity batchSize;
    Efficiency efficiency;
    BoilOff boilOff;  // Kettle Logic data, not BeerJSON; see BoilOff
    // Kettle Logic data: the share of the sugar that yeast turns into alcohol, used to plan
    // drinks. 100 % is the theoretical ceiling. The default is a typical yeast, not a measurement.
    Quantity fermentationEfficiency = Quantity::fromUnit(Rational(92), Unit::Percent);
    // Kettle Logic data: the folder the recipe is filed in, or none for the top level, and the
    // BJCP style it is compared against, by guideline id such as "21A". Both are stored in the sidecar.
    std::optional<EntityId> folder;
    std::optional<std::string> bjcpStyle;
    // Kettle Logic data: the app's planning inputs for this recipe (yeast starter, water, mash, and
    // so on), as JSON text the engine stores but does not read. Stored in the sidecar.
    std::string planning;
    // Kettle Logic data: the currency every ingredient price in this recipe is in, as an ISO 4217
    // code. The engine never converts between currencies. Stored in the sidecar.
    std::string currency = "USD";

    // Required by the schema, so always present (possibly empty).
    std::vector<Fermentable> fermentables;

    // Optional in the schema. nullopt means the list is absent from the
    // document; an engaged but empty list means it is present and empty.
    // Commands add and remove the presence (see setIngredientListPresent).
    std::optional<std::vector<Hop>> hops;
    std::optional<std::vector<Miscellaneous>> miscellaneous;
    std::optional<std::vector<Culture>> cultures;
    std::optional<std::vector<WaterAddition>> waters;

    json::Object ingredientsPreserved;

    std::optional<Mash> mash;
    std::optional<Fermentation> fermentation;
    std::optional<Boil> boil;

    std::optional<std::string> notes;
    std::optional<Measure> originalGravity;
    std::optional<Measure> finalGravity;
    std::optional<Quantity> alcoholByVolume;
    std::optional<Measure> colorEstimate;
    std::optional<Measure> beerPh;
    std::optional<Quantity> apparentAttenuation;
    std::optional<Rational> carbonation;
    std::optional<Rational> caloriesPerPint;

    // Typed later: style, ibu_estimate, packaging, taste.
    json::Object preserved;
};

// A problem found while reading. It does not stop the read.
struct Diagnostic {
    std::string pointer;  // JSON Pointer of the value the note is about
    std::string message;
};

// The JSON Pointer of `key` inside the object at `parent` (RFC 6901 escaping).
inline std::string jsonPointerChild(const std::string& parent, std::string_view key) {
    std::string token;
    for (const char c : key) {
        if (c == '~') {
            token += "~0";
        } else if (c == '/') {
            token += "~1";
        } else {
            token.push_back(c);
        }
    }
    return parent + "/" + token;
}

// A property the BeerJSON schema does not define, in an object the schema
// closes to such properties. It is kept here rather than in the model, and it
// is not written to BeerJSON output, because writing it would make the file
// invalid. The sidecar file stores it (see sidecar.hpp).
struct ForeignProperty {
    // Where it was: the parent object's JSON Pointer, the key, and the parent's
    // "name" if it had a string one. The name is the fingerprint used to check
    // the place when the sidecar is opened again.
    std::string parent;
    std::string key;
    std::optional<std::string> parentName;
    json::Value value;

    // The entity that contains the parent, and the parent's JSON Pointer
    // relative to that entity (for example "/batch_size"). The owner is nil
    // when no entity contains the parent, and then `path` is the parent's
    // absolute pointer. Identity follows the owner, so the link survives
    // reordering and renaming.
    Uuid owner;
    std::string path;

    std::string pointer() const { return jsonPointerChild(parent, key); }
};

// A folder that groups recipes in the app. Kettle Logic data, not BeerJSON: it is
// stored in the sidecar, and a recipe refers to it by id.
struct Folder {
    EntityId id = 0;  // session handle
    Uuid uuid;        // identity kept across saves
    std::string name;
    // The Recently Deleted folder, which holds the recipes the user deleted. It is made the
    // first time a recipe is deleted, and it cannot be renamed or removed.
    bool trash = false;
};

class Document {
public:
    // The newest BeerJSON version the vendored schema accepts.
    Rational version{3};
    std::vector<Recipe> recipes;
    std::vector<Folder> folders;  // in the order the user made them
    std::vector<styles::UserStyle> userStyles;  // in the order the user made them

    // Every root-level key other than `version` and `recipes` that the schema
    // defines, kept as authored (catalog records, packaging, and so on)
    // until they are modeled.
    json::Object preserved;

    // Warnings from the last read. Read results are not errors unless the
    // reader throws.
    std::vector<Diagnostic> diagnostics;

    // Properties the schema does not define. saveDocument stores them in the
    // sidecar, never in the BeerJSON file.
    std::vector<ForeignProperty> foreign;

    EntityId allocateId() { return nextId_++; }

    Recipe* findRecipe(EntityId id);
    const Recipe* findRecipe(EntityId id) const;
    Folder* findFolder(EntityId id);
    const Folder* findFolder(EntityId id) const;
    // The Recently Deleted folder, or nullptr if no recipe has been deleted yet.
    Folder* trashFolder();
    const Folder* trashFolder() const;
    styles::UserStyle* findUserStyle(std::string_view id);
    const styles::UserStyle* findUserStyle(std::string_view id) const;

private:
    EntityId nextId_ = 1;
};

}  // namespace kettlelogic::model
