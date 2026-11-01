#include "pch.h"

#include "MainWindow.h"

#include "AppUpdates.h"
#include "Dialogs.h"
#include "Telemetry.h"

#include "kettlelogic/app/text.hpp"
#include "kettlelogic/version.hpp"

namespace kl {

using namespace kettlelogic::app;
using platform::H;
using platform::U;
using winrt::box_value;
using winrt::unbox_value_or;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::System::VirtualKey;
using winrt::Windows::System::VirtualKeyModifiers;

namespace {

// The About dialog's text: the app's version, which the project sets, and the engine's, which the engine reports.
std::string AboutText() {
    return std::string("Version ") + KETTLELOGIC_UI_VERSION + "\nEngine version " + kettlelogic::kEngineVersion;
}

AppPaths MakePaths() {
    return AppPaths{platform::HomeFolder(), platform::DataFolder()};
}

const std::vector<platform::FileType>& RecipeTypes() {
    static const std::vector<platform::FileType> types = {{L"BeerJSON recipes", L"*.json"}};
    return types;
}

xaml::Input::KeyboardAccelerator Accelerator(VirtualKey key, VirtualKeyModifiers modifiers) {
    xaml::Input::KeyboardAccelerator accelerator;
    accelerator.Key(key);
    accelerator.Modifiers(modifiers);
    return accelerator;
}

// The tag of a navigation item: a kind letter, then its parts, separated by "|".
std::vector<std::string> SplitTag(const std::string& tag) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : tag) {
        if (c == '|') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string RecipeTag(const std::string& document, EntityId id) {
    return "r|" + document + "|" + std::to_string(id);
}

std::string FolderTag(const std::string& document, EntityId id) {
    return "f|" + document + "|" + std::to_string(id);
}

std::string TagOf(ctl::NavigationViewItemBase const& item) {
    return U(unbox_value_or<winrt::hstring>(item.Tag(), L""));
}

// Finds the navigation item with a tag, among the items and their children.
ctl::NavigationViewItem FindItem(winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> const& items,
                                 const std::string& tag) {
    for (const auto& entry : items) {
        if (auto item = entry.try_as<ctl::NavigationViewItem>()) {
            if (TagOf(item) == tag) {
                return item;
            }
            if (auto found = FindItem(item.MenuItems(), tag)) {
                return found;
            }
        }
    }
    return nullptr;
}

ctl::MenuFlyoutItem MenuItem(const std::string& text, std::function<void()> action) {
    ctl::MenuFlyoutItem item;
    item.Text(H(text));
    item.Click([action = std::move(action)](auto const&, auto const&) {
        if (action) action();
    });
    return item;
}

}  // namespace

// MARK: Window

MainWindow::MainWindow() : store_(MakePaths()), host_{store_, window_} {
    window_.Title(L"Kettle Logic");
    try {
        window_.SystemBackdrop(xaml::Media::MicaBackdrop());
    } catch (winrt::hresult_error const&) {
        // A system without Mica keeps the plain window background.
    }

    host_.refresh = [this] { ScheduleRefresh(); };
    host_.openTool = [this](ToolKind kind, std::map<std::string, std::string> prefill) {
        OpenToolWindow(kind, std::move(prefill));
    };
    host_.openBrewDay = [this](const BrewDayPlan& plan) { OpenBrewDayWindow(plan); };
    host_.openHelp = [this] { OpenHelpWindow(); };
    host_.openFile = [this] { OpenFile(); };
    store_.changed = [this] { ScheduleRefresh(); };

    BuildMenu();
    BuildToolbar();
    BuildLayout();
    AddAccelerators();
    window_.Content(root_);

    const auto queue = window_.DispatcherQueue();
    refreshTimer_ = queue.CreateTimer();
    refreshTimer_.Interval(std::chrono::milliseconds(120));
    refreshTimer_.IsRepeating(false);
    refreshTimer_.Tick([this](auto const&, auto const&) {
        refreshTimer_.Stop();
        Refresh();
    });
    autosaveTimer_ = queue.CreateTimer();
    autosaveTimer_.Interval(std::chrono::milliseconds(500));
    autosaveTimer_.IsRepeating(true);
    autosaveTimer_.Tick([this](auto const&, auto const&) { store_.tick(); });
    autosaveTimer_.Start();

    window_.Closed([this](auto const&, auto const&) {
        // Whatever is waiting for its autosave is saved before the app ends.
        store_.tick(DocumentStore::Clock::now() + std::chrono::hours(1));
        xaml::Application::Current().Exit();
    });

    root_.Loaded([this](auto const&, auto const&) {
        if (startupDialogsShown_) return;
        startupDialogsShown_ = true;
        if (!store_.preferences.boolean(PrefKey::interfaceChosen)) {
            // The first-launch dialog asks about telemetry too, and starts it when it is answered.
            dialogs::ShowInterfaceChoice(host_);
        } else if (!store_.preferences.boolean(PrefKey::telemetryNoticeShown)) {
            // An upgrade from a version that never asked: tell the user once, and start when they have read it.
            dialogs::ShowTelemetryNotice(host_);
        } else {
            telemetry::Start(store_.preferences);
        }
        Refresh();
        if (store_.preferences.boolean(PrefKey::checkForUpdates)) {
            CheckForUpdates(false);
        }
    });

    store_.loadRecipeFolder();
    Refresh();
}

MainWindow::~MainWindow() {
    telemetry::Stop();
}

// A check the user asked for always says what it found. The automatic check at launch speaks only to offer an update.
void MainWindow::CheckForUpdates(bool asked) {
    update_check::CheckInBackground(window_.DispatcherQueue(), [this, asked](kettlelogic::app::updates::CheckResult result) {
        namespace analytics = kettlelogic::app::analytics;
        const char* outcome = result.status == kettlelogic::app::updates::CheckResult::Status::available ? "available"
                              : result.status == kettlelogic::app::updates::CheckResult::Status::upToDate ? "up_to_date"
                                                                                                           : "failed";
        telemetry::Record(analytics::Event::updateCheck, {analytics::Param::word("result", outcome)});
        switch (result.status) {
        case kettlelogic::app::updates::CheckResult::Status::available:
            update_check::Offer(host_.xamlRoot(), window_.DispatcherQueue(), *result.release);
            break;
        case kettlelogic::app::updates::CheckResult::Status::upToDate:
            if (asked) ui::ShowMessage(host_.xamlRoot(), "Kettle Logic Is Up to Date", "This is the newest version.");
            break;
        case kettlelogic::app::updates::CheckResult::Status::failed:
            if (asked) ui::ShowMessage(host_.xamlRoot(), "Could Not Check for Updates", result.message);
            break;
        }
    });
}

void MainWindow::Activate() {
    try {
        const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(platform::WindowHandle(window_));
        const auto appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id);
        appWindow.Resize(winrt::Windows::Graphics::SizeInt32{1320, 860});
    } catch (winrt::hresult_error const&) {
    }
    window_.Activate();
}

// MARK: Menu

void MainWindow::BuildMenu() {
    const auto withKey = [](ctl::MenuFlyoutItem item, VirtualKey key, VirtualKeyModifiers modifiers) {
        item.KeyboardAccelerators().Append(Accelerator(key, modifiers));
        return item;
    };
    const auto control = VirtualKeyModifiers::Control;
    const auto controlShift = VirtualKeyModifiers::Control | VirtualKeyModifiers::Shift;

    ctl::MenuBarItem file;
    file.Title(L"File");
    file.Items().Append(withKey(MenuItem("New Recipe", [this] { store_.newRecipe(); }), VirtualKey::N, control));
    file.Items().Append(withKey(MenuItem("New Recipe from a Style…", [this] { dialogs::ShowWizard(host_); }),
                                VirtualKey::N, controlShift));
    file.Items().Append(withKey(MenuItem("Open…", [this] { OpenFile(); }), VirtualKey::O, control));
    file.Items().Append(ctl::MenuFlyoutSeparator());
    saveItem_ = withKey(MenuItem("Save", [this] { Save(); }), VirtualKey::S, control);
    file.Items().Append(saveItem_);
    saveAsItem_ = withKey(MenuItem("Save As…", [this] { SaveAs(); }), VirtualKey::S, controlShift);
    file.Items().Append(saveAsItem_);
    file.Items().Append(ctl::MenuFlyoutSeparator());
    // Ctrl+, is registered in AddAccelerators. As a menu accelerator it crashes WinUI when the menu draws its text.
    const auto settingsItem = MenuItem("Settings…", [this] { dialogs::ShowSettings(host_); });
    settingsItem.KeyboardAcceleratorTextOverride(L"Ctrl+,");
    file.Items().Append(settingsItem);
    menuBar_.Items().Append(file);

    ctl::MenuBarItem edit;
    edit.Title(L"Edit");
    undoItem_ = withKey(MenuItem("Undo", [this] { store_.undo(); }), VirtualKey::Z, control);
    redoItem_ = withKey(MenuItem("Redo", [this] { store_.redo(); }), VirtualKey::Y, control);
    edit.Items().Append(undoItem_);
    edit.Items().Append(redoItem_);
    menuBar_.Items().Append(edit);

    ctl::MenuBarItem view;
    view.Title(L"View");
    simpleModeItem_ = ctl::ToggleMenuFlyoutItem();
    simpleModeItem_.Text(L"Simple Mode");
    simpleModeItem_.KeyboardAccelerators().Append(Accelerator(VirtualKey::M, control | VirtualKeyModifiers::Menu));
    simpleModeItem_.Click([this](auto const&, auto const&) {
        store_.preferences.setBoolean(PrefKey::simpleMode, simpleModeItem_.IsChecked());
        ScheduleRefresh();
    });
    view.Items().Append(simpleModeItem_);
    menuBar_.Items().Append(view);

    ctl::MenuBarItem tools;
    tools.Title(L"Tools");
    for (const ToolKind kind : calculators()) {
        tools.Items().Append(MenuItem(toolSpec(kind).title, [this, kind] { OpenToolWindow(kind, {}); }));
    }
    menuBar_.Items().Append(tools);

    ctl::MenuBarItem unitTools;
    unitTools.Title(L"Unit Tools");
    for (const ToolKind kind : converters()) {
        unitTools.Items().Append(MenuItem(toolSpec(kind).title, [this, kind] { OpenToolWindow(kind, {}); }));
    }
    menuBar_.Items().Append(unitTools);

    ctl::MenuBarItem help;
    help.Title(L"Help");
    help.Items().Append(withKey(MenuItem("Kettle Logic Help", [this] { OpenHelpWindow(); }), VirtualKey::F1,
                                VirtualKeyModifiers::None));
    help.Items().Append(ctl::MenuFlyoutSeparator());
    help.Items().Append(MenuItem("Check for Updates…", [this] { CheckForUpdates(true); }));
    help.Items().Append(MenuItem("About Kettle Logic", [this] {
        ui::ShowMessage(host_.xamlRoot(), "About Kettle Logic", AboutText());
    }));
    menuBar_.Items().Append(help);
}

void MainWindow::AddAccelerators() {
    // Ctrl+1 to Ctrl+9 go to a tab of the open recipe, or to a group of tabs when the tabs are grouped.
    for (int number = 1; number <= 9; ++number) {
        auto accelerator = Accelerator(static_cast<VirtualKey>(static_cast<int>(VirtualKey::Number1) + number - 1),
                                       VirtualKeyModifiers::Control);
        accelerator.Invoked([this, number](auto const&, xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
            if (!store_.selection()) return;
            args.Handled(true);
            if (onTabShortcut_) onTabShortcut_(number);
        });
        root_.KeyboardAccelerators().Append(accelerator);
    }

    // Ctrl+, opens Settings. 0xBC is the comma key, which VirtualKey has no name for.
    auto settings = Accelerator(static_cast<VirtualKey>(0xBC), VirtualKeyModifiers::Control);
    settings.Invoked([this](auto const&, xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
        args.Handled(true);
        dialogs::ShowSettings(host_);
    });
    root_.KeyboardAccelerators().Append(settings);
}

// MARK: Layout

void MainWindow::BuildToolbar() {
    toolbar_.DefaultLabelPosition(ctl::CommandBarDefaultLabelPosition::Right);
    toolbar_.IsOpen(false);
    toolbar_.HorizontalAlignment(xaml::HorizontalAlignment::Right);

    statusText_ = ui::Text("", ui::Kind::secondary);
    statusText_.VerticalAlignment(xaml::VerticalAlignment::Center);
    statusText_.Margin(xaml::Thickness{0, 0, 8, 0});
    ctl::AppBarElementContainer status;
    status.Content(statusText_);
    toolbar_.PrimaryCommands().Append(status);

    const auto makeButton = [](ctl::AppBarButton& button, ctl::Symbol symbol, const wchar_t* label) {
        button = ctl::AppBarButton();
        button.Icon(ctl::SymbolIcon(symbol));
        button.Label(label);
    };

    makeButton(undoButton_, ctl::Symbol::Undo, L"Undo");
    undoButton_.Click([this](auto const&, auto const&) { store_.undo(); });
    makeButton(redoButton_, ctl::Symbol::Redo, L"Redo");
    redoButton_.Click([this](auto const&, auto const&) { store_.redo(); });
    toolbar_.PrimaryCommands().Append(undoButton_);
    toolbar_.PrimaryCommands().Append(redoButton_);
    toolbar_.PrimaryCommands().Append(ctl::AppBarSeparator());

    makeButton(saveButton_, ctl::Symbol::Save, L"Save");
    saveButton_.Click([this](auto const&, auto const&) { Save(); });
    ui::SetTip(saveButton_, "Save this document. Ctrl+S also saves.");
    toolbar_.PrimaryCommands().Append(saveButton_);

    makeButton(shareButton_, ctl::Symbol::Share, L"Share");
    ui::SetTip(shareButton_, "Export this recipe as a file, or share it.");
    ctl::MenuFlyout shareMenu;
    ctl::MenuFlyoutSubItem exportMenu;
    exportMenu.Text(L"Export As");
    ctl::MenuFlyoutSubItem shareAsMenu;
    shareAsMenu.Text(L"Share As");
    for (const auto& [title, format] : std::vector<std::pair<std::string, std::string>>{
             {"BeerXML…", "beerxml"}, {"Text…", "text"}, {"PDF…", "pdf"}, {"BeerJSON…", "beerjson"}}) {
        exportMenu.Items().Append(MenuItem(title, [this, format = format] { ExportRecipe(format, false); }));
        shareAsMenu.Items().Append(MenuItem(title, [this, format = format] { ExportRecipe(format, true); }));
    }
    shareMenu.Items().Append(exportMenu);
    shareMenu.Items().Append(shareAsMenu);
    shareButton_.Flyout(shareMenu);
    toolbar_.PrimaryCommands().Append(shareButton_);

    makeButton(duplicateButton_, ctl::Symbol::Copy, L"Duplicate");
    ui::SetTip(duplicateButton_, "Make a copy of this recipe, after it in the list");
    duplicateButton_.Click([this](auto const&, auto const&) {
        if (store_.selection()) store_.duplicateRecipe(*store_.selection());
    });
    toolbar_.PrimaryCommands().Append(duplicateButton_);

    makeButton(deleteButton_, ctl::Symbol::Delete, L"Delete");
    ui::SetTip(deleteButton_, "Delete this recipe. It moves to Recently Deleted.");
    deleteButton_.Click([this](auto const&, auto const&) {
        if (store_.selection()) store_.requestDelete(*store_.selection());
    });
    toolbar_.PrimaryCommands().Append(deleteButton_);
}

void MainWindow::BuildLayout() {
    ctl::RowDefinition menuRow;
    menuRow.Height(xaml::GridLength{1, xaml::GridUnitType::Auto});
    ctl::RowDefinition bodyRow;
    bodyRow.Height(xaml::GridLength{1, xaml::GridUnitType::Star});
    root_.RowDefinitions().Append(menuRow);
    root_.RowDefinitions().Append(bodyRow);
    root_.Children().Append(menuBar_);

    // The pane's header: the title, and the buttons that add a recipe or a folder.
    ctl::StackPanel header;
    header.Orientation(ctl::Orientation::Horizontal);
    header.Spacing(2);
    header.Margin(xaml::Thickness{12, 4, 8, 4});
    ctl::TextBlock title = ui::Text("Recipes", ui::Kind::strong);
    title.VerticalAlignment(xaml::VerticalAlignment::Center);
    title.Margin(xaml::Thickness{0, 0, 12, 0});
    header.Children().Append(title);
    header.Children().Append(ui::IconButton(ctl::Symbol::Add, "Add a recipe to the selected document, or start a new one",
                                            [this] { store_.newRecipe(); }));
    header.Children().Append(ui::IconButton(ctl::Symbol::Edit, "Start a recipe from a beer style, with the amounts worked out for you",
                                            [this] { dialogs::ShowWizard(host_); }));
    newFolderButton_ = ui::IconButton(ctl::Symbol::NewFolder, "Add a folder to group recipes", [this] {
        const auto doc = store_.document();
        if (!doc) return;
        dialogs::ShowNameDialog(host_, "New Folder", "Create", "Folder name", "",
                                [this, id = doc->id()](const std::string& name) { store_.createFolder(name, id); });
    });
    header.Children().Append(newFolderButton_);

    nav_.PaneDisplayMode(ctl::NavigationViewPaneDisplayMode::Left);
    nav_.IsBackButtonVisible(ctl::NavigationViewBackButtonVisible::Collapsed);
    nav_.IsSettingsVisible(true);
    nav_.OpenPaneLength(300);
    nav_.IsPaneToggleButtonVisible(false);
    nav_.PaneHeader(header);
    nav_.SelectionChanged([this](auto const&, ctl::NavigationViewSelectionChangedEventArgs const& args) {
        OnSelectionChanged(args);
    });
    // A folder that the user opens or closes is remembered, so a rebuild of the sidebar keeps it that way. These
    // events belong to the NavigationView, not its items, so the folder is found from the item's tag.
    const auto rememberFolder = [this](ctl::NavigationViewItemBase const& container, bool open) {
        if (rebuildingSidebar_ || !container) return;
        const std::vector<std::string> parts = SplitTag(TagOf(container));
        if (parts.size() < 3 || parts[0] != "f") return;
        const kettlelogic::app::ItemKey key{parts[1], static_cast<EntityId>(std::stoull(parts[2]))};
        if (open) {
            closedFolders_.erase(key);
        } else {
            closedFolders_.insert(key);
        }
    };
    nav_.Expanding([rememberFolder](auto const&, auto const& args) {
        rememberFolder(args.ExpandingItemContainer(), true);
    });
    nav_.Collapsed([rememberFolder](auto const&, auto const& args) {
        rememberFolder(args.CollapsedItemContainer(), false);
    });

    ctl::Grid body;
    ctl::RowDefinition toolbarRow;
    toolbarRow.Height(xaml::GridLength{1, xaml::GridUnitType::Auto});
    ctl::RowDefinition contentRow;
    contentRow.Height(xaml::GridLength{1, xaml::GridUnitType::Star});
    body.RowDefinitions().Append(toolbarRow);
    body.RowDefinitions().Append(contentRow);
    body.Children().Append(toolbar_);
    detail_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    detail_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
    detail_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    detail_.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    ctl::Grid::SetRow(detail_, 1);
    body.Children().Append(detail_);
    nav_.Content(body);

    ctl::Grid::SetRow(nav_, 1);
    root_.Children().Append(nav_);
}

// MARK: Refresh

void MainWindow::ScheduleRefresh() {
    if (!refreshTimer_) {
        return;
    }
    if (refreshPending_) {
        return;
    }
    refreshPending_ = true;
    refreshTimer_.Start();
}

void MainWindow::Refresh() {
    refreshPending_ = false;
    UpdateChrome();
    RebuildSidebar();
    UpdateDetail();
    if (startupDialogsShown_) {
        PresentPending();
    }
}

void MainWindow::UpdateChrome() {
    const auto doc = store_.document();
    const bool hasDocument = doc != nullptr;
    saveItem_.IsEnabled(hasDocument);
    saveAsItem_.IsEnabled(hasDocument);

    const bool canUndo = hasDocument && doc->canUndo();
    const bool canRedo = hasDocument && doc->canRedo();
    undoItem_.IsEnabled(canUndo);
    redoItem_.IsEnabled(canRedo);
    undoItem_.Text(H(canUndo && !doc->undoLabel().empty() ? "Undo " + doc->undoLabel() : "Undo"));
    redoItem_.Text(H(canRedo && !doc->redoLabel().empty() ? "Redo " + doc->redoLabel() : "Redo"));
    undoButton_.IsEnabled(canUndo);
    redoButton_.IsEnabled(canRedo);
    ui::SetTip(undoButton_, canUndo && !doc->undoLabel().empty() ? "Undo " + doc->undoLabel() : "Undo");
    ui::SetTip(redoButton_, canRedo && !doc->redoLabel().empty() ? "Redo " + doc->redoLabel() : "Redo");
    simpleModeItem_.IsChecked(store_.preferences.boolean(PrefKey::simpleMode));

    newFolderButton_.IsEnabled(hasDocument);
    const bool showsRecipe = hasDocument && store_.recipe(store_.selection()) != nullptr;
    const model::Recipe* open = store_.recipe(store_.selection());
    window_.Title(H(open != nullptr ? open->name + " \xE2\x80\x94 Kettle Logic" : std::string("Kettle Logic")));
    toolbar_.Visibility(showsRecipe ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (showsRecipe) {
        statusText_.Text(H(store_.saveStatus(*doc, store_.selection()->id)));
    }
}

ctl::NavigationViewItem MainWindow::RecipeItem(const RecipeDocument& doc, const model::Recipe& recipe) {
    const ItemKey key{doc.id(), recipe.id};
    const bool unsaved = doc.isUnsaved(recipe.id);

    ctl::NavigationViewItem item;
    ctl::TextBlock content = ui::Text(unsaved ? recipe.name + "*" : recipe.name);
    content.TextWrapping(xaml::TextWrapping::NoWrap);
    content.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    if (unsaved) {
        content.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
    }
    item.Content(content);
    item.Icon(ctl::SymbolIcon(ctl::Symbol::Document));
    item.Tag(box_value(H(RecipeTag(doc.id(), recipe.id))));

    // A recipe is dragged by its key, and dropped onto a folder or a document's name.
    item.CanDrag(true);
    item.DragStarting([key](auto const&, xaml::DragStartingEventArgs const& args) {
        args.Data().SetText(H(key.dragText()));
        args.Data().RequestedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move);
    });

    // A recipe in Recently Deleted comes back to the top level, or to any folder the user picks.
    const bool deleted = doc.isDeleted(recipe.id);
    ctl::MenuFlyout menu;
    menu.Items().Append(MenuItem("Rename Recipe…", [this, key, name = recipe.name] {
        dialogs::ShowNameDialog(host_, "Rename Recipe", "Rename", "Recipe name", name,
                                [this, key](const std::string& renamed) { store_.renameRecipe(key, renamed); });
    }));
    menu.Items().Append(ctl::MenuFlyoutSeparator());
    if (deleted) {
        menu.Items().Append(MenuItem("Restore", [this, key] { store_.moveRecipe(key, std::nullopt); }));
    }
    ctl::MenuFlyoutSubItem move;
    move.Text(L"Move to Folder");
    auto top = MenuItem("Top Level", [this, key] { store_.moveRecipe(key, std::nullopt); });
    top.IsEnabled(recipe.folder.has_value());
    move.Items().Append(top);
    for (const model::Folder& folder : doc.folders()) {
        if (folder.trash) continue;
        auto entry = MenuItem(folder.name, [this, key, folderId = folder.id] { store_.moveRecipe(key, folderId); });
        entry.IsEnabled(recipe.folder != folder.id);
        move.Items().Append(entry);
    }
    menu.Items().Append(move);
    menu.Items().Append(ctl::MenuFlyoutSeparator());
    if (!deleted) {
        menu.Items().Append(MenuItem("Duplicate Recipe", [this, key] { store_.duplicateRecipe(key); }));
    }
    // Deleting moves a recipe to Recently Deleted. Deleting one there removes it for good.
    menu.Items().Append(MenuItem(deleted ? "Delete Permanently" : "Delete Recipe", [this, key] { store_.requestDelete(key); }));
    item.ContextFlyout(menu);
    return item;
}

void MainWindow::MakeDropTarget(xaml::UIElement const& target, const std::string& documentId,
                                std::optional<EntityId> folder) {
    using winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation;
    using winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats;
    target.AllowDrop(true);
    target.DragOver([](auto const&, xaml::DragEventArgs const& args) {
        if (args.DataView().Contains(StandardDataFormats::Text())) {
            args.AcceptedOperation(DataPackageOperation::Move);
        }
    });
    target.Drop([this, documentId, folder](auto const&, xaml::DragEventArgs args) -> winrt::fire_and_forget {
        // The coroutine outlives the call, and the target can be rebuilt while it waits, so it keeps its own copies.
        MainWindow* const self = this;
        const std::string document = documentId;
        const std::optional<EntityId> target = folder;
        auto deferral = args.GetDeferral();
        const auto view = args.DataView();
        if (view.Contains(StandardDataFormats::Text())) {
            const winrt::hstring text = co_await view.GetTextAsync();
            // Recipes stay in their own document.
            if (const auto key = ItemKey::fromDragText(U(text)); key && key->document == document) {
                self->store_.moveRecipe(*key, target);
            }
        }
        deferral.Complete();
    });
}

void MainWindow::RebuildSidebar() {
    rebuildingSidebar_ = true;
    nav_.MenuItems().Clear();

    for (const auto& doc : store_.documents()) {
        // Dropping a recipe on the document's name moves it to the top level of that document.
        ctl::NavigationViewItemHeader header;
        header.Content(box_value(H(doc->displayName())));
        MakeDropTarget(header, doc->id(), std::nullopt);
        nav_.MenuItems().Append(header);

        // Recently Deleted is kept by the app, so it has no menu to rename or delete it, and it is listed last.
        const auto appendFolder = [&](const model::Folder& folder) {
            const ItemKey folderKey{doc->id(), folder.id};
            ctl::NavigationViewItem item;
            item.Content(box_value(H(folder.name)));
            item.Icon(ctl::SymbolIcon(folder.trash ? ctl::Symbol::Delete : ctl::Symbol::Folder));
            item.Tag(box_value(H(FolderTag(doc->id(), folder.id))));
            for (const model::Recipe& recipe : doc->recipes()) {
                if (recipe.folder == folder.id) {
                    item.MenuItems().Append(RecipeItem(*doc, recipe));
                }
            }
            item.IsExpanded(closedFolders_.count(folderKey) == 0);
            MakeDropTarget(item, doc->id(), folder.id);

            if (!folder.trash) {
                ctl::MenuFlyout menu;
                menu.Items().Append(MenuItem("Rename Folder…", [this, folderKey, name = folder.name] {
                    dialogs::ShowNameDialog(host_, "Rename Folder", "Rename", "Folder name", name,
                                            [this, folderKey](const std::string& renamed) { store_.renameFolder(folderKey, renamed); });
                }));
                menu.Items().Append(MenuItem("Delete Folder", [this, folderKey] { store_.deleteFolder(folderKey); }));
                item.ContextFlyout(menu);
            }
            nav_.MenuItems().Append(item);
        };

        for (const model::Folder& folder : doc->folders()) {
            if (!folder.trash) appendFolder(folder);
        }

        for (const model::Recipe& recipe : doc->recipes()) {
            if (!recipe.folder) {
                nav_.MenuItems().Append(RecipeItem(*doc, recipe));
            }
        }

        for (const model::Folder& folder : doc->folders()) {
            if (folder.trash) appendFolder(folder);
        }
    }

    nav_.MenuItems().Append(ctl::NavigationViewItemSeparator());
    ctl::NavigationViewItemHeader directory;
    directory.Content(box_value(L"Ingredient Directory"));
    nav_.MenuItems().Append(directory);
    const auto& pages = LibraryPage::all();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        ctl::NavigationViewItem item;
        item.Content(box_value(H(pages[i].title())));
        ctl::Symbol symbol = ctl::Symbol::Library;
        switch (pages[i].kind) {
            case LibraryPage::Kind::yours: symbol = ctl::Symbol::Contact; break;
            case LibraryPage::Kind::equipment: symbol = ctl::Symbol::Repair; break;
            case LibraryPage::Kind::ingredient:
                switch (pages[i].ingredient) {
                    case IngredientKind::fermentable: symbol = ctl::Symbol::Library; break;
                    case IngredientKind::hop: symbol = ctl::Symbol::Tag; break;
                    case IngredientKind::culture: symbol = ctl::Symbol::World; break;
                    case IngredientKind::water: symbol = ctl::Symbol::Globe; break;
                    case IngredientKind::miscellaneous: symbol = ctl::Symbol::Shop; break;
                }
                break;
        }
        item.Icon(ctl::SymbolIcon(symbol));
        item.Tag(box_value(H("l|" + std::to_string(i))));
        nav_.MenuItems().Append(item);
    }

    // The open recipe or page is the selected item.
    std::string wanted;
    if (store_.selection()) {
        wanted = RecipeTag(store_.selection()->document, store_.selection()->id);
    } else if (store_.library()) {
        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (pages[i] == *store_.library()) wanted = "l|" + std::to_string(i);
        }
    }
    nav_.SelectedItem(wanted.empty() ? nullptr : FindItem(nav_.MenuItems(), wanted));
    rebuildingSidebar_ = false;
}

void MainWindow::OnSelectionChanged(ctl::NavigationViewSelectionChangedEventArgs const& args) {
    if (rebuildingSidebar_) {
        return;
    }
    if (args.IsSettingsSelected()) {
        dialogs::ShowSettings(host_);
        ScheduleRefresh();  // puts the selection back on the open recipe
        return;
    }
    const auto container = args.SelectedItemContainer();
    if (!container) {
        return;
    }
    const std::vector<std::string> parts = SplitTag(TagOf(container));
    if (parts[0] == "r" && parts.size() == 3) {
        store_.select(ItemKey{parts[1], static_cast<EntityId>(std::stoull(parts[2]))});
    } else if (parts[0] == "l" && parts.size() == 2) {
        store_.showLibrary(LibraryPage::all()[static_cast<std::size_t>(std::stoul(parts[1]))]);
    } else {
        ScheduleRefresh();  // a folder only opens and closes
    }
}

void MainWindow::UpdateDetail() {
    std::string wanted;
    if (store_.library()) {
        wanted = "library:" + store_.library()->title();
    } else if (store_.selection() && store_.recipe(store_.selection())) {
        wanted = "recipe:" + store_.selection()->dragText();
    } else {
        wanted = "welcome";
    }

    if (wanted != viewKey_ || !view_) {
        viewKey_ = wanted;
        onTabShortcut_ = nullptr;
        if (store_.library()) {
            view_ = store_.library()->kind == LibraryPage::Kind::equipment ? MakeEquipmentDirectoryView(host_)
                                                                            : MakeDirectoryView(host_, *store_.library());
        } else if (wanted == "welcome") {
            view_ = MakeWelcomeView(host_);
        } else {
            view_ = MakeRecipeView(host_, *store_.selection());
            if (auto* recipeView = dynamic_cast<TabbedView*>(view_.get())) {
                onTabShortcut_ = [recipeView](int number) { recipeView->SelectTab(number); };
            }
        }
        detail_.Content(view_->Root());
    } else {
        view_->Refresh();
    }
}

void MainWindow::PresentPending() {
    const auto root = host_.xamlRoot();
    if (!root) {
        return;
    }
    if (store_.errorMessage) {
        const std::string message = *store_.errorMessage;
        store_.errorMessage.reset();
        ui::ShowMessage(root, "Kettle Logic", message);
    }
    if (store_.pendingDeletion) {
        const ItemKey key = *store_.pendingDeletion;
        const model::Recipe* recipe = store_.recipe(key);
        const std::string name = recipe ? recipe->name : "this recipe";
        const auto doc = store_.document(key);
        const bool forGood = doc && doc->isDeleted(key.id);
        // Showing it clears the request, so a refresh while the question is open does not ask again.
        store_.pendingDeletion.reset();
        ui::DialogOptions options;
        options.title = "Delete “" + name + "”?";
        options.content = ui::Text(forGood ? "It is removed for good." : "It moves to Recently Deleted, where you can restore it.");
        options.primaryText = forGood ? "Delete Permanently" : "Delete Recipe";
        options.primary = [this, key] {
            store_.deleteRecipe(key);
            return true;
        };
        ui::ShowDialog(root, options);
    }
}

// MARK: Files

void MainWindow::OpenFile() {
    const auto file = platform::OpenFile(host_.hwnd(), store_.recipeLibrary.folder(), RecipeTypes(),
                                         L"Open a BeerJSON recipe. Its sidecar file is opened with it.");
    if (file) {
        store_.open(*file);
    }
}

void MainWindow::Save() {
    if (store_.save() == DocumentStore::SaveResult::needsFile) {
        SaveAs();
    }
}

void MainWindow::SaveAs() {
    if (!store_.document()) {
        return;
    }
    std::filesystem::path start;
    try {
        start = store_.recipeLibrary.ensureFolder();
    } catch (const std::exception&) {
        start = store_.recipeLibrary.folder();
    }
    const auto file = platform::SaveFile(host_.hwnd(), start, H(store_.suggestedFileName() + ".json").c_str(),
                                         RecipeTypes(), L"json");
    if (file) {
        store_.saveAs(*file);
    }
}

void MainWindow::ExportRecipe(const std::string& format, bool share) {
    const auto doc = store_.document();
    const model::Recipe* recipe = store_.recipe(store_.selection());
    if (!doc || recipe == nullptr) {
        return;
    }

    struct Format {
        const char* name;
        const wchar_t* title;
        const char* extension;
        const char* engine;
    };
    static const Format formats[] = {
        {"beerxml", L"BeerXML", "xml", "beerxml"},
        {"text", L"Text", "txt", "text"},
        {"pdf", L"PDF", "pdf", ""},
        {"beerjson", L"BeerJSON", "json", "beerjson"},
    };
    const Format* chosen = nullptr;
    for (const Format& f : formats) {
        if (format == f.name) chosen = &f;
    }
    if (chosen == nullptr) {
        return;
    }

    // The recipe's name, made safe for a file name.
    const std::string stem = RecipeLibrary::safeFileName(recipe->name);
    const std::string fileName = stem + "." + chosen->extension;
    try {
        std::filesystem::path target;
        if (share) {
            target = std::filesystem::temp_directory_path() / std::filesystem::path(std::u8string(fileName.begin(), fileName.end()));
        } else {
            const std::wstring extension = platform::H(chosen->extension).c_str();
            const auto picked = platform::SaveFile(host_.hwnd(), store_.recipeLibrary.folder(), H(fileName).c_str(),
                                                   {{std::wstring(chosen->title), L"*." + extension}}, extension);
            if (!picked) {
                return;
            }
            target = *picked;
        }
        if (format == "pdf") {
            platform::WriteBinaryFile(target, doc->exportRecipePdf(recipe->id));
        } else {
            platform::WriteTextFile(target, doc->exportRecipe(recipe->id, chosen->engine));
        }
        telemetry::Record(kettlelogic::app::analytics::Event::exportRecipe,
                          {kettlelogic::app::analytics::Param::word("format", chosen->name)});
        if (share) {
            platform::ShareFile(host_.hwnd(), target, recipe->name);
        }
    } catch (const std::exception& e) {
        store_.errorMessage = e.what();
        ScheduleRefresh();
    } catch (winrt::hresult_error const& e) {
        store_.errorMessage = U(e.message());
        ScheduleRefresh();
    }
}

// MARK: Other windows

void MainWindow::OpenToolWindow(ToolKind kind, std::map<std::string, std::string> prefill) {
    telemetry::Record(kettlelogic::app::analytics::Event::featureUsed,
                      {kettlelogic::app::analytics::Param::word("feature", "tool_" + toolKindName(kind))});
    auto window = std::make_unique<ToolWindow>(host_, kind, std::move(prefill), [this](ToolWindow* closed) {
        toolWindows_.erase(std::remove_if(toolWindows_.begin(), toolWindows_.end(),
                                          [closed](const auto& w) { return w.get() == closed; }),
                           toolWindows_.end());
    });
    window->Activate();
    toolWindows_.push_back(std::move(window));
}

void MainWindow::OpenBrewDayWindow(const BrewDayPlan& plan) {
    auto window = std::make_unique<BrewDayWindow>(host_, plan, [this](BrewDayWindow* closed) {
        brewDayWindows_.erase(std::remove_if(brewDayWindows_.begin(), brewDayWindows_.end(),
                                             [closed](const auto& w) { return w.get() == closed; }),
                              brewDayWindows_.end());
    });
    window->Activate();
    brewDayWindows_.push_back(std::move(window));
}

void MainWindow::OpenHelpWindow() {
    if (helpWindow_) {
        helpWindow_->Activate();
        return;
    }
    helpWindow_ = std::make_unique<HelpWindow>(host_, [this](HelpWindow*) { helpWindow_.reset(); });
    helpWindow_->Activate();
}

}  // namespace kl
