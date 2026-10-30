#include "kettlelogic/app/engine.hpp"

#include "kettlelogic/app/text.hpp"
#include "kettlelogic/catalog/options.hpp"
#include "kettlelogic/io/document_file.hpp"
#include "kettlelogic/io/recipe_export.hpp"
#include "kettlelogic/io/recipe_pdf.hpp"
#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/model/sidecar.hpp"
#include "kettlelogic/quantity.hpp"

#include <algorithm>
#include <cstdlib>

namespace kettlelogic::app {

using model::Document;
using model::Recipe;

namespace {

// A recipe a new document starts with. It passes the schema's required rules.
constexpr const char* kNewDocument = R"({"beerjson": {"version": 3, "recipes": [{
    "name": "New Recipe", "type": "all grain", "author": "Unknown",
    "batch_size": {"value": 5, "unit": "gal"},
    "efficiency": {"brewhouse": {"value": 72, "unit": "%"}},
    "ingredients": {"fermentable_additions": []}}]}})";

std::string newDocumentId() {
    return Uuid::generate().toString();
}

// Runs `fn`, and reports any engine failure as an EngineError with its message.
template <typename Fn>
auto guarded(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const EngineError&) {
        throw;
    } catch (const std::exception& e) {
        throw EngineError(e.what());
    }
}

Quantity quantityFrom(const std::string& value, const std::string& unitName) {
    const auto unit = unitFromBeerJson(unitName);
    if (!unit) {
        throw std::invalid_argument("unknown unit \"" + unitName + "\"");
    }
    return Quantity::fromUnit(Rational::fromDecimal(value), *unit);
}

std::optional<Quantity> optionalQuantity(const std::optional<std::string>& value, const std::string& unitName) {
    if (!value) {
        return std::nullopt;
    }
    return quantityFrom(*value, unitName);
}

Measure measureFrom(const std::string& value, const std::string& unitName) {
    return Measure::fromUnit(Rational::fromDecimal(value), unitName);
}

template <typename T>
T itemIn(const std::vector<T>* list, EntityId id, const char* noun) {
    if (list != nullptr) {
        for (const T& item : *list) {
            if (item.id == id) {
                return item;
            }
        }
    }
    throw std::invalid_argument(std::string("no ") + noun + " with id " + std::to_string(id));
}

template <typename T>
const std::vector<T>* listIn(const std::optional<std::vector<T>>& list) {
    return list ? &*list : nullptr;
}

model::IngredientKind pricedKind(IngredientKind kind) {
    switch (kind) {
        case IngredientKind::fermentable: return model::IngredientKind::Fermentable;
        case IngredientKind::hop: return model::IngredientKind::Hop;
        case IngredientKind::culture: return model::IngredientKind::Culture;
        case IngredientKind::miscellaneous: return model::IngredientKind::Miscellaneous;
        case IngredientKind::water: break;
    }
    throw std::invalid_argument("water is not an ingredient that has a price");
}

// A number the user typed. Throws if it is not a number.
double typedNumber(const std::string& text, const char* what) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        throw std::invalid_argument(std::string(what) + " must be a number");
    }
    return value;
}

}  // namespace

// MARK: Lists and names

const std::vector<IngredientKind>& allIngredientKinds() {
    static const std::vector<IngredientKind> kinds = {
        IngredientKind::fermentable, IngredientKind::hop, IngredientKind::culture, IngredientKind::water,
        IngredientKind::miscellaneous};
    return kinds;
}

std::string ingredientKindTitle(IngredientKind kind) {
    switch (kind) {
        case IngredientKind::fermentable: return "Fermentable";
        case IngredientKind::hop: return "Hop";
        case IngredientKind::culture: return "Culture";
        case IngredientKind::water: return "Water";
        case IngredientKind::miscellaneous: return "Miscellaneous";
    }
    return "";
}

std::string ingredientKindName(IngredientKind kind) {
    switch (kind) {
        case IngredientKind::fermentable: return "fermentable";
        case IngredientKind::hop: return "hop";
        case IngredientKind::culture: return "culture";
        case IngredientKind::water: return "water";
        case IngredientKind::miscellaneous: return "miscellaneous";
    }
    return "";
}

std::optional<IngredientKind> ingredientKindFromName(const std::string& name) {
    for (const IngredientKind kind : allIngredientKinds()) {
        if (ingredientKindName(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

QuantityText describe(const Quantity& quantity) {
    return {quantity.valueIn(quantity.unit()).toDecimal(), std::string(beerJsonName(quantity.unit()))};
}

QuantityText describe(const Measure& measure) {
    return {measure.value().toDecimal(), measure.unit()};
}

std::optional<PriceText> describe(const std::optional<model::Price>& price) {
    if (!price) {
        return std::nullopt;
    }
    return PriceText{price->amount.toDecimal(), std::string(beerJsonName(price->per))};
}

const std::vector<std::string>& currencyCodes() { return catalog::currencyCodes(); }
const std::vector<std::string>& recipeTypes() { return catalog::recipeTypes(); }
const std::vector<std::string>& fermentableTypes() { return catalog::fermentableTypes(); }
const std::vector<std::string>& fermentableUnits() { return catalog::fermentableUnits(); }
const std::vector<std::string>& miscellaneousTypes() { return catalog::miscellaneousTypes(); }
const std::vector<std::string>& miscellaneousUnits() { return catalog::miscellaneousUnits(); }
const std::vector<std::string>& hopForms() { return catalog::hopForms(); }
const std::vector<std::string>& hopUnits() { return catalog::hopUnits(); }
const std::vector<std::string>& hopTimeUnits() { return catalog::hopTimeUnits(); }
const std::vector<std::string>& hopUses() { return catalog::hopUses(); }
const std::vector<std::string>& cultureTypes() { return catalog::cultureTypes(); }
const std::vector<std::string>& cultureForms() { return catalog::cultureForms(); }
const std::vector<std::string>& cultureUnits() { return catalog::cultureUnits(); }

std::string hopUseName(const std::string& use) {
    if (use == "add_to_boil") return "Boil";
    if (use == "add_to_mash") return "Mash";
    if (use == "add_to_fermentation") return "Fermentation";
    if (use == "add_to_package") return "Package";
    if (use.empty()) return "Not set";
    return use;
}

std::string hopFormName(const std::string& form) {
    return form.empty() ? "Not set" : form;
}

std::vector<std::string> priceUnits(const std::string& amountUnit) { return catalog::priceUnits(amountUnit); }

std::vector<std::string> options(const std::vector<std::string>& base, const std::string& current) {
    if (std::find(base.begin(), base.end(), current) != base.end()) {
        return base;
    }
    std::vector<std::string> out = {current};
    out.insert(out.end(), base.begin(), base.end());
    return out;
}

// MARK: RecipeDocument

std::shared_ptr<RecipeDocument> RecipeDocument::makeNew() {
    return guarded([&] {
        std::shared_ptr<RecipeDocument> doc(new RecipeDocument);
        doc->id_ = newDocumentId();
        doc->document_ = model::readBeerJson(kNewDocument);
        doc->refresh();
        return doc;
    });
}

std::shared_ptr<RecipeDocument> RecipeDocument::open(const std::filesystem::path& file) {
    return guarded([&] {
        auto loaded = io::loadDocument(file);
        std::shared_ptr<RecipeDocument> doc(new RecipeDocument);
        doc->id_ = newDocumentId();
        doc->file_ = file;
        doc->document_ = std::move(loaded.document);
        switch (loaded.source) {
            case io::Source::Current: doc->source_ = "current"; break;
            case io::Source::Autosave: doc->source_ = "autosave"; break;
            case io::Source::Previous: doc->source_ = "previous"; break;
        }
        doc->refresh();
        doc->rememberSaved();
        return doc;
    });
}

std::string RecipeDocument::displayName() const {
    return file_ ? file_->stem().string() : "New Recipe";
}

void RecipeDocument::save(const std::filesystem::path& file) {
    guarded([&] {
        io::saveDocument(document_, file);
        source_ = "current";
        file_ = file;
        // Saving changes where the document is, not what the recipes hold, so the calculations stay as they are.
        rememberSaved();
    });
}

const Recipe& RecipeDocument::require(EntityId id) const {
    const Recipe* found = document_.findRecipe(id);
    if (found == nullptr) {
        throw EngineError("no recipe with id " + std::to_string(id));
    }
    return *found;
}

const calc::Results& RecipeDocument::results(EntityId id) const {
    auto it = results_.find(id);
    if (it == results_.end()) {
        it = results_.emplace(id, calc::calculate(require(id))).first;
    }
    return it->second;
}

std::string RecipeDocument::fingerprint(const Recipe& recipe) const {
    // The recipe on its own, as the engine would save it: the BeerJSON text and the sidecar, which holds the
    // app's data. Two recipes with the same text are the same recipe.
    Document single;
    single.version = document_.version;
    single.recipes = {recipe};
    single.folders = document_.folders;
    const model::Saved saved = model::saveDocument(single);
    return saved.beerJson + "\n" + saved.sidecar;
}

void RecipeDocument::rememberSaved() {
    saved_ = current_;
}

bool RecipeDocument::isUnsaved(EntityId recipe) const {
    const auto saved = saved_.find(recipe);
    const auto current = current_.find(recipe);
    return saved == saved_.end() || current == current_.end() || saved->second != current->second;
}

void RecipeDocument::refresh() {
    ++revision_;
    results_.clear();
    current_.clear();
    for (const Recipe& recipe : document_.recipes) {
        try {
            current_[recipe.id] = fingerprint(recipe);
        } catch (const std::exception&) {
            current_[recipe.id] = "unreadable " + std::to_string(revision_);  // never equal to a saved copy
        }
    }
}

void RecipeDocument::execute(model::EditPtr edit) {
    guarded([&] { stack_.execute(document_, std::move(edit)); });
    refresh();
}

bool RecipeDocument::undo() {
    const bool done = guarded([&] { return stack_.undo(document_); });
    refresh();
    return done;
}

bool RecipeDocument::redo() {
    const bool done = guarded([&] { return stack_.redo(document_); });
    refresh();
    return done;
}

void RecipeDocument::grouped(const std::string& label, const std::function<void()>& work) {
    if (groupDepth_) {
        throw EngineError("an edit group is already open");
    }
    const std::size_t depth = stack_.undoDepth();
    groupDepth_ = depth;
    stack_.seal();
    try {
        work();
    } catch (...) {
        groupDepth_.reset();
        try {
            stack_.rollbackTo(document_, depth);
        } catch (...) {
            // The rollback itself failed. The original error is the one to report.
        }
        refresh();
        throw;
    }
    groupDepth_.reset();
    stack_.groupSince(depth, label);
    refresh();
}

// MARK: Recipe commands

void RecipeDocument::rename(EntityId recipe, const std::string& name) {
    execute(model::setRecipeName(recipe, name));
}

void RecipeDocument::setType(EntityId recipe, const std::string& type) {
    const auto parsed = model::recipeTypeFromName(type);
    if (!parsed) {
        throw EngineError("unknown recipe type \"" + type + "\"");
    }
    execute(model::setRecipeType(recipe, *parsed));
}

void RecipeDocument::setBatchSize(EntityId recipe, const std::string& value, const std::string& unit) {
    guarded([&] { execute(model::setBatchSize(recipe, quantityFrom(value, unit))); });
}

void RecipeDocument::setFermentationEfficiency(EntityId recipe, const std::string& percent) {
    guarded([&] { execute(model::setFermentationEfficiency(recipe, quantityFrom(percent, "%"))); });
}

void RecipeDocument::setBoilOff(EntityId recipe, const std::string& evaporationPercent,
                                const std::string& trubLossLiters) {
    guarded([&] {
        model::BoilOff boilOff;
        boilOff.evaporationRate = quantityFrom(evaporationPercent, "%");
        boilOff.trubLoss = quantityFrom(trubLossLiters, "l");
        execute(model::setBoilOff(recipe, boilOff));
    });
}

void RecipeDocument::applyEquipment(EntityId recipe, const std::string& batchValue, const std::string& batchUnit,
                                    const std::string& efficiencyPercent, const std::string& evaporationPercent,
                                    const std::string& trubLossLiters) {
    guarded([&] {
        model::BoilOff boilOff;
        boilOff.evaporationRate = quantityFrom(evaporationPercent, "%");
        boilOff.trubLoss = quantityFrom(trubLossLiters, "l");
        execute(model::applyEquipment(recipe, quantityFrom(batchValue, batchUnit),
                                      quantityFrom(efficiencyPercent, "%"), boilOff));
    });
}

EntityId RecipeDocument::addRecipe() {
    return guarded([&] {
        Recipe recipe;
        recipe.name = "New Recipe";
        recipe.author = "Unknown";
        recipe.batchSize = quantityFrom("5", "gal");
        recipe.efficiency.brewhouse = quantityFrom("72", "%");
        execute(model::addRecipe(std::move(recipe)));
        return document_.recipes.back().id;
    });
}

void RecipeDocument::removeRecipe(EntityId recipe) {
    execute(model::removeRecipe(recipe));
}

void RecipeDocument::trashRecipe(EntityId recipe) {
    execute(model::trashRecipe(recipe));
}

bool RecipeDocument::isDeleted(EntityId recipe) const {
    const model::Recipe* found = document_.findRecipe(recipe);
    if (found == nullptr || !found->folder) {
        return false;
    }
    const model::Folder* folder = document_.findFolder(*found->folder);
    return folder != nullptr && folder->trash;
}

EntityId RecipeDocument::duplicateRecipe(EntityId recipe) {
    return guarded([&] {
        execute(model::duplicateRecipe(recipe));
        // The copy is placed right after the original.
        const auto& recipes = document_.recipes;
        const auto it = std::find_if(recipes.begin(), recipes.end(),
                                     [recipe](const Recipe& r) { return r.id == recipe; });
        return (it + 1)->id;
    });
}

void RecipeDocument::duplicateIngredient(IngredientKind kind, EntityId recipe, EntityId item) {
    switch (kind) {
        case IngredientKind::fermentable: execute(model::duplicateFermentable(recipe, item)); break;
        case IngredientKind::hop: execute(model::duplicateHop(recipe, item)); break;
        case IngredientKind::culture: execute(model::duplicateCulture(recipe, item)); break;
        case IngredientKind::water: execute(model::duplicateWater(recipe, item)); break;
        case IngredientKind::miscellaneous: execute(model::duplicateMiscellaneous(recipe, item)); break;
    }
}

// MARK: Ingredient commands

namespace {

model::Fermentable fermentableFrom(model::Fermentable base, const FermentableInput& in) {
    base.name = in.name;
    base.type = in.type;
    base.amount = quantityFrom(in.amount, in.unit);
    base.yield.potential = measureFrom(in.yieldValue, in.yieldUnit);
    base.color = measureFrom(in.colorValue, in.colorUnit);
    return base;
}

model::Hop hopFrom(model::Hop base, const HopInput& in) {
    base.name = in.name;
    base.alphaAcid = quantityFrom(in.alphaPercent, "%");
    base.betaAcid = optionalQuantity(in.betaPercent, "%");
    base.amount = quantityFrom(in.amount, in.unit);
    base.form = in.form;
    base.timing.use = in.use;
    if (in.time && !in.timeUnit) {
        throw std::invalid_argument("a hop time needs a unit");
    }
    base.timing.time = in.time ? optionalQuantity(in.time, *in.timeUnit) : std::nullopt;
    return base;
}

model::Culture cultureFrom(model::Culture base, const CultureInput& in) {
    base.name = in.name;
    base.type = in.type;
    base.form = in.form;
    base.amount = quantityFrom(in.amount, in.unit);
    base.attenuation = optionalQuantity(in.attenuationPercent, "%");
    return base;
}

model::WaterAddition waterFrom(model::WaterAddition base, const WaterInput& in) {
    base.water.name = in.name;
    base.water.calcium = quantityFrom(in.calcium, "mg/l");
    base.water.bicarbonate = quantityFrom(in.bicarbonate, "mg/l");
    base.water.sulfate = quantityFrom(in.sulfate, "mg/l");
    base.water.chloride = quantityFrom(in.chloride, "mg/l");
    base.water.sodium = quantityFrom(in.sodium, "mg/l");
    base.water.magnesium = quantityFrom(in.magnesium, "mg/l");
    base.amount = optionalQuantity(in.amountLiters, "l");
    return base;
}

model::Miscellaneous miscellaneousFrom(model::Miscellaneous base, const MiscellaneousInput& in) {
    base.name = in.name;
    base.type = in.type;
    base.amount = quantityFrom(in.amount, in.unit);
    return base;
}

}  // namespace

void RecipeDocument::addFermentable(EntityId recipe, const FermentableInput& in) {
    guarded([&] { execute(model::addFermentable(recipe, fermentableFrom({}, in))); });
}

void RecipeDocument::setFermentable(EntityId recipe, EntityId item, const FermentableInput& in) {
    guarded([&] {
        auto existing = itemIn(&require(recipe).fermentables, item, "fermentable");
        execute(model::setFermentable(recipe, fermentableFrom(std::move(existing), in)));
    });
}

void RecipeDocument::removeFermentable(EntityId recipe, EntityId item) {
    execute(model::removeFermentable(recipe, item));
}

void RecipeDocument::addHop(EntityId recipe, const HopInput& in) {
    guarded([&] { execute(model::addHop(recipe, hopFrom({}, in))); });
}

void RecipeDocument::setHop(EntityId recipe, EntityId item, const HopInput& in) {
    guarded([&] {
        auto existing = itemIn(listIn(require(recipe).hops), item, "hop");
        execute(model::setHop(recipe, hopFrom(std::move(existing), in)));
    });
}

void RecipeDocument::removeHop(EntityId recipe, EntityId item) {
    execute(model::removeHop(recipe, item));
}

void RecipeDocument::addCulture(EntityId recipe, const CultureInput& in) {
    guarded([&] { execute(model::addCulture(recipe, cultureFrom({}, in))); });
}

void RecipeDocument::setCulture(EntityId recipe, EntityId item, const CultureInput& in) {
    guarded([&] {
        auto existing = itemIn(listIn(require(recipe).cultures), item, "culture");
        execute(model::setCulture(recipe, cultureFrom(std::move(existing), in)));
    });
}

void RecipeDocument::removeCulture(EntityId recipe, EntityId item) {
    execute(model::removeCulture(recipe, item));
}

void RecipeDocument::addWater(EntityId recipe, const WaterInput& in) {
    guarded([&] { execute(model::addWater(recipe, waterFrom({}, in))); });
}

void RecipeDocument::setWater(EntityId recipe, EntityId item, const WaterInput& in) {
    guarded([&] {
        auto existing = itemIn(listIn(require(recipe).waters), item, "water");
        execute(model::setWater(recipe, waterFrom(std::move(existing), in)));
    });
}

void RecipeDocument::removeWater(EntityId recipe, EntityId item) {
    execute(model::removeWater(recipe, item));
}

void RecipeDocument::addMiscellaneous(EntityId recipe, const MiscellaneousInput& in) {
    guarded([&] { execute(model::addMiscellaneous(recipe, miscellaneousFrom({}, in))); });
}

void RecipeDocument::setMiscellaneous(EntityId recipe, EntityId item, const MiscellaneousInput& in) {
    guarded([&] {
        auto existing = itemIn(listIn(require(recipe).miscellaneous), item, "miscellaneous ingredient");
        execute(model::setMiscellaneous(recipe, miscellaneousFrom(std::move(existing), in)));
    });
}

void RecipeDocument::removeMiscellaneous(EntityId recipe, EntityId item) {
    execute(model::removeMiscellaneous(recipe, item));
}

void RecipeDocument::setPrice(EntityId recipe, IngredientKind kind, EntityId item,
                              const std::optional<std::string>& amount, const std::optional<std::string>& per) {
    guarded([&] {
        std::optional<model::Price> price;
        if (amount) {
            if (!per) {
                throw std::invalid_argument("a price needs a unit");
            }
            const auto unit = unitFromBeerJson(*per);
            if (!unit) {
                throw std::invalid_argument("unknown unit \"" + *per + "\"");
            }
            price = model::Price{Rational::fromDecimal(*amount), *unit};
        }
        execute(model::setPrice(recipe, pricedKind(kind), item, std::move(price)));
    });
}

void RecipeDocument::setRecipeCurrency(EntityId recipe, const std::string& currency) {
    execute(model::setCurrency(recipe, currency));
}

void RecipeDocument::setPlanning(EntityId recipe, const std::string& planning) {
    execute(model::setPlanning(recipe, planning));
}

void RecipeDocument::setStyle(EntityId recipe, const std::optional<std::string>& style) {
    if (style && !calc::findStyle(document_, *style)) {
        throw EngineError("no style with id \"" + *style + "\"");
    }
    execute(model::setBjcpStyle(recipe, style));
}

// MARK: User styles

std::vector<StyleChoice> RecipeDocument::styleChoices() const {
    std::vector<StyleChoice> out;
    for (const calc::BjcpStyle* style : bjcpStyles()) {
        out.push_back(StyleChoice{std::string(style->id), styleTitle(*style), false});
    }
    for (const styles::UserStyle& style : document_.userStyles) {
        out.push_back(StyleChoice{style.id, style.name, true});
    }
    return out;
}

std::vector<styles::UserStyle> RecipeDocument::userStyles() const {
    return document_.userStyles;
}

std::optional<styles::UserStyle> RecipeDocument::userStyle(const std::string& id) const {
    const styles::UserStyle* found = document_.findUserStyle(id);
    return found != nullptr ? std::optional<styles::UserStyle>(*found) : std::nullopt;
}

std::string RecipeDocument::addUserStyle(const styles::UserStyle& style) {
    execute(model::addUserStyle(style));
    return document_.userStyles.back().id;
}

void RecipeDocument::updateUserStyle(const styles::UserStyle& style) {
    execute(model::setUserStyle(style));
}

void RecipeDocument::removeUserStyle(const std::string& id) {
    execute(model::removeUserStyle(id));
}

// MARK: Folders

EntityId RecipeDocument::addFolder(const std::string& name) {
    model::Folder folder;
    folder.name = name;
    execute(model::addFolder(std::move(folder)));
    return document_.folders.back().id;
}

void RecipeDocument::renameFolder(EntityId folder, const std::string& name) {
    execute(model::renameFolder(folder, name));
}

void RecipeDocument::removeFolder(EntityId folder) {
    execute(model::removeFolder(folder));
}

void RecipeDocument::moveRecipe(EntityId recipe, std::optional<EntityId> folder) {
    execute(model::moveRecipeToFolder(recipe, folder));
}

// MARK: Calculations

std::vector<calc::ProcedureStep> RecipeDocument::procedure(EntityId recipe, int boilMinutes,
                                                           const std::string& system) const {
    return guarded([&] {
        return calc::brewDayProcedure(require(recipe), boilMinutes, calc::mashSystemFromName(system));
    });
}

calc::BrewPlan RecipeDocument::brewingPlan(EntityId recipe, const calc::BrewPlanInputs& inputs) const {
    return guarded([&] { return calc::planBrewing(require(recipe), inputs); });
}

calc::JournalResults RecipeDocument::journalPlan(EntityId recipe, const calc::JournalInputs& measured) const {
    return guarded([&] { return calc::planJournal(require(recipe), measured); });
}

calc::StarterPlan RecipeDocument::starterPlan(EntityId recipe, const StarterSettings& settings) const {
    return guarded([&] {
        const Recipe& found = require(recipe);
        const calc::Results& r = results(recipe);
        if (!r.originalGravity) {
            throw std::invalid_argument("the recipe has no original gravity yet, so its yeast cannot be planned");
        }
        calc::StarterInputs inputs;
        inputs.lager = settings.lager;
        inputs.packCellsBillion = typedNumber(settings.packCells, "the pack size");
        inputs.growthBillionPerLiterPlato = typedNumber(settings.growth, "the growth");
        inputs.starterLiters = typedNumber(settings.starterLiters, "the starter volume");
        inputs.starterGravity = typedNumber(settings.starterGravity, "the starter gravity");
        inputs.twoStage = settings.twoStage;
        inputs.firstStageLiters = typedNumber(settings.firstStageLiters, "the first stage");
        inputs.addToBottling = settings.addToBottling;
        return calc::planStarter(found.batchSize.valueIn(Unit::Liter).toDouble(), *r.originalGravity, inputs);
    });
}

std::optional<StyleComparison> RecipeDocument::styleComparison(EntityId recipe) const {
    return guarded([&]() -> std::optional<StyleComparison> {
        const Recipe& found = require(recipe);
        if (!found.bjcpStyle) {
            return std::nullopt;
        }
        const std::optional<calc::StyleRef> style = calc::findStyle(document_, *found.bjcpStyle);
        if (!style) {
            throw std::invalid_argument("no style with id \"" + *found.bjcpStyle + "\"");
        }
        return StyleComparison{style->id, style->name, calc::compareToStyle(results(recipe), style->ranges)};
    });
}

std::string RecipeDocument::exportRecipe(EntityId recipe, const std::string& format) const {
    return guarded([&] {
        const Recipe& found = require(recipe);
        if (format == "beerxml") {
            return io::exportBeerXml(found, results(recipe));
        }
        if (format == "text") {
            return io::exportText(found, results(recipe));
        }
        if (format == "beerjson") {
            return io::exportBeerJson(found);
        }
        throw std::invalid_argument("unknown export format \"" + format + "\"");
    });
}

std::string RecipeDocument::exportRecipePdf(EntityId recipe) const {
    return guarded([&] { return io::recipePdf(document_, require(recipe), results(recipe)); });
}

// MARK: Styles and tools

const std::vector<const calc::BjcpStyle*>& bjcpStyles() {
    static const std::vector<const calc::BjcpStyle*> styles = [] {
        std::vector<const calc::BjcpStyle*> out;
        for (const calc::BjcpStyle& style : calc::bjcpStyles()) {
            out.push_back(&style);
        }
        return out;
    }();
    return styles;
}

std::string styleTitle(const calc::BjcpStyle& style) {
    return std::string(style.id) + " " + std::string(style.name);
}

calc::tools::Output runTool(const std::string& tool, const std::map<std::string, std::string>& inputs) {
    return guarded([&] { return calc::tools::run(tool, inputs); });
}

}  // namespace kettlelogic::app
