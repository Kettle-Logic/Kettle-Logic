#pragma once

#include "host.hpp"

#include "kettlelogic/app/brew_day.hpp"
#include "kettlelogic/app/equipment.hpp"
#include "kettlelogic/app/planning.hpp"
#include "kettlelogic/app/recipe_text.hpp"

namespace kl {

// What one open recipe's tabs share: the document, the recipe, and the settings that decide how it is shown.
struct RecipeContext {
    Host& host;
    kettlelogic::app::ItemKey key;
    std::shared_ptr<kettlelogic::app::RecipeDocument> document;

    kettlelogic::app::DocumentStore& store() { return host.store; }
    kettlelogic::app::EntityId id() const { return key.id; }
    const kettlelogic::model::Recipe& recipe() const { return *document->recipe(key.id); }
    const kettlelogic::calc::Results& results() const { return document->results(key.id); }
    kettlelogic::app::Units units() const { return host.units(); }
    bool simple() const { return host.simpleMode(); }

    // The brewing system chosen in the recipe's equipment section. It is kept while the recipe is open.
    std::string equipmentName;

    // What the tabs keep while the recipe is open: the saved water profile picked and the message about it, the
    // brew session shown in the journal, and the procedure last generated.
    std::string chosenProfile;
    std::string libraryMessage;
    std::string selectedSession;
    std::string procedureBoil;
    std::string procedureSystem;  // the brewing system the boil length was set from
    std::optional<std::vector<kettlelogic::calc::ProcedureStep>> procedureSteps;
    std::string procedureError;
    int procedureMinutes = 0;
    unsigned long long procedureRevision = 0;
    kettlelogic::app::EquipmentProfile equipment() const { return host.store.equipment.profile(equipmentName); }

    // The planning stored in the recipe, read as it is now.
    kettlelogic::app::RecipePlanning planning() const {
        return kettlelogic::app::RecipePlanning::read(recipe().planning);
    }
    // Saves a change to the planning of the recipe. The change is applied to the planning as it is stored now, so a
    // tab cannot overwrite what another tab saved.
    void updatePlanning(const std::function<void(kettlelogic::app::RecipePlanning&)>& change) {
        kettlelogic::app::RecipePlanning current = planning();
        change(current);
        const std::string text = current.text();
        store().attempt([&] { document->setPlanning(key.id, text); });
    }
    // Set when the screen itself changed what it shows, such as the tab or a pick that is not kept in the recipe, so the
    // next refresh draws it again even though the recipe did not change.
    bool dirty = true;
    void redraw() {
        dirty = true;
        host.refresh();
    }

    // Runs an edit and shows its error, if any.
    void attempt(const std::function<void()>& work) { store().attempt(work); }
};

// A number field bound to a value in the planning, shown with a fixed number of places. Text that is not a number
// leaves the value as it was. `factor` converts the value to the unit the field shows.
std::string NumberText(double value, int places = 2, double factor = 1);
// The value a typed number stands for, or nothing when the text is not a number.
std::optional<double> TypedNumber(const std::string& text, double factor = 1);

}  // namespace kl
