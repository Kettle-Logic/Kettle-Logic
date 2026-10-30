#include "pch.h"

#include "MainWindow.h"
#include "Views.h"

#include "kettlelogic/app/text.hpp"
#include "kettlelogic/app/water_profiles.hpp"

namespace kl {

using namespace kettlelogic::app;
using platform::U;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

ToolView::ToolView(ToolKind kind, std::map<std::string, std::string> prefill, WaterProfileLibrary& profiles, bool)
    : kind_(kind), spec_(toolSpec(kind)), profiles_(profiles), values_(std::move(prefill)) {
    root_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    root_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
    Rebuild();
}

xaml::UIElement ToolView::Root() {
    return root_;
}

void ToolView::Refresh() {
    Rebuild();
}

// A tool or converter form. The form runs the engine on every change, so the results follow the inputs without a
// save.
void ToolView::Rebuild() {
    focus_.Capture(root_.XamlRoot(), root_.Content().try_as<xaml::UIElement>());

    ui::Form form;
    for (const ToolSection& section : spec_.sections) {
        form.Section(section.title, section.footer.value_or(""));

        for (const ToolField& field : section.fields) {
            if (!toolShows(spec_, values_, field.enabledBy)) continue;
            const std::string key = field.key;
            const std::string current = toolValue(spec_, values_, key);
            switch (field.kind) {
                case ToolField::Kind::number:
                    form.Labeled(field.label, ui::Field(current, [this, key](std::string text) {
                                     values_[key] = text;
                                     Rebuild();
                                 }, 120));
                    break;
                case ToolField::Kind::toggle:
                    form.Labeled(field.label, ui::Switch(current == "1", [this, key](bool on) {
                                     values_[key] = on ? "1" : "0";
                                     Rebuild();
                                 }));
                    break;
                case ToolField::Kind::choice: {
                    std::vector<ui::Choice> choices;
                    for (const ToolField::Choice& choice : field.choices) choices.push_back({choice.value, choice.label});
                    form.Labeled(field.label, ui::Picker(choices, current, [this, key](std::string value) {
                                     values_[key] = value;
                                     Rebuild();
                                 }, 200));
                    break;
                }
            }
        }

        // A section with no readouts only supplies inputs to the results of its tool.
        if (section.readouts.empty()) continue;
        try {
            const auto output = runTool(section.tool, toolInputs(spec_, section, values_));
            for (const ToolReadout& readout : section.readouts) {
                if (!toolShows(spec_, values_, readout.enabledBy)) continue;
                const auto found = output.results.find(readout.key);
                std::string shown = found == output.results.end() ? "\xE2\x80\x94" : significant(found->second);
                if (!readout.unit.empty()) shown += " " + readout.unit;
                form.Labeled(readout.label, ui::Text(shown));
            }
            for (const std::string& note : output.notes) form.Note(note, ui::Kind::secondary);
        } catch (const std::exception& e) {
            form.Note(e.what(), ui::Kind::secondary);
        }
    }

    if (kind_ == ToolKind::waterProfile) {
        form.Section("Save This Profile",
                     "Saves the start water, the reverse osmosis share, the salts, and the target when it is on. The salts are "
                     "kept as grams per liter, so a recipe scales them to its own mash water. Load a saved profile in the Water "
                     "tab of any recipe.");
        form.Labeled("Name", ui::Field(saveName_, [this](std::string text) { saveName_ = text; Rebuild(); }, 240, false));
        ctl::Button save = ui::Button("Save Profile", [this] {
            // The inputs of the Water Profile tool, as they were typed.
            std::map<std::string, std::string> inputs;
            for (const ToolSection& section : spec_.sections) {
                if (section.tool != "water.profile" || !section.readouts.empty()) continue;
                for (const ToolField& field : section.fields) {
                    if (toolShows(spec_, values_, field.enabledBy)) inputs[field.key] = toolValue(spec_, values_, field.key);
                }
            }
            const std::string name = trimmed(saveName_);
            const auto profile = SavedWaterProfile::fromTool(name, inputs);
            if (!profile) {
                saveMessage_ = "Enter a water volume above zero before saving.";
            } else {
                try {
                    profiles_.save(*profile);
                    saveMessage_ = "Saved \xE2\x80\x9C" + profile->name + "\xE2\x80\x9D. Load it in the Water tab of a recipe.";
                } catch (const std::exception& e) {
                    saveMessage_ = e.what();
                }
            }
            Rebuild();
        });
        save.IsEnabled(!trimmed(saveName_).empty());
        form.Wide(save);
        if (!saveMessage_.empty()) form.Note(saveMessage_);
    }

    const auto screen = form.Build();
    root_.Content(screen);
    screen.UpdateLayout();
    focus_.Restore(screen);
}

// A window of its own for a calculator or a converter.
ToolWindow::ToolWindow(Host& host, ToolKind kind, std::map<std::string, std::string> prefill,
                       std::function<void(ToolWindow*)> closed)
    : view_(kind, std::move(prefill), host.store.waterProfiles) {
    window_.Title(platform::H(toolSpec(kind).title));
    window_.Content(view_.Root());
    try {
        window_.SystemBackdrop(xaml::Media::MicaBackdrop());
        const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(platform::WindowHandle(window_));
        winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id).Resize(winrt::Windows::Graphics::SizeInt32{620, 760});
    } catch (winrt::hresult_error const&) {
    }
    window_.Closed([this, closed = std::move(closed)](auto const&, auto const&) {
        // The window is released after its Closed event has finished.
        window_.DispatcherQueue().TryEnqueue([this, closed] { closed(this); });
    });
}

}  // namespace kl
