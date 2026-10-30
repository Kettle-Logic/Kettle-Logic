#include "recipe_context.hpp"
#include "views.hpp"

#include "kettlelogic/app/text.hpp"

namespace kl {

using namespace kettlelogic::app;

namespace {

// The step's phase, and for the boil its minute, then the text.
std::string StepHeading(const calc::ProcedureStep& step) {
    return step.minute ? step.phase + " \xC2\xB7 " + std::to_string(*step.minute) + " min" : step.phase;
}

}  // namespace

// Generates a brew-day procedure from the recipe, as sections of the recipe's form. The user gives the boil length,
// and the brewing system sets the mash steps. The additions are placed by the times on each ingredient, counted back
// from the end of the boil.
void BuildProcedureTab(RecipeContext& c, ui::Form& form) {
    const EquipmentProfile profile = c.equipment();

    // A new brewing system starts the boil length at its own, and a changed recipe makes the steps stale.
    if (c.procedureSystem != profile.name) {
        c.procedureSystem = profile.name;
        c.procedureBoil = std::to_string(profile.boilMinutes);
    }
    if (c.procedureSteps && c.procedureRevision != c.document->revision()) {
        c.procedureSteps.reset();
        c.procedureError.clear();
    }

    form.Section("Brew-Day Procedure",
                 "Each addition is placed by the time set on its ingredient, counted back from the end of this boil. An "
                 "addition timed longer than the boil is flagged in red.");
    form.Labeled("Brewing system", ui::Text(profile.name));
    form.Labeled("Boil length (minutes)", ui::Field(c.procedureBoil, [&c](std::string text) { c.procedureBoil = text; }));
    form.Wide(ui::Button("Generate Procedure", [&c] {
        const std::string text = trimmed(c.procedureBoil);
        const bool plain = !text.empty() && text.size() <= 4 && text.find_first_not_of("0123456789") == std::string::npos;
        const int minutes = plain ? std::stoi(text) : 0;
        if (!plain || minutes < 1 || minutes > 600) {
            c.procedureSteps.reset();
            c.procedureError = "Enter a boil length from 1 to 600 minutes.";
        } else {
            try {
                c.procedureSteps = c.document->procedure(c.id(), minutes, c.equipment().mashSystem);
                c.procedureMinutes = minutes;
                c.procedureRevision = c.document->revision();
                c.procedureError.clear();
            } catch (const std::exception& e) {
                c.procedureSteps.reset();
                c.procedureError = e.what();
            }
        }
        c.redraw();
    }));

    if (!c.procedureError.empty()) {
        form.Section();
        form.Note(c.procedureError, ui::Kind::error);
    }

    if (c.procedureSteps) {
        form.Section("Procedure");
        for (const calc::ProcedureStep& step : *c.procedureSteps) {
            GtkWidget* panel = ui::Box(GTK_ORIENTATION_VERTICAL, 2);
            ui::Append(panel, ui::Text(StepHeading(step), ui::Kind::caption));
            GtkWidget* text = ui::Text(step.text);
            if (step.warning) ui::Style(text, "error");
            ui::Append(panel, text);
            form.Wide(panel);
        }
        form.Wide(ui::HStack({
            ui::Button("Copy Procedure", [&c] {
                if (!c.procedureSteps) return;
                std::string text;
                for (const calc::ProcedureStep& step : *c.procedureSteps) {
                    const std::string when = step.minute ? " \xC2\xB7 " + std::to_string(*step.minute) + " min" : "";
                    text += (text.empty() ? "" : "\n") + step.phase + when + ": " + step.text;
                }
                platform::CopyText(text);
            }),
            ui::Button("Open Brew-Day View", [&c] {
                if (!c.procedureSteps) return;
                BrewDayPlan plan;
                plan.id = newUuidText();
                plan.recipeName = c.recipe().name;
                plan.boilMinutes = c.procedureMinutes;
                plan.steps = *c.procedureSteps;
                c.host.openBrewDay(plan);
            })}, 8));
    }
}

}  // namespace kl
