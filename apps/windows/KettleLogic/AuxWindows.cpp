#include "pch.h"

#include "MainWindow.h"

#include "kettlelogic/app/help_markdown.hpp"
#include "kettlelogic/app/text.hpp"

#include <regex>

namespace kl {

using namespace kettlelogic::app;
using platform::H;
using platform::U;
using winrt::box_value;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

// MARK: Brew day

namespace {

xaml::Media::SolidColorBrush AccentTint(double opacity) {
    winrt::Windows::UI::Color color = winrt::Microsoft::UI::ColorHelper::FromArgb(255, 0, 120, 212);
    try {
        const auto resources = xaml::Application::Current().Resources();
        const auto key = box_value(L"SystemAccentColor");
        if (resources.HasKey(key)) {
            color = winrt::unbox_value<winrt::Windows::UI::Color>(resources.Lookup(key));
        }
    } catch (winrt::hresult_error const&) {
    }
    xaml::Media::SolidColorBrush brush(color);
    brush.Opacity(opacity);
    return brush;
}

}  // namespace

// A large checklist of the procedure, for reading at the kettle. Click a step to tick it off. The first step not yet
// ticked is the current one. The display stays awake while it is open, unless the switch at the bottom turns that off.
BrewDayWindow::BrewDayWindow(Host& host, BrewDayPlan plan, std::function<void(BrewDayWindow*)> closed)
    : host_(host), plan_(std::move(plan)) {
    done_ = plan_.loadProgress(host_.store.preferences);
    window_.Title(H("Brew Day: " + plan_.recipeName));
    window_.Content(content_);
    try {
        window_.SystemBackdrop(xaml::Media::MicaBackdrop());
        const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(platform::WindowHandle(window_));
        winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id).Resize(winrt::Windows::Graphics::SizeInt32{720, 860});
    } catch (winrt::hresult_error const&) {
    }

    displayLock_.Set(host_.store.preferences.boolean(PrefKey::keepDisplayOn));

    timer_ = window_.DispatcherQueue().CreateTimer();
    timer_.Interval(std::chrono::seconds(1));
    timer_.IsRepeating(true);
    timer_.Tick([this](auto const&, auto const&) { Tick(); });
    timer_.Start();

    window_.Closed([this, closed = std::move(closed)](auto const&, auto const&) {
        timer_.Stop();
        displayLock_.Set(false);
        window_.DispatcherQueue().TryEnqueue([this, closed] { closed(this); });
    });
    Rebuild();
}

BrewDayWindow::~BrewDayWindow() {
    displayLock_.Set(false);
}

void BrewDayWindow::Tick() {
    const auto now = std::chrono::steady_clock::now();
    bool redraw = false;
    for (auto& [index, countdown] : countdowns_) {
        if (!countdown.end || countdown.finished) continue;
        const double left = std::chrono::duration<double>(*countdown.end - now).count();
        if (left <= 0) {
            countdown.finished = true;
            redraw = true;
        } else if (const auto clock = clocks_.find(index); clock != clocks_.end()) {
            clock->second.Text(H(clockText(left)));
        }
    }
    if (redraw) Rebuild();
}

void BrewDayWindow::Rebuild() {
    clocks_.clear();
    int current = -1;
    for (std::size_t i = 0; i < plan_.steps.size(); ++i) {
        if (done_.count(static_cast<int>(i)) == 0) {
            current = static_cast<int>(i);
            break;
        }
    }

    ctl::StackPanel steps;
    steps.Spacing(14);
    steps.Padding(xaml::Thickness{24, 20, 24, 20});

    ctl::StackPanel header;
    header.Spacing(6);
    ctl::TextBlock title = ui::Text(plan_.recipeName, ui::Kind::title);
    title.FontSize(34);
    header.Children().Append(title);
    ctl::TextBlock progress = ui::Text("Boil " + std::to_string(plan_.boilMinutes) + " minutes \xC2\xB7 " + std::to_string(done_.size()) +
                                           " of " + std::to_string(plan_.steps.size()) + " done",
                                       ui::Kind::secondary);
    progress.FontSize(18);
    header.Children().Append(progress);
    ctl::Button startOver = ui::Button("Start Over", [this] {
        done_.clear();
        plan_.saveProgress(host_.store.preferences, done_);
        Rebuild();
    });
    startOver.IsEnabled(!done_.empty());
    ui::SetTip(startOver, "Untick every step. Ticked steps are remembered if you close this window.");
    header.Children().Append(startOver);
    steps.Children().Append(header);

    for (std::size_t i = 0; i < plan_.steps.size(); ++i) {
        const int index = static_cast<int>(i);
        const calc::ProcedureStep& step = plan_.steps[i];
        const bool isDone = done_.count(index) != 0;
        const bool isCurrent = index == current;

        ctl::StackPanel column;
        column.Spacing(8);

        ctl::Grid card;
        card.ColumnSpacing(16);
        card.Padding(xaml::Thickness{18, 18, 18, 18});
        card.ColumnDefinitions().Append(ctl::ColumnDefinition());
        card.ColumnDefinitions().GetAt(0).Width(xaml::GridLength{40, xaml::GridUnitType::Pixel});
        ctl::ColumnDefinition textColumn;
        textColumn.Width(xaml::GridLength{1, xaml::GridUnitType::Star});
        card.ColumnDefinitions().Append(textColumn);

        ctl::FontIcon tick;
        tick.Glyph(isDone ? L"\xE73E" : L"\xEA3A");  // a check mark, or an empty circle
        tick.FontSize(30);
        tick.VerticalAlignment(xaml::VerticalAlignment::Top);
        tick.Foreground(isDone ? ui::ThemeBrush(L"SystemFillColorSuccessBrush") : ui::ThemeBrush(L"TextFillColorSecondaryBrush"));
        card.Children().Append(tick);

        ctl::StackPanel texts;
        texts.Spacing(6);
        ctl::TextBlock heading = ui::Text(step.minute ? step.phase + " \xC2\xB7 " + std::to_string(*step.minute) + " min" : step.phase,
                                          ui::Kind::strong);
        heading.Foreground(ui::ThemeBrush(L"TextFillColorSecondaryBrush"));
        texts.Children().Append(heading);
        ctl::TextBlock text = ui::Text(step.text);
        text.FontSize(isCurrent ? 28 : 22);
        if (isCurrent) text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        if (step.warning) text.Foreground(ui::ThemeBrush(L"SystemFillColorCriticalBrush"));
        if (isDone) {
            ctl::TextBlock struck = text;
            struck.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Strikethrough);
        }
        texts.Children().Append(text);
        ctl::Grid::SetColumn(texts, 1);
        card.Children().Append(texts);

        ctl::Button button;
        button.Content(card);
        button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
        button.CornerRadius(xaml::CornerRadius{14, 14, 14, 14});
        button.Padding(xaml::Thickness{0, 0, 0, 0});
        button.Background(isCurrent ? xaml::Media::Brush(AccentTint(0.18)) : ui::ThemeBrush(L"CardBackgroundFillColorDefaultBrush"));
        button.Opacity(isDone ? 0.5 : 1);
        ui::SetTip(button, isDone ? "Click to untick" : "Click to tick off");
        button.Click([this, index](auto const&, auto const&) {
            if (done_.count(index) != 0) done_.erase(index);
            else done_.insert(index);
            plan_.saveProgress(host_.store.preferences, done_);
            Rebuild();
        });
        column.Children().Append(button);

        // A step that needs a timer has one beneath it, outside the tick button so the two are not nested.
        if (step.timerMinutes) {
            const int minutes = static_cast<int>(*step.timerMinutes);
            Countdown& countdown = countdowns_[index];
            ctl::StackPanel timer;
            timer.Orientation(ctl::Orientation::Horizontal);
            timer.Spacing(12);
            timer.Margin(xaml::Thickness{18, 0, 0, 0});

            const auto start = [this, index, minutes] {
                Countdown& cd = countdowns_[index];
                cd.end = std::chrono::steady_clock::now() + std::chrono::seconds(minutes * 60);
                cd.secondsLeft.reset();
                cd.finished = false;
                Rebuild();
            };
            const auto big = [](ctl::Button button) {
                button.MinHeight(40);
                return button;
            };
            if (countdown.end) {
                if (countdown.finished) {
                    ctl::TextBlock up = ui::Text("\xF0\x9F\x94\x94 Time's up", ui::Kind::strong);
                    up.FontSize(22);
                    up.Foreground(ui::ThemeBrush(L"SystemFillColorCautionBrush"));
                    up.VerticalAlignment(xaml::VerticalAlignment::Center);
                    timer.Children().Append(up);
                } else {
                    const double left = std::chrono::duration<double>(*countdown.end - std::chrono::steady_clock::now()).count();
                    ctl::TextBlock clock = ui::Text(clockText(left), ui::Kind::strong);
                    clock.FontSize(28);
                    clock.VerticalAlignment(xaml::VerticalAlignment::Center);
                    clocks_[index] = clock;
                    timer.Children().Append(clock);
                    timer.Children().Append(big(ui::Button("Pause", [this, index] {
                        Countdown& cd = countdowns_[index];
                        if (!cd.end) return;
                        cd.secondsLeft = std::max(0.0, std::chrono::duration<double>(*cd.end - std::chrono::steady_clock::now()).count());
                        cd.end.reset();
                        Rebuild();
                    })));
                }
                timer.Children().Append(big(ui::Button("Restart", start)));
            } else if (countdown.secondsLeft) {
                ctl::TextBlock clock = ui::Text(clockText(*countdown.secondsLeft), ui::Kind::secondary);
                clock.FontSize(28);
                clock.VerticalAlignment(xaml::VerticalAlignment::Center);
                timer.Children().Append(clock);
                timer.Children().Append(big(ui::Button("Resume", [this, index] {
                    Countdown& cd = countdowns_[index];
                    if (!cd.secondsLeft) return;
                    cd.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(*cd.secondsLeft * 1000));
                    cd.secondsLeft.reset();
                    Rebuild();
                })));
                timer.Children().Append(big(ui::Button("Restart", start)));
            } else {
                ctl::Button begin = ui::Button("Start " + std::to_string(minutes) + "-minute timer", start);
                begin.Content(ui::HStack({ctl::SymbolIcon(ctl::Symbol::Clock), ui::Text("Start " + std::to_string(minutes) + "-minute timer")}, 8));
                timer.Children().Append(big(begin));
            }
            column.Children().Append(timer);
        }
        steps.Children().Append(column);
    }

    ctl::ScrollViewer scroller;
    scroller.VerticalScrollBarVisibility(ctl::ScrollBarVisibility::Auto);
    scroller.Content(steps);

    ctl::Grid root;
    ctl::RowDefinition bodyRow;
    bodyRow.Height(xaml::GridLength{1, xaml::GridUnitType::Star});
    ctl::RowDefinition footerRow;
    footerRow.Height(xaml::GridLength{1, xaml::GridUnitType::Auto});
    root.RowDefinitions().Append(bodyRow);
    root.RowDefinitions().Append(footerRow);
    root.Children().Append(scroller);

    ctl::Border footer;
    footer.Padding(xaml::Thickness{24, 12, 24, 12});
    footer.Background(ui::ThemeBrush(L"LayerFillColorDefaultBrush"));
    footer.BorderBrush(ui::ThemeBrush(L"DividerStrokeColorDefaultBrush"));
    footer.BorderThickness(xaml::Thickness{0, 1, 0, 0});
    ctl::ToggleSwitch keepOn;
    keepOn.Header(box_value(L"Keep display on"));
    keepOn.IsOn(host_.store.preferences.boolean(PrefKey::keepDisplayOn));
    keepOn.Toggled([this](auto const& sender, auto const&) {
        const bool on = sender.template as<ctl::ToggleSwitch>().IsOn();
        host_.store.preferences.setBoolean(PrefKey::keepDisplayOn, on);
        displayLock_.Set(on);
    });
    footer.Child(keepOn);
    ctl::Grid::SetRow(footer, 1);
    root.Children().Append(footer);
    content_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    content_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
    content_.Content(root);
}

// MARK: Help

// The user guide: the documents in a list, and the selected one beside it. A link to another document opens it in the
// same view, and a web link opens in the browser.
HelpWindow::HelpWindow(Host& host, std::function<void(HelpWindow*)> closed) : host_(host) {
    window_.Title(L"Kettle Logic Help");
    pages_ = helpPages(platform::ExecutableFolder() / L"Help");

    ctl::Grid root;
    root.ColumnDefinitions().Append(ctl::ColumnDefinition());
    root.ColumnDefinitions().GetAt(0).Width(xaml::GridLength{230, xaml::GridUnitType::Pixel});
    ctl::ColumnDefinition body;
    body.Width(xaml::GridLength{1, xaml::GridUnitType::Star});
    root.ColumnDefinitions().Append(body);

    if (pages_.empty()) {
        ctl::StackPanel missing;
        missing.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        missing.VerticalAlignment(xaml::VerticalAlignment::Center);
        missing.Spacing(6);
        missing.Children().Append(ui::Text("Help Is Missing", ui::Kind::subtitle));
        missing.Children().Append(ui::Text("The documentation was not found in this copy of the app.", ui::Kind::secondary));
        ctl::Grid::SetColumnSpan(missing, 2);
        root.Children().Append(missing);
    } else {
        for (const HelpPage& page : pages_) {
            list_.Items().Append(box_value(H(page.title)));
        }
        list_.SelectionChanged([this](auto const& sender, auto const&) {
            const int index = sender.template as<ctl::ListView>().SelectedIndex();
            if (index >= 0 && index < static_cast<int>(pages_.size())) Show(pages_[static_cast<std::size_t>(index)].id);
        });
        root.Children().Append(list_);
        ctl::Grid::SetColumn(web_, 1);
        root.Children().Append(web_);
        list_.SelectedIndex(0);
        Start();
    }
    window_.Content(root);
    try {
        window_.SystemBackdrop(xaml::Media::MicaBackdrop());
        const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(platform::WindowHandle(window_));
        winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id).Resize(winrt::Windows::Graphics::SizeInt32{1000, 720});
    } catch (winrt::hresult_error const&) {
    }
    window_.Closed([this, closed = std::move(closed)](auto const&, auto const&) {
        window_.DispatcherQueue().TryEnqueue([this, closed] { closed(this); });
    });
}

HelpWindow::~HelpWindow() {
    *alive_ = false;
}

// Starts the web view, then shows the page waiting for it.
winrt::fire_and_forget HelpWindow::Start() {
    const auto alive = alive_;
    const auto web = web_;
    co_await web.EnsureCoreWebView2Async();
    if (!*alive) co_return;
    web.CoreWebView2().NavigationStarting([this](auto const&, winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& args) {
        const std::string uri = U(args.Uri());
        static const std::string internal = "https://kettlelogic.help/";
        if (uri.rfind(internal, 0) == 0) {
            args.Cancel(true);
            const std::string id = uri.substr(internal.size());
            for (std::size_t i = 0; i < pages_.size(); ++i) {
                if (pages_[i].id == id) {
                    list_.SelectedIndex(static_cast<int>(i));
                    break;
                }
            }
        } else if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
            args.Cancel(true);
            platform::OpenUrl(uri);
        }
    });
    ready_ = true;
    if (!pending_.empty()) {
        const std::string id = pending_;
        pending_.clear();
        shown_.clear();
        Show(id);
    }
}

void HelpWindow::Show(const std::string& id) {
    if (!ready_) {
        pending_ = id;
        return;
    }
    if (shown_ == id) return;
    for (const HelpPage& page : pages_) {
        if (page.id != id) continue;
        shown_ = id;
        std::string markdown;
        try {
            markdown = platform::ReadTextFile(page.file);
        } catch (const std::exception&) {
        }
        std::string html = HelpMarkdown::html(markdown, page.title);
        // A link to another document stays inside the window: it is caught as it starts to load.
        static const std::regex link(R"re(href="([^"#:]+\.md)")re");
        html = std::regex_replace(html, link, "href=\"https://kettlelogic.help/$1\"");
        web_.NavigateToString(H(html));
        return;
    }
}

}  // namespace kl
