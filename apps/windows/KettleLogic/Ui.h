#pragma once

#include "pch.h"

#include "Platform.h"

// A small kit for building the app's screens from native WinUI controls in code. A screen is a Form: sections of
// rows, which is how the macOS app's grouped forms read, built from Border, Grid, TextBlock, TextBox, ComboBox,
// ToggleSwitch and the other standard controls, and so styled by the system theme.
namespace kl::ui {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

enum class Kind { body, strong, caption, secondary, title, subtitle, mono, error, success };

ctl::TextBlock Text(std::string_view text, Kind kind = Kind::body);

// Gives an element one of the KL* styles in App.xaml, such as "KLCardBorder". A color that depends on the theme comes
// from such a style, whose {ThemeResource} setters follow a theme chosen later. Do not fetch a brush and assign it.
void UseStyle(xaml::FrameworkElement const& element, wchar_t const* key);

// The color theme of every window and dialog: "system" follows Windows, "light" and "dark" override it.
void SetTheme(std::string_view theme);
std::string Theme();
// Gives a window's content or a dialog the chosen theme, and keeps it following later changes.
void FollowTheme(xaml::FrameworkElement const& root);
xaml::Media::SolidColorBrush Solid(int r, int g, int b);

struct Choice {
    std::string value;
    std::string label;
};
std::vector<Choice> Choices(const std::vector<std::string>& values);

ctl::Button Button(std::string_view label, std::function<void()> onClick, bool accent = false);
ctl::Button IconButton(ctl::Symbol symbol, std::string_view tooltip, std::function<void()> onClick);
ctl::HyperlinkButton LinkButton(std::string_view label, std::function<void()> onClick);

// A text field that commits when Enter is pressed or focus leaves it, and only when the text changed.
ctl::TextBox Field(std::string_view text, std::function<void(std::string)> commit, double width = 110,
                   bool rightAligned = true, bool commitOnEnter = true);
ctl::TextBox MultilineField(std::string_view text, std::function<void(std::string)> commit, double minHeight);
// A pick from a list. `onChange` is called with the value the user picked, not for a change made by the app.
ctl::ComboBox Picker(const std::vector<Choice>& choices, std::string_view selected,
                     std::function<void(std::string)> onChange, double width = 0);
ctl::ToggleSwitch Switch(bool on, std::function<void(bool)> onChange);
ctl::CheckBox Check(std::string_view label, bool on, std::function<void(bool)> onChange);
ctl::CalendarDatePicker DateField(double referenceSeconds, std::function<void(double)> onChange);

xaml::FrameworkElement Spacer(double height);
xaml::Media::SolidColorBrush ColorBrush(winrt::Windows::UI::Color color);

// A horizontal row of controls with space between them.
ctl::StackPanel HStack(std::initializer_list<xaml::UIElement> items, double spacing = 8);
ctl::StackPanel VStack(std::initializer_list<xaml::UIElement> items, double spacing = 8);

void SetTip(xaml::DependencyObject const& element, std::string_view tip);

// The labeled field a help term explains. In simple mode the explanation sits under the label. Otherwise an (i)
// button opens it in a flyout.
xaml::UIElement HelpLabel(std::string_view title, std::string_view explanation, bool simpleMode);

// A screen of sections of rows.
class Form {
public:
    Form();

    // Starts a section, with an optional heading above it and a note below it.
    void Section(std::string_view header = {}, std::string_view footer = {});
    // A section whose heading has a button at its right, as the ingredient lists have.
    void SectionWithAction(std::string_view header, std::string_view actionLabel, std::function<void()> action,
                           std::string_view footer = {});

    // A label on the left and a control on the right, with an optional explanation under the label.
    void Labeled(std::string_view label, xaml::UIElement const& control, std::string_view help = {});
    void Labeled(xaml::UIElement const& label, xaml::UIElement const& control);
    // A control that takes the whole row.
    void Wide(xaml::UIElement const& element);
    // A line of text in the section: secondary by default, or an error.
    void Note(std::string_view text, Kind kind = Kind::secondary);

    // The finished form, in a scroll viewer.
    xaml::UIElement Build();
    // The finished form without a scroll viewer, to sit inside another screen or a dialog.
    xaml::UIElement BuildPanel();

private:
    void Flush();
    void AddRow(xaml::UIElement const& row);

    ctl::StackPanel page_;
    ctl::StackPanel rows_{nullptr};
    std::string footer_;
};

// Keeps the keyboard focus across a rebuild of a screen. Capture before the old controls go, and restore on the
// new ones. The control is found by its place among the controls that take focus.
class FocusKeeper {
public:
    void Capture(xaml::XamlRoot const& root, xaml::UIElement const& screen);
    void Restore(xaml::UIElement const& screen);

private:
    int index_ = -1;
};

// A dialog over the window, with a title, content, and buttons. `primary` is called when the primary button is
// pressed, and returns false to keep the dialog open, such as when a value was refused.
struct DialogOptions {
    std::string title;
    xaml::UIElement content{nullptr};
    std::string primaryText;      // empty for no primary button
    std::string closeText = "Cancel";
    std::function<bool()> primary;
    bool primaryEnabled = true;
    double width = 0;             // 0 for the default width
};

// Shows a dialog, one at a time, and calls `done` after it closes. Returns the dialog so the caller can change it
// while it is open.
ctl::ContentDialog ShowDialog(xaml::XamlRoot const& root, const DialogOptions& options,
                              std::function<void()> done = nullptr);

// A short message dialog with an OK button.
void ShowMessage(xaml::XamlRoot const& root, std::string_view title, std::string_view message);
// A question with a destructive primary button: `confirm` runs when it is pressed.
void ShowConfirm(xaml::XamlRoot const& root, std::string_view title, std::string_view message,
                 std::string_view confirmText, std::function<void()> confirm);

}  // namespace kl::ui
