#include "pch.h"

#include "Ui.h"

#include "kettlelogic/app/planning.hpp"

#include <algorithm>
#include <deque>
#include <vector>

namespace kl::ui {

using platform::H;
using platform::U;
using winrt::box_value;
using winrt::Microsoft::UI::Xaml::Thickness;

namespace {

xaml::Style LookupStyle(wchar_t const* key) {
    auto resources = xaml::Application::Current().Resources();
    const auto name = box_value(key);
    if (resources.HasKey(name)) {
        return resources.Lookup(name).as<xaml::Style>();
    }
    return nullptr;
}

std::string& CurrentTheme() {
    static std::string theme = "system";
    return theme;
}

std::vector<winrt::weak_ref<xaml::FrameworkElement>>& Followers() {
    static std::vector<winrt::weak_ref<xaml::FrameworkElement>> followers;
    return followers;
}

xaml::ElementTheme ElementThemeFor(const std::string& theme) {
    if (theme == "light") return xaml::ElementTheme::Light;
    if (theme == "dark") return xaml::ElementTheme::Dark;
    return xaml::ElementTheme::Default;
}

}  // namespace

void SetTheme(std::string_view theme) {
    CurrentTheme() = std::string(theme);
    auto& followers = Followers();
    std::erase_if(followers, [](const auto& weak) { return !weak.get(); });
    for (const auto& weak : followers) {
        if (const auto root = weak.get()) root.RequestedTheme(ElementThemeFor(CurrentTheme()));
    }
}

std::string Theme() {
    return CurrentTheme();
}

void FollowTheme(xaml::FrameworkElement const& root) {
    root.RequestedTheme(ElementThemeFor(CurrentTheme()));
    Followers().push_back(winrt::make_weak(root));
}

void UseStyle(xaml::FrameworkElement const& element, wchar_t const* key) {
    if (auto style = LookupStyle(key)) element.Style(style);
}

xaml::Media::SolidColorBrush Solid(int r, int g, int b) {
    return ColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                                                static_cast<uint8_t>(b)));
}

xaml::Media::SolidColorBrush ColorBrush(winrt::Windows::UI::Color color) {
    return xaml::Media::SolidColorBrush(color);
}

ctl::TextBlock Text(std::string_view text, Kind kind) {
    ctl::TextBlock block;
    block.Text(H(text));
    block.TextWrapping(xaml::TextWrapping::Wrap);
    switch (kind) {
        case Kind::body: break;
        case Kind::strong:
            if (auto style = LookupStyle(L"BodyStrongTextBlockStyle")) block.Style(style);
            break;
        case Kind::caption:
            UseStyle(block, L"KLCaptionText");
            break;
        case Kind::secondary:
            UseStyle(block, L"KLSecondaryText");
            break;
        case Kind::title:
            if (auto style = LookupStyle(L"TitleTextBlockStyle")) block.Style(style);
            break;
        case Kind::subtitle:
            if (auto style = LookupStyle(L"SubtitleTextBlockStyle")) block.Style(style);
            break;
        case Kind::mono:
            block.FontFamily(xaml::Media::FontFamily(L"Consolas"));
            break;
        case Kind::error:
            UseStyle(block, L"KLErrorText");
            break;
        case Kind::success:
            UseStyle(block, L"KLSuccessText");
            break;
    }
    return block;
}

std::vector<Choice> Choices(const std::vector<std::string>& values) {
    std::vector<Choice> out;
    for (const std::string& value : values) {
        out.push_back({value, value});
    }
    return out;
}

void SetTip(xaml::DependencyObject const& element, std::string_view tip) {
    ctl::ToolTipService::SetToolTip(element, box_value(H(tip)));
}

ctl::Button Button(std::string_view label, std::function<void()> onClick, bool accent) {
    ctl::Button button;
    button.Content(box_value(H(label)));
    if (accent) {
        if (auto style = LookupStyle(L"AccentButtonStyle")) button.Style(style);
    }
    button.Click([onClick = std::move(onClick)](auto const&, auto const&) {
        if (onClick) onClick();
    });
    return button;
}

ctl::Button IconButton(ctl::Symbol symbol, std::string_view tip, std::function<void()> onClick) {
    ctl::Button button;
    button.Content(ctl::SymbolIcon(symbol));
    button.Background(ColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
    button.BorderThickness(Thickness{0, 0, 0, 0});
    SetTip(button, tip);
    // An icon has no text, so the tip is also what a screen reader says.
    xaml::Automation::AutomationProperties::SetName(button, H(tip));
    button.Click([onClick = std::move(onClick)](auto const&, auto const&) {
        if (onClick) onClick();
    });
    return button;
}

ctl::HyperlinkButton LinkButton(std::string_view label, std::function<void()> onClick) {
    ctl::HyperlinkButton button;
    button.Content(box_value(H(label)));
    button.Padding(Thickness{0, 0, 0, 0});
    button.Click([onClick = std::move(onClick)](auto const&, auto const&) {
        if (onClick) onClick();
    });
    return button;
}

ctl::TextBox Field(std::string_view text, std::function<void(std::string)> commit, double width, bool rightAligned,
                   bool commitOnEnter) {
    ctl::TextBox box;
    box.Text(H(text));
    if (width > 0) box.Width(width);
    if (rightAligned) box.TextAlignment(xaml::TextAlignment::Right);
    box.VerticalAlignment(xaml::VerticalAlignment::Center);

    // What the field held when it was last committed, so an unchanged field commits nothing.
    auto committed = std::make_shared<std::string>(text);
    auto run = [committed, commit = std::move(commit)](ctl::TextBox const& source) {
        std::string now = U(source.Text());
        if (now == *committed) return;
        *committed = now;
        if (commit) commit(std::move(now));
    };
    box.LostFocus([run](auto const& sender, auto const&) { run(sender.template as<ctl::TextBox>()); });
    if (commitOnEnter) {
        box.KeyDown([run](auto const& sender, xaml::Input::KeyRoutedEventArgs const& args) {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter) {
                run(sender.template as<ctl::TextBox>());
            }
        });
    }
    return box;
}

ctl::TextBox MultilineField(std::string_view text, std::function<void(std::string)> commit, double minHeight) {
    ctl::TextBox box = Field(text, std::move(commit), 0, false, false);
    box.AcceptsReturn(true);
    box.TextWrapping(xaml::TextWrapping::Wrap);
    box.MinHeight(minHeight);
    box.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    box.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    return box;
}

ctl::ComboBox Picker(const std::vector<Choice>& choices, std::string_view selected,
                     std::function<void(std::string)> onChange, double width) {
    ctl::ComboBox combo;
    auto values = std::make_shared<std::vector<std::string>>();
    int selectedIndex = -1;
    for (const Choice& choice : choices) {
        if (choice.value == selected) selectedIndex = static_cast<int>(values->size());
        values->push_back(choice.value);
        combo.Items().Append(box_value(H(choice.label)));
    }
    if (selectedIndex < 0 && !selected.empty()) {
        // A value the list does not offer, such as a unit an imported recipe uses, stays selectable.
        selectedIndex = static_cast<int>(values->size());
        values->push_back(std::string(selected));
        combo.Items().Append(box_value(H(selected)));
    }
    combo.SelectedIndex(selectedIndex);
    if (width > 0) combo.Width(width);
    combo.VerticalAlignment(xaml::VerticalAlignment::Center);
    combo.SelectionChanged([values, onChange = std::move(onChange)](auto const& sender, auto const&) {
        const int index = sender.template as<ctl::ComboBox>().SelectedIndex();
        if (index >= 0 && index < static_cast<int>(values->size()) && onChange) {
            onChange((*values)[static_cast<std::size_t>(index)]);
        }
    });
    return combo;
}

ctl::ToggleSwitch Switch(bool on, std::function<void(bool)> onChange) {
    ctl::ToggleSwitch toggle;
    toggle.OnContent(box_value(L""));
    toggle.OffContent(box_value(L""));
    toggle.MinWidth(0);
    toggle.IsOn(on);
    toggle.Toggled([onChange = std::move(onChange)](auto const& sender, auto const&) {
        if (onChange) onChange(sender.template as<ctl::ToggleSwitch>().IsOn());
    });
    return toggle;
}

ctl::CheckBox Check(std::string_view label, bool on, std::function<void(bool)> onChange) {
    ctl::CheckBox box;
    box.Content(box_value(H(label)));
    box.IsChecked(on);
    auto handler = [onChange = std::move(onChange)](auto const& sender, auto const&) {
        if (onChange) onChange(sender.template as<ctl::CheckBox>().IsChecked().Value());
    };
    box.Checked(handler);
    box.Unchecked(handler);
    return box;
}

namespace {

constexpr double kReferenceToUnix = 978307200.0;

winrt::Windows::Foundation::DateTime ToDateTime(double referenceSeconds) {
    return winrt::clock::from_time_t(static_cast<std::time_t>(referenceSeconds + kReferenceToUnix));
}

}  // namespace

ctl::CalendarDatePicker DateField(double referenceSeconds, std::function<void(double)> onChange) {
    ctl::CalendarDatePicker picker;
    picker.Date(winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime>(ToDateTime(referenceSeconds)));
    picker.DateChanged([onChange = std::move(onChange)](auto const&, ctl::CalendarDatePickerDateChangedEventArgs const& args) {
        const auto date = args.NewDate();
        if (!date || !onChange) return;
        // The picker gives a moment on the chosen day. Keep the day, at midday, so it is the same in any time zone.
        const double reference = static_cast<double>(winrt::clock::to_time_t(date.Value())) - kReferenceToUnix;
        onChange(kettlelogic::app::referenceSecondsAtMidday(kettlelogic::app::civilDateOf(reference)));
    });
    return picker;
}

xaml::FrameworkElement Spacer(double height) {
    ctl::Border spacer;
    spacer.Height(height);
    return spacer;
}

ctl::StackPanel HStack(std::initializer_list<xaml::UIElement> items, double spacing) {
    ctl::StackPanel panel;
    panel.Orientation(ctl::Orientation::Horizontal);
    panel.Spacing(spacing);
    for (const auto& item : items) {
        // A label next to a field lines up with the field's middle, not its top. Items that set their own alignment keep it.
        if (const auto element = item.try_as<xaml::FrameworkElement>()) {
            if (element.VerticalAlignment() == xaml::VerticalAlignment::Stretch) {
                element.VerticalAlignment(xaml::VerticalAlignment::Center);
            }
        }
        panel.Children().Append(item);
    }
    return panel;
}

ctl::StackPanel VStack(std::initializer_list<xaml::UIElement> items, double spacing) {
    ctl::StackPanel panel;
    panel.Orientation(ctl::Orientation::Vertical);
    panel.Spacing(spacing);
    for (const auto& item : items) panel.Children().Append(item);
    return panel;
}

xaml::UIElement HelpLabel(std::string_view title, std::string_view explanation, bool simpleMode) {
    if (simpleMode) {
        return VStack({Text(title), Text(explanation, Kind::caption)}, 2);
    }
    ctl::Button info;
    ctl::FontIcon glyph;
    glyph.Glyph(L"\xE946");  // the Info icon in Segoe Fluent Icons
    glyph.FontSize(14);
    info.Content(glyph);
    info.Padding(Thickness{4, 4, 4, 4});
    info.Background(ColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
    info.BorderThickness(Thickness{0, 0, 0, 0});
    UseStyle(info, L"KLInfoButton");
    SetTip(info, explanation);
    ctl::Flyout flyout;
    ctl::TextBlock body = Text(explanation);
    body.MaxWidth(280);
    flyout.Content(body);
    info.Flyout(flyout);
    return HStack({Text(title), info}, 4);
}

// MARK: Form

Form::Form() {
    page_.Spacing(6);
    page_.Padding(Thickness{20, 16, 20, 28});
    page_.MaxWidth(840);
    page_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
}

void Form::Flush() {
    if (!rows_) {
        return;
    }
    ctl::Border card;
    UseStyle(card, L"KLCardBorder");
    card.BorderThickness(Thickness{1, 1, 1, 1});
    card.CornerRadius(xaml::CornerRadius{8, 8, 8, 8});
    card.Child(rows_);
    page_.Children().Append(card);
    if (!footer_.empty()) {
        ctl::TextBlock note = Text(footer_, Kind::caption);
        note.Margin(Thickness{4, 2, 4, 0});
        page_.Children().Append(note);
    }
    rows_ = nullptr;
    footer_.clear();
}

void Form::Section(std::string_view header, std::string_view footer) {
    Flush();
    // Space between the sections.
    if (page_.Children().Size() > 0) {
        page_.Children().Append(Spacer(10));
    }
    if (!header.empty()) {
        ctl::TextBlock title = Text(header, Kind::strong);
        title.Margin(Thickness{4, 0, 4, 2});
        page_.Children().Append(title);
    }
    rows_ = ctl::StackPanel();
    footer_ = std::string(footer);
}

void Form::SectionWithAction(std::string_view header, std::string_view actionLabel, std::function<void()> action,
                             std::string_view footer) {
    Flush();
    if (page_.Children().Size() > 0) {
        page_.Children().Append(Spacer(10));
    }
    ctl::Grid bar;
    bar.ColumnDefinitions().Append(ctl::ColumnDefinition());
    ctl::ColumnDefinition actionColumn;
    actionColumn.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
    bar.ColumnDefinitions().Append(actionColumn);
    ctl::TextBlock title = Text(header, Kind::strong);
    title.VerticalAlignment(xaml::VerticalAlignment::Center);
    title.Margin(Thickness{4, 0, 4, 0});
    bar.Children().Append(title);
    ctl::Button add;
    ctl::StackPanel content = HStack({ctl::SymbolIcon(ctl::Symbol::Add), Text(actionLabel)}, 6);
    add.Content(content);
    add.Background(ColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
    add.BorderThickness(Thickness{0, 0, 0, 0});
    SetTip(add, std::string(actionLabel) + " " + std::string(header));
    xaml::Automation::AutomationProperties::SetName(add, H(actionLabel));
    add.Click([action = std::move(action)](auto const&, auto const&) {
        if (action) action();
    });
    ctl::Grid::SetColumn(add, 1);
    bar.Children().Append(add);
    page_.Children().Append(bar);
    rows_ = ctl::StackPanel();
    footer_ = std::string(footer);
}

void Form::AddRow(xaml::UIElement const& row) {
    if (!rows_) {
        Section();
    }
    if (rows_.Children().Size() > 0) {
        ctl::Border divider;
        divider.Height(1);
        UseStyle(divider, L"KLDivider");
        rows_.Children().Append(divider);
    }
    ctl::Border cell;
    cell.Padding(Thickness{16, 9, 16, 9});
    cell.Child(row);
    rows_.Children().Append(cell);
}

namespace {

// A switch, text box, or list has no text of its own, so a screen reader would announce it without a name. The label
// beside it names it, which also lets the smoke test find it. A row's control can be a stack that holds the field and
// a unit, so the first such control inside is named.
void NameFromLabel(xaml::DependencyObject const& control, winrt::hstring const& label) {
    if (control.try_as<ctl::TextBox>() || control.try_as<ctl::ComboBox>() || control.try_as<ctl::ToggleSwitch>() ||
        control.try_as<ctl::Slider>()) {
        if (xaml::Automation::AutomationProperties::GetName(control).empty()) {
            xaml::Automation::AutomationProperties::SetName(control, label);
        }
        return;
    }
    if (const auto panel = control.try_as<ctl::Panel>()) {
        for (const auto& child : panel.Children()) {
            if (child.try_as<ctl::TextBox>() || child.try_as<ctl::ComboBox>() || child.try_as<ctl::ToggleSwitch>() ||
                child.try_as<ctl::Slider>()) {
                NameFromLabel(child, label);
                return;
            }
        }
    }
}

}  // namespace

void Form::Labeled(std::string_view label, xaml::UIElement const& control, std::string_view help) {
    NameFromLabel(control, H(label));
    xaml::UIElement left = help.empty() ? xaml::UIElement(Text(label)) : xaml::UIElement(VStack({Text(label), Text(help, Kind::caption)}, 2));
    Labeled(left, control);
}

void Form::Labeled(xaml::UIElement const& label, xaml::UIElement const& control) {
    ctl::Grid grid;
    grid.ColumnSpacing(16);
    grid.ColumnDefinitions().Append(ctl::ColumnDefinition());
    ctl::ColumnDefinition right;
    right.Width(xaml::GridLength{1, xaml::GridUnitType::Auto});
    grid.ColumnDefinitions().Append(right);
    if (auto frame = label.try_as<xaml::FrameworkElement>()) {
        frame.VerticalAlignment(xaml::VerticalAlignment::Center);
    }
    grid.Children().Append(label);
    if (auto frame = control.try_as<xaml::FrameworkElement>()) {
        frame.VerticalAlignment(xaml::VerticalAlignment::Center);
        frame.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    }
    ctl::Grid::SetColumn(control.as<xaml::FrameworkElement>(), 1);
    grid.Children().Append(control);
    AddRow(grid);
}

void Form::Wide(xaml::UIElement const& element) {
    AddRow(element);
}

void Form::Note(std::string_view text, Kind kind) {
    AddRow(Text(text, kind));
}

xaml::UIElement Form::BuildPanel() {
    Flush();
    page_.Padding(Thickness{0, 0, 0, 0});
    return page_;
}

xaml::UIElement Form::Build() {
    Flush();
    ctl::ScrollViewer scroller;
    scroller.VerticalScrollBarVisibility(ctl::ScrollBarVisibility::Auto);
    scroller.HorizontalScrollBarVisibility(ctl::ScrollBarVisibility::Disabled);
    // The page is a column centered in the window, as wide as the window up to 840, so the margin is the same on each
    // side. A stretched panel with a maximum width does not center reliably inside a ScrollViewer, so the width is set.
    page_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    scroller.SizeChanged([page = page_](auto const&, xaml::SizeChangedEventArgs const& args) {
        page.Width((std::min)(840.0, static_cast<double>(args.NewSize().Width)));
    });
    scroller.Content(page_);
    return scroller;
}

// MARK: Focus

namespace {

void Collect(xaml::DependencyObject const& node, std::vector<xaml::UIElement>& out) {
    const int count = xaml::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; ++i) {
        const xaml::DependencyObject child = xaml::Media::VisualTreeHelper::GetChild(node, i);
        // These take focus themselves. Their templates hold parts, such as a clear button, that are not listed.
        if (child.try_as<ctl::TextBox>() || child.try_as<ctl::ComboBox>() || child.try_as<ctl::ToggleSwitch>() ||
            child.try_as<ctl::CheckBox>() || child.try_as<ctl::CalendarDatePicker>() ||
            child.try_as<ctl::Primitives::ButtonBase>()) {
            out.push_back(child.as<xaml::UIElement>());
            continue;
        }
        Collect(child, out);
    }
}

}  // namespace

void FocusKeeper::Capture(xaml::XamlRoot const& root, xaml::UIElement const& screen) {
    index_ = -1;
    if (!root || !screen) return;
    const auto focused = xaml::Input::FocusManager::GetFocusedElement(root);
    if (!focused) return;
    std::vector<xaml::UIElement> controls;
    Collect(screen, controls);
    for (xaml::DependencyObject at = focused.try_as<xaml::DependencyObject>(); at;
         at = xaml::Media::VisualTreeHelper::GetParent(at)) {
        for (std::size_t i = 0; i < controls.size(); ++i) {
            if (controls[i] == at) {
                index_ = static_cast<int>(i);
                return;
            }
        }
    }
}

void FocusKeeper::Restore(xaml::UIElement const& screen) {
    if (index_ < 0 || !screen) return;
    std::vector<xaml::UIElement> controls;
    Collect(screen, controls);
    if (index_ < static_cast<int>(controls.size())) {
        controls[static_cast<std::size_t>(index_)].Focus(xaml::FocusState::Programmatic);
    }
}

// MARK: Dialogs

namespace {

struct DialogQueue {
    std::deque<std::function<void()>> waiting;
    bool open = false;
};

DialogQueue& Queue() {
    static DialogQueue queue;
    return queue;
}

void RunNext() {
    DialogQueue& queue = Queue();
    if (queue.open || queue.waiting.empty()) return;
    queue.open = true;
    auto next = std::move(queue.waiting.front());
    queue.waiting.pop_front();
    next();
}

}  // namespace

ctl::ContentDialog ShowDialog(xaml::XamlRoot const& root, const DialogOptions& options, std::function<void()> done) {
    ctl::ContentDialog dialog;
    dialog.XamlRoot(root);
    FollowTheme(dialog);
    dialog.Title(box_value(H(options.title)));
    if (options.content) dialog.Content(options.content);
    if (!options.primaryText.empty()) {
        dialog.PrimaryButtonText(H(options.primaryText));
        dialog.DefaultButton(ctl::ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(options.primaryEnabled);
        dialog.PrimaryButtonClick([primary = options.primary](auto const&, ctl::ContentDialogButtonClickEventArgs const& args) {
            if (primary && !primary()) {
                args.Cancel(true);
            }
        });
    }
    if (!options.closeText.empty()) dialog.CloseButtonText(H(options.closeText));
    if (options.width > 0) {
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(options.width));
        dialog.Resources().Insert(box_value(L"ContentDialogMinWidth"), box_value(options.width));
    }

    Queue().waiting.push_back([dialog, done = std::move(done)]() {
        try {
            auto operation = dialog.ShowAsync();
            operation.Completed([done](auto const&, auto) {
                Queue().open = false;
                if (done) done();
                RunNext();
            });
        } catch (winrt::hresult_error const&) {
            Queue().open = false;
            RunNext();
        }
    });
    RunNext();
    return dialog;
}

void ShowMessage(xaml::XamlRoot const& root, std::string_view title, std::string_view message) {
    DialogOptions options;
    options.title = std::string(title);
    ctl::TextBlock body = Text(message);
    body.IsTextSelectionEnabled(true);
    options.content = body;
    options.closeText = "OK";
    ShowDialog(root, options);
}

void ShowConfirm(xaml::XamlRoot const& root, std::string_view title, std::string_view message,
                 std::string_view confirmText, std::function<void()> confirm) {
    DialogOptions options;
    options.title = std::string(title);
    options.content = Text(message);
    options.primaryText = std::string(confirmText);
    options.primary = [confirm = std::move(confirm)]() {
        if (confirm) confirm();
        return true;
    };
    ShowDialog(root, options);
}

}  // namespace kl::ui
