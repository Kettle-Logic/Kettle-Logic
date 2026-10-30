#pragma once

#include "pch.h"

#include "Platform.h"
#include "Ui.h"

#include "kettlelogic/app/brew_day.hpp"
#include "kettlelogic/app/document_store.hpp"
#include "kettlelogic/app/tool_specs.hpp"

#include <map>

namespace kl {

// The engine's namespaces, as the screens name them.
namespace calc = kettlelogic::calc;
namespace model = kettlelogic::model;

// What a screen needs from the window it is in: the app's state, the window to put dialogs over, and the ways to
// open the other windows. Screens never reach for the window directly.
struct Host {
    kettlelogic::app::DocumentStore& store;
    winrt::Microsoft::UI::Xaml::Window window;

    // Draws the screens again after a change. Changes coalesce, so asking often is cheap.
    std::function<void()> refresh;
    // Opens a calculator or converter in its own window.
    std::function<void(kettlelogic::app::ToolKind, std::map<std::string, std::string>)> openTool;
    std::function<void(const kettlelogic::app::BrewDayPlan&)> openBrewDay;
    std::function<void()> openHelp;
    // Asks for a recipe file to open.
    std::function<void()> openFile;

    winrt::Microsoft::UI::Xaml::XamlRoot xamlRoot() const { return window.Content().XamlRoot(); }
    HWND hwnd() const { return platform::WindowHandle(window); }
    kettlelogic::app::Units units() const { return kettlelogic::app::Units{store.preferences.units()}; }
    bool simpleMode() const { return store.preferences.boolean(kettlelogic::app::PrefKey::simpleMode); }
};

// A screen in the detail area of the window.
class View {
public:
    virtual ~View() = default;
    virtual winrt::Microsoft::UI::Xaml::UIElement Root() = 0;
    // Called after the app's state changed, so the screen shows it.
    virtual void Refresh() {}
};

}  // namespace kl
