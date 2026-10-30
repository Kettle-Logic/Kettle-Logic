#include "pch.h"

#include "RecipeContext.h"
#include "Views.h"

#include "kettlelogic/app/brew_sheet.hpp"
#include "kettlelogic/app/readings_csv.hpp"
#include "kettlelogic/app/text.hpp"
#include "kettlelogic/io/brew_sheet_pdf.hpp"

#include <map>
#include <memory>

namespace kl {

using namespace kettlelogic::app;

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

const char* const kDash = "\xE2\x80\x94";

std::string Shown(std::optional<double> value, int places) {
    return value ? number(*value, places, places) : kDash;
}

template <typename T>
T* FindById(std::vector<T>& list, const std::string& id) {
    for (T& item : list) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

BrewSession* SessionById(RecipePlanning& planning, const std::string& id) {
    return FindById(planning.sessions, id);
}

using SessionNumber = std::optional<double> BrewSession::*;

// A measured value, which may be empty. `factor` converts the value to the unit the field shows, and the typed number
// is converted back. Text that is not a number leaves the value as it was.
ctl::TextBox OptionalField(RecipeContext& c, const std::string& sessionId, std::optional<double> value, SessionNumber member,
                           int places, double factor = 1, double width = 100) {
    const std::string text = value ? number(*value * factor, 0, places) : "";
    return ui::Field(text, [&c, sessionId, member, factor](std::string typed) {
        const std::string trimmedText = trimmed(typed);
        std::optional<double> parsed;
        if (!trimmedText.empty()) {
            parsed = parseNumber(trimmedText);
            if (!parsed) return;
            parsed = *parsed / factor;
        }
        c.updatePlanning([&](RecipePlanning& p) {
            if (BrewSession* session = SessionById(p, sessionId)) session->*member = parsed;
        });
    }, width);
}

// The brew sheet for the recipe, in the units the app shows.
calc::BrewSheet CurrentSheet(RecipeContext& c) {
    return brewSheetFor(c.recipe(), c.results(), c.units());
}

// Saves the recipe's brew sheet as a PDF the brewer can print.
void PrintBrewSheet(RecipeContext& c) {
    try {
        const std::string pdf = kettlelogic::io::brewSheetPdf(CurrentSheet(c));
        std::string name = c.recipe().name;
        for (char& ch : name) {
            if (ch == '/' || ch == ':') ch = '-';
        }
        std::filesystem::path start;
        try {
            start = c.store().recipeLibrary.ensureFolder();
        } catch (const std::exception&) {
            start = c.store().recipeLibrary.folder();
        }
        const auto file = platform::SaveFile(c.host.hwnd(), start, platform::H(name + " brew sheet.pdf").c_str(),
                                             {{L"PDF documents", L"*.pdf"}}, L"pdf");
        if (file) platform::WriteBinaryFile(*file, pdf);
    } catch (const std::exception& e) {
        c.store().errorMessage = e.what();
        c.redraw();
    }
}

// A form with a box for every code on the sheet. Nothing is saved until the sheet's values read back cleanly, so one
// unreadable box keeps the whole sheet from going in half-way.
void ShowBrewSheetEntry(RecipeContext& c, const std::string& sessionId) {
    const calc::BrewSheet sheet = CurrentSheet(c);
    // The boxes are read when Enter is pressed, so every value typed counts, not only the ones already committed.
    auto boxes = std::make_shared<std::vector<std::pair<std::string, ctl::TextBox>>>();

    ui::Form form;
    for (const calc::BrewSheetSection& section : sheet.sections) {
        form.Section(section.code + "   " + section.title);
        for (const calc::BrewSheetBox& box : section.boxes) {
            ctl::TextBox entry = ui::Field("", [](std::string) {}, 150);
            boxes->push_back({box.code, entry});
            form.Labeled(box.code + "   " + box.label + " (" + box.unit + ")", entry);
        }
    }

    ui::DialogOptions options;
    options.title = "Enter Brew Sheet";
    options.content = form.BuildPanel();
    options.primaryText = "Enter";
    options.width = 480;
    options.primary = [&c, sessionId, sheet, boxes] {
        std::map<std::string, std::string> typed;
        for (const auto& [code, entry] : *boxes) typed[code] = platform::U(entry.Text());
        const calc::BrewSheetValues values = calc::readBrewSheet(sheet, typed);
        if (!values.issues.empty()) {
            std::string message;
            for (const calc::BrewSheetIssue& issue : values.issues) {
                message += issue.code + ": " + issue.message + "\n";
            }
            ui::ShowMessage(c.host.xamlRoot(), "Some boxes could not be read",
                            message + "Nothing was entered. Fix the boxes and try again.");
            return false;
        }
        c.updatePlanning([&](RecipePlanning& p) {
            if (BrewSession* session = SessionById(p, sessionId)) applyBrewSheet(*session, values);
        });
        return true;
    };
    ui::ShowDialog(c.host.xamlRoot(), options);
}

ctl::StackPanel Row(std::initializer_list<xaml::UIElement> items) {
    return ui::HStack(items, 8);
}

}  // namespace

void BuildJournalTab(RecipeContext& c, ui::Form& form) {
    const Units units = c.units();
    const RecipePlanning planning = c.planning();

    // MARK: Sessions

    const BrewSession* selected = nullptr;
    for (const BrewSession& session : planning.sessions) {
        if (session.id == c.selectedSession) selected = &session;
    }
    if (selected == nullptr && !planning.sessions.empty()) {
        selected = &planning.sessions.front();
    }

    form.Section("Brew Session");
    if (!planning.sessions.empty()) {
        std::vector<ui::Choice> choices;
        for (const BrewSession& session : planning.sessions) choices.push_back({session.id, session.name});
        form.Labeled("Session", ui::Picker(choices, selected->id, [&c](std::string id) {
                         c.selectedSession = id;
                         c.redraw();
                     }, 220));
    }
    ctl::Button remove = ui::Button("Delete Session", [&c, id = selected ? selected->id : std::string()] {
        c.updatePlanning([&](RecipePlanning& p) {
            p.sessions.erase(std::remove_if(p.sessions.begin(), p.sessions.end(), [&](const BrewSession& s) { return s.id == id; }),
                             p.sessions.end());
        });
        c.selectedSession.clear();
    });
    remove.IsEnabled(selected != nullptr);
    form.Wide(ui::HStack({ui::Button("New Session", [&c] {
        BrewSession session;
        const RecipePlanning current = c.planning();
        session.name = "Session " + std::to_string(current.sessions.size() + 1);
        c.selectedSession = session.id;
        c.updatePlanning([&](RecipePlanning& p) { p.sessions.push_back(session); });
    }), remove}, 8));

    if (selected == nullptr) {
        form.Note("No brew session yet. A session records one brew day, and a recipe can have several.");
        return;
    }

    const BrewSession session = *selected;
    const std::string sid = session.id;
    form.Labeled("Session name", ui::Field(session.name, [&c, sid](std::string text) {
                     c.updatePlanning([&](RecipePlanning& p) { if (auto* s = SessionById(p, sid)) s->name = text; });
                 }, 220, false));
    form.Labeled("Brew day", ui::DateField(session.brewDate, [&c, sid](double date) {
                     c.updatePlanning([&](RecipePlanning& p) { if (auto* s = SessionById(p, sid)) s->brewDate = date; });
                 }));

    // MARK: Brew sheet

    form.Section("Brew Sheet",
                 "Print a sheet to fill in with a pencil on brew day. Each box has a code, and the entry screen has the same "
                 "codes, so enter the sheet's values here when the brew is done.");
    form.Wide(ui::HStack({
        ui::Button("Print Brew Sheet…", [&c] { PrintBrewSheet(c); }),
        ui::Button("Enter Brew Sheet…", [&c, sid] { ShowBrewSheetEntry(c, sid); }),
    }, 8));

    // MARK: Figures

    std::optional<calc::JournalResults> plan;
    try {
        plan = c.document->journalPlan(c.id(), journalInputs(session));
    } catch (const std::exception&) {
    }
    std::optional<double> adjustedPh;
    try {
        adjustedPh = c.document->brewingPlan(c.id(), brewingInputs(planning.water, planning.mash)).estimatedMashPh;
    } catch (const std::exception&) {
    }
    const calc::Results& results = c.results();
    const double batchLitersValue = batchLiters(c.recipe());
    const double volumeFactor = units.displayVolume(1);
    const std::string volumeUnit = units.volumeUnit();

    form.Section("Mash pH and Runnings",
                 "The adjusted mash pH is the Mash tab's estimate, from the residual alkalinity after the acids. Measure the pH to check it.");
    form.Labeled("Measured mash pH", OptionalField(c, sid, session.mashPh, &BrewSession::mashPh, 2));
    form.Labeled("Adjusted mash pH (estimated)", ui::Text(Shown(adjustedPh, 2)));
    form.Labeled("Sparge runoff pH", OptionalField(c, sid, session.spargeRunoffPh, &BrewSession::spargeRunoffPh, 2));
    form.Labeled("End of runnings gravity (SG)", OptionalField(c, sid, session.endOfRunningsGravity, &BrewSession::endOfRunningsGravity, 3));

    form.Section("Volume and Gravity in the Boiler");
    form.Labeled("Estimated pre-boil volume (" + volumeUnit + ")",
                 ui::Text(Shown(results.preBoilLiters ? std::optional<double>(units.displayVolume(*results.preBoilLiters)) : std::nullopt, 2)));
    form.Labeled("Measured pre-boil volume", OptionalField(c, sid, session.preBoilLiters, &BrewSession::preBoilLiters, 2, volumeFactor));
    form.Labeled("Estimated pre-boil gravity (SG)", ui::Text(Shown(plan ? plan->expectedPreBoilGravity : std::nullopt, 3)));
    form.Labeled("Measured pre-boil gravity (SG)", OptionalField(c, sid, session.preBoilGravity, &BrewSession::preBoilGravity, 3));

    form.Section("Into the Fermenter");
    form.Labeled("Calculated batch size (" + volumeUnit + ")", ui::Text(Shown(units.displayVolume(batchLitersValue), 1)));
    form.Labeled("Measured batch size", OptionalField(c, sid, session.batchLiters, &BrewSession::batchLiters, 2, volumeFactor));
    form.Labeled("Estimated original gravity (SG)", ui::Text(Shown(results.originalGravity, 3)));
    form.Labeled("Measured original gravity (SG)", OptionalField(c, sid, session.originalGravity, &BrewSession::originalGravity, 3));

    form.Section("Brewhouse Efficiency",
                 "The expected brewhouse efficiency is the recipe's, shown on the Equipment section too. The measured "
                 "efficiencies are the grain's potential extract, scaled by the gravity and volume measured. Calories are an "
                 "estimate from the gravities and alcohol.");
    form.Labeled("Expected brewhouse efficiency (%)", ui::Text(Shown(plan ? plan->expectedBrewhouseEfficiency : std::nullopt, 1)));
    form.Labeled("Measured mash efficiency (%)", ui::Text(Shown(plan ? plan->measuredMashEfficiency : std::nullopt, 1)));
    form.Labeled("Measured brewhouse efficiency (%)", ui::Text(Shown(plan ? plan->measuredBrewhouseEfficiency : std::nullopt, 1)));
    form.Labeled("Estimated calories (per 12 fl oz)", ui::Text(Shown(plan ? plan->expectedCaloriesPer12Oz : std::nullopt, 0)));
    form.Labeled("Measured calories (per 12 fl oz)", ui::Text(Shown(plan ? plan->measuredCaloriesPer12Oz : std::nullopt, 0)));

    form.Section("At Packaging");
    form.Labeled("Estimated final gravity (SG)", ui::Text(Shown(results.finalGravity, 3)));
    form.Labeled("Measured final gravity (SG)", OptionalField(c, sid, session.finalGravity, &BrewSession::finalGravity, 3));
    form.Labeled("Estimated bottling volume (" + volumeUnit + ")", ui::Text(Shown(units.displayVolume(batchLitersValue), 1)));
    form.Labeled("Measured bottling volume", OptionalField(c, sid, session.bottlingLiters, &BrewSession::bottlingLiters, 2, volumeFactor));

    form.Section("Brew Statistics", "ABV is the gravity drop times 131.25, and attenuation is the share of the original gravity that fermented.");
    form.Labeled("Estimated ABV (%)", ui::Text(Shown(results.abv, 2)));
    form.Labeled("Measured ABV (%)", ui::Text(Shown(plan ? plan->measuredAbv : std::nullopt, 2)));
    form.Labeled("Estimated attenuation (%)", ui::Text(Shown(plan ? plan->expectedAttenuation : std::nullopt, 1)));
    form.Labeled("Measured attenuation (%)", ui::Text(Shown(plan ? plan->measuredAttenuation : std::nullopt, 1)));

    // MARK: Carbonation

    form.Section("Carbonation");
    form.Labeled("Method", ui::Picker(ui::Choices(options(BrewSession::carbonationMethods(), session.carbonationMethod)),
                                      session.carbonationMethod, [&c, sid](std::string method) {
                                          c.updatePlanning([&](RecipePlanning& p) { if (auto* s = SessionById(p, sid)) s->carbonationMethod = method; });
                                      }, 260));
    form.Labeled("Target carbonation (volumes of CO\xE2\x82\x82)",
                 OptionalField(c, sid, session.carbonationVolumes, &BrewSession::carbonationVolumes, 2));
    if (session.carbonationMethod.rfind("Bottle", 0) == 0) {
        form.Labeled("Corn sugar (g per L)", ui::Text(Shown(plan ? plan->primingSugarGramsPerLiter : std::nullopt, 1)));
        form.Labeled("Corn sugar for the bottling volume (g)", ui::Text(Shown(plan ? plan->primingSugarGrams : std::nullopt, 0)));
    } else if (session.carbonationMethod == "Force carbonation") {
        const std::string tempText = session.servingTempC ? number(units.displayTemperature(*session.servingTempC), 0, 1) : "";
        form.Labeled("Serving temperature (" + units.temperatureSymbol() + ")",
                     ui::Field(tempText, [&c, sid, units](std::string typed) {
                         const std::string t = trimmed(typed);
                         std::optional<double> celsius;
                         if (!t.empty()) {
                             const auto parsed = parseNumber(t);
                             if (!parsed) return;
                             celsius = units.celsius(*parsed);
                         }
                         c.updatePlanning([&](RecipePlanning& p) { if (auto* s = SessionById(p, sid)) s->servingTempC = celsius; });
                     }));
        form.Labeled("Regulator pressure (psi)", ui::Text(Shown(plan ? plan->forceCarbonationPsi : std::nullopt, 1)));
    }

    // MARK: Fermentation temperature profile

    form.Section("Fermentation Temperature Profile", "Pick a profile to fill in its steps, then change them to suit the yeast.");
    std::vector<ui::Choice> profiles = {{"None", "None"}};
    for (const FermentationPreset& preset : FermentationPreset::all()) profiles.push_back({preset.name, preset.name});
    form.Labeled("Profile", ui::Picker(profiles, session.profileName.value_or("None"), [&c, sid](std::string name) {
                     c.updatePlanning([&](RecipePlanning& p) {
                         BrewSession* s = SessionById(p, sid);
                         if (s == nullptr) return;
                         s->profileName = name == "None" ? std::nullopt : std::optional<std::string>(name);
                         s->profileSteps.clear();
                         for (const FermentationPreset& preset : FermentationPreset::all()) {
                             if (preset.name == name) s->profileSteps = preset.steps;
                         }
                     });
                 }, 240));
    double totalDays = 0;
    for (const ProfileStep& step : session.profileSteps) {
        totalDays += step.days;
        const std::string stepId = step.id;
        form.Wide(Row({
            ui::Text("Days"),
            ui::Field(number(step.days, 0, 0), [&c, sid, stepId](std::string text) {
                if (const auto days = parseNumber(text)) {
                    c.updatePlanning([&](RecipePlanning& p) {
                        if (auto* s = SessionById(p, sid)) if (auto* step = FindById(s->profileSteps, stepId)) step->days = *days;
                    });
                }
            }, 60),
            ui::Text("Temperature (" + units.temperatureSymbol() + ")"),
            ui::Field(number(units.displayTemperature(step.tempC), 0, 1), [&c, sid, stepId, units](std::string text) {
                if (const auto typed = parseNumber(text)) {
                    const double celsius = units.celsius(*typed);
                    c.updatePlanning([&](RecipePlanning& p) {
                        if (auto* s = SessionById(p, sid)) if (auto* step = FindById(s->profileSteps, stepId)) step->tempC = celsius;
                    });
                }
            }, 80)}));
    }
    form.Labeled("Total days", ui::Text(std::to_string(static_cast<int>(totalDays))));

    // MARK: Readings

    form.Section("Fermentation Readings",
                 "Each reading has a date, a temperature, and a gravity. The day counts from brew day. The CSV file has the "
                 "columns date, day, temperature_c, and gravity_sg.");
    for (const FermentationReading& reading : session.readings) {
        const std::string readingId = reading.id;
        form.Wide(Row({
            ui::DateField(reading.date, [&c, sid, readingId](double date) {
                c.updatePlanning([&](RecipePlanning& p) {
                    if (auto* s = SessionById(p, sid)) if (auto* r = FindById(s->readings, readingId)) r->date = date;
                });
            }),
            ui::Text("Day " + std::to_string(daysBetween(session.brewDate, reading.date))),
            ui::Field(reading.temperatureC ? number(units.displayTemperature(*reading.temperatureC), 0, 1) : "",
                      [&c, sid, readingId, units](std::string text) {
                          const std::string t = trimmed(text);
                          std::optional<double> celsius;
                          if (!t.empty()) {
                              const auto parsed = parseNumber(t);
                              if (!parsed) return;
                              celsius = units.celsius(*parsed);
                          }
                          c.updatePlanning([&](RecipePlanning& p) {
                              if (auto* s = SessionById(p, sid)) if (auto* r = FindById(s->readings, readingId)) r->temperatureC = celsius;
                          });
                      }, 70),
            ui::Field(reading.gravity ? number(*reading.gravity, 0, 3) : "", [&c, sid, readingId](std::string text) {
                const std::string t = trimmed(text);
                std::optional<double> gravity;
                if (!t.empty()) {
                    gravity = parseNumber(t);
                    if (!gravity) return;
                }
                c.updatePlanning([&](RecipePlanning& p) {
                    if (auto* s = SessionById(p, sid)) if (auto* r = FindById(s->readings, readingId)) r->gravity = gravity;
                });
            }, 90),
            ui::IconButton(ctl::Symbol::Remove, "Remove this reading", [&c, sid, readingId] {
                c.updatePlanning([&](RecipePlanning& p) {
                    if (auto* s = SessionById(p, sid)) {
                        auto& list = s->readings;
                        list.erase(std::remove_if(list.begin(), list.end(), [&](const FermentationReading& r) { return r.id == readingId; }),
                                   list.end());
                    }
                });
            })}));
    }

    ctl::Button exportButton = ui::Button("Export CSV…", [&c, session] {
        const std::string text = ReadingsCsv::exportReadings(session.readings, session.brewDate);
        try {
            std::filesystem::path start;
            try {
                start = c.store().recipeLibrary.ensureFolder();
            } catch (const std::exception&) {
                start = c.store().recipeLibrary.folder();
            }
            const auto file = platform::SaveFile(c.host.hwnd(), start, platform::H(session.name + " readings.csv").c_str(),
                                                 {{L"CSV files", L"*.csv"}}, L"csv");
            if (file) platform::WriteTextFile(*file, text);
        } catch (const std::exception& e) {
            c.store().errorMessage = e.what();
            c.redraw();
        }
    });
    exportButton.IsEnabled(!session.readings.empty());
    form.Wide(ui::HStack({
        ui::Button("Add Reading", [&c, sid] {
            c.updatePlanning([&](RecipePlanning& p) { if (auto* s = SessionById(p, sid)) s->readings.push_back(FermentationReading{}); });
        }),
        ui::Button("Import CSV…", [&c, sid] {
            try {
                const auto file = platform::OpenFile(c.host.hwnd(), c.store().recipeLibrary.folder(), {{L"CSV files", L"*.csv"}},
                                                     L"Import fermentation readings");
                if (!file) return;
                const ReadingsCsv::Parsed parsed = ReadingsCsv::parse(platform::ReadTextFile(*file));
                c.updatePlanning([&](RecipePlanning& p) {
                    if (auto* s = SessionById(p, sid)) {
                        s->readings.insert(s->readings.end(), parsed.readings.begin(), parsed.readings.end());
                    }
                });
                if (parsed.skipped > 0) {
                    c.store().errorMessage = "Imported " + std::to_string(parsed.readings.size()) + " readings. " +
                                             std::to_string(parsed.skipped) + " rows could not be read.";
                    c.redraw();
                }
            } catch (const std::exception& e) {
                c.store().errorMessage = e.what();
                c.redraw();
            }
        }),
        exportButton}, 8));
}

}  // namespace kl
