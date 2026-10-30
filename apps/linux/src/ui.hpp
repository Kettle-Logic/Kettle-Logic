#pragma once

#include <adwaita.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A small kit for building the app's screens from native GTK4 and libadwaita widgets in code. A screen is a Form:
// sections of rows, which is how the macOS app's grouped forms read, built from AdwPreferencesGroup, AdwActionRow,
// GtkEntry, GtkDropDown, GtkSwitch and the other standard widgets, and so styled by the system theme. It has the same
// shape as the Windows app's kit, so the screens of the two read alike.
namespace kl::ui {

using Callback = std::function<void()>;

enum class Kind { body, strong, caption, secondary, title, subtitle, mono, error, success };

// Runs `work` once, when the current event has finished. A screen is never rebuilt inside one of its own signal
// handlers: it is rebuilt from here.
void Later(Callback work);

// Calls `callback` when a signal with no arguments beyond the instance fires, such as "clicked" or "activate".
void On(gpointer instance, const char* signal, Callback callback);
// Calls `callback` when an action is activated. An action's signal carries a parameter, so it has its own.
void OnAction(GSimpleAction* action, Callback callback);
// Calls `callback` when a property changes.
void OnNotify(gpointer instance, const char* property, Callback callback);

GtkWidget* Text(std::string_view text, Kind kind = Kind::body);
// Sets the text of a label made by Text.
void SetText(GtkWidget* label, std::string_view text);

struct Choice {
    std::string value;
    std::string label;
};
std::vector<Choice> Choices(const std::vector<std::string>& values);

GtkWidget* Button(std::string_view label, Callback onClick, bool accent = false);
// A button with an icon from the icon theme, such as "list-remove-symbolic".
GtkWidget* IconButton(const char* icon, std::string_view tooltip, Callback onClick);
// A button with an icon and a label.
GtkWidget* IconLabelButton(const char* icon, std::string_view label, std::string_view tooltip, Callback onClick);
GtkWidget* LinkButton(std::string_view label, Callback onClick);

// A text field that commits when Enter is pressed or focus leaves it, and only when the text changed.
GtkWidget* Field(std::string_view text, std::function<void(std::string)> commit, double width = 110,
                 bool rightAligned = true, bool commitOnEnter = true);
GtkWidget* MultilineField(std::string_view text, std::function<void(std::string)> commit, double minHeight);
// A text field that reports every change, for the fields of a dialog.
GtkWidget* LiveField(std::string_view text, std::function<void(std::string)> changed, double width = 110,
                     bool rightAligned = true);
// A pick from a list. `onChange` is called with the value the user picked, not for a change made by the app.
GtkWidget* Picker(const std::vector<Choice>& choices, std::string_view selected,
                  std::function<void(std::string)> onChange, double width = 0);
GtkWidget* Switch(bool on, std::function<void(bool)> onChange);
GtkWidget* Check(std::string_view label, bool on, std::function<void(bool)> onChange);
// A day, as seconds from the Cocoa reference date, which is how the planning keeps dates.
GtkWidget* DateField(double referenceSeconds, std::function<void(double)> onChange);

GtkWidget* Spacer(double height);

// A horizontal row of widgets with space between them, and a vertical stack of them.
GtkWidget* HStack(std::initializer_list<GtkWidget*> items, double spacing = 8);
GtkWidget* VStack(std::initializer_list<GtkWidget*> items, double spacing = 8);

// A box of widgets built one at a time, and a CSS class from the theme, such as "error" or "warning".
GtkWidget* Box(GtkOrientation orientation, double spacing = 8);
void Append(GtkWidget* box, GtkWidget* child);
void Style(GtkWidget* widget, const char* cssClass);

void SetTip(GtkWidget* widget, std::string_view tip);
void Enable(GtkWidget* widget, bool enabled);
void Placeholder(GtkWidget* entry, std::string_view text);
void Margins(GtkWidget* widget, int start, int top, int end, int bottom);

// The labeled field a help term explains. In simple mode the explanation sits under the label. Otherwise an (i)
// button opens it in a popover.
GtkWidget* HelpLabel(std::string_view title, std::string_view explanation, bool simpleMode);

// MARK: Menus

// One line of a menu. A line with children opens a submenu, and a separator starts a new section.
struct MenuLine {
    std::string label;
    Callback action;
    bool enabled = true;
    bool separator = false;
    std::vector<MenuLine> children;

    static MenuLine Item(std::string label, Callback action, bool enabled = true) {
        return MenuLine{std::move(label), std::move(action), enabled, false, {}};
    }
    static MenuLine Sub(std::string label, std::vector<MenuLine> children, bool enabled = true) {
        return MenuLine{std::move(label), nullptr, enabled, false, std::move(children)};
    }
    static MenuLine Separator() { return MenuLine{{}, nullptr, true, true, {}}; }
};
using MenuBuilder = std::function<std::vector<MenuLine>()>;

// A menu button whose menu is built each time it opens. With a label and an icon, the icon comes first. With no icon
// it shows the label and an arrow.
GtkWidget* MenuButton(const char* icon, std::string_view label, std::string_view tooltip, MenuBuilder builder);
// Opens a menu built on demand when the widget is right-clicked or long-pressed.
void ContextMenu(GtkWidget* widget, MenuBuilder builder);

// MARK: Screens

// A scroller whose content is replaced without losing the place the user has scrolled to.
class Page {
public:
    Page();
    GtkWidget* Root() const { return scroller_; }
    void Set(GtkWidget* content);
    void ScrollToTop();
    void SetMinHeight(int height);
    GtkWidget* Content() const;

private:
    GtkWidget* scroller_;
    GtkWidget* viewport_;
};

// A screen of sections of rows.
class Form {
public:
    Form();

    // Starts a section, with an optional heading above it and a note under the heading.
    void Section(std::string_view header = {}, std::string_view footer = {});
    // A section whose heading has a button at its right, as the ingredient lists have.
    void SectionWithAction(std::string_view header, std::string_view actionLabel, Callback action,
                           std::string_view footer = {});

    // A label on the left and a control on the right, with an optional explanation under the label.
    void Labeled(std::string_view label, GtkWidget* control, std::string_view help = {});
    void Labeled(GtkWidget* label, GtkWidget* control);
    // A control that takes the whole row.
    void Wide(GtkWidget* widget);
    // A line of text in the section: secondary by default, or an error.
    void Note(std::string_view text, Kind kind = Kind::secondary);

    // The finished form for a screen: centered, with room around it. Put it in a Page.
    GtkWidget* Build();
    // The finished form without the room around it, to sit inside another screen or a dialog.
    GtkWidget* BuildPanel();

private:
    void EnsureGroup();
    void AddRow(GtkWidget* row);

    GtkWidget* page_;
    GtkWidget* group_ = nullptr;
};

// Keeps the keyboard focus across a rebuild of a screen. Capture before the old widgets go, and restore on the new
// ones. The widget is found by its place among the widgets that take focus, and the caret is put back with it.
class FocusKeeper {
public:
    void Capture(GtkWidget* screen);
    void Restore(GtkWidget* screen);

private:
    int index_ = -1;
    int caret_ = -1;
};

// MARK: Dialogs

class Dialog;

// A dialog over the window, with a title, content, and buttons. `primary` is called when the primary button is
// pressed, and returns false to keep the dialog open, such as when a value was refused.
struct DialogOptions {
    std::string title;
    GtkWidget* content = nullptr;
    std::string primaryText;      // empty for no primary button
    std::string closeText = "Cancel";  // empty for no way to cancel
    std::function<bool()> primary;
    bool primaryEnabled = true;
    int width = 0;                // 0 for the default width
};

class Dialog {
public:
    void SetPrimaryEnabled(bool enabled);
    void SetPrimaryText(std::string_view text);
    // A button beside Cancel, such as the wizard's Back. Hidden until it is given text.
    void SetSecondary(std::string_view text, Callback action);
    void Close();
    bool IsOpen() const { return dialog_ != nullptr; }

private:
    friend std::shared_ptr<Dialog> ShowDialog(GtkWidget*, const DialogOptions&, Callback);
    AdwDialog* dialog_ = nullptr;
    GtkWidget* primary_ = nullptr;
    GtkWidget* secondary_ = nullptr;
    Callback secondaryAction_;
};

// Shows a dialog and calls `done` after it closes. Returns the dialog so the caller can change it while it is open.
std::shared_ptr<Dialog> ShowDialog(GtkWidget* parent, const DialogOptions& options, Callback done = nullptr);

// Puts a form inside a scroller that stops growing at `maxHeight`, for a dialog.
GtkWidget* InDialog(GtkWidget* content, int maxHeight = 560);

// A short message dialog with an OK button.
void ShowMessage(GtkWidget* parent, std::string_view title, std::string_view message);
// A question with a destructive confirm button: `confirm` runs when it is pressed.
void ShowConfirm(GtkWidget* parent, std::string_view title, std::string_view message, std::string_view confirmText,
                 Callback confirm);

}  // namespace kl::ui
