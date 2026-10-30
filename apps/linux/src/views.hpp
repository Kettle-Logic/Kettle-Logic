#pragma once

#include "host.hpp"

#include <memory>

namespace kl {

// A recipe screen has tabs, and the window's Ctrl+1 to Ctrl+9 go to them.
class TabbedView : public View {
public:
    // Goes to the nth tab, counting from 1, or to the nth group when the tabs are grouped.
    virtual void SelectTab(int number) = 0;
};

// The screens of the detail area.
std::unique_ptr<View> MakeRecipeView(Host& host, const kettlelogic::app::ItemKey& key);
std::unique_ptr<View> MakeDirectoryView(Host& host, const kettlelogic::app::LibraryPage& page);
std::unique_ptr<View> MakeEquipmentDirectoryView(Host& host);
std::unique_ptr<View> MakeWelcomeView(Host& host);

// A calculator or converter. The same screen fills a tool window and a dialog over a recipe.
class ToolView : public View {
public:
    ToolView(kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill,
             kettlelogic::app::WaterProfileLibrary& profiles, bool inDialog = false);
    ~ToolView() override;
    GtkWidget* Root() override { return page_.Root(); }
    void Refresh() override;

private:
    void Rebuild();

    kettlelogic::app::ToolKind kind_;
    kettlelogic::app::ToolSpec spec_;
    kettlelogic::app::WaterProfileLibrary& profiles_;
    std::map<std::string, std::string> values_;
    std::string saveName_;
    std::string saveMessage_;
    ui::Page page_;
    ui::FocusKeeper focus_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// The tabs of a recipe that plan its yeast, water, mash, and brew sessions, and its brew-day procedure. Each
// builds its sections into a form, as the macOS app's tabs do.
struct RecipeContext;
void BuildStarterTab(RecipeContext& context, ui::Form& form);
void BuildWaterTab(RecipeContext& context, ui::Form& form);
void BuildMashTab(RecipeContext& context, ui::Form& form);
void BuildJournalTab(RecipeContext& context, ui::Form& form);
void BuildProcedureTab(RecipeContext& context, ui::Form& form);

}  // namespace kl
