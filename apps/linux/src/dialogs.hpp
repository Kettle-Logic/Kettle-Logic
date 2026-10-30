#pragma once

#include "host.hpp"

#include "kettlelogic/app/ingredient_draft.hpp"
#include "kettlelogic/styles/style.hpp"

// The dialogs the app opens over its window. Each one is shown and forgets itself when it closes.
namespace kl::dialogs {

// An ingredient editor. `save` is called with the finished draft, and throws to refuse a value: the message is shown
// in the editor, which stays open.
struct IngredientRequest {
    std::string title;
    std::string saveTitle;
    kettlelogic::app::IngredientDraft seed;
    std::vector<kettlelogic::app::MenuEntry> menu;  // the Common ingredients menu
    bool showsQuantities = true;
    std::optional<std::string> currency;            // the currency a price is in, for an ingredient of a recipe
    std::function<void(const kettlelogic::app::IngredientDraft&)> save;
};
void ShowIngredientEditor(Host& host, IngredientRequest request);

// A brewing system editor, to make or change one. `replacing` is the custom system it changes, or nothing.
void ShowEquipmentEditor(Host& host, std::string title, kettlelogic::app::EquipmentProfile profile,
                         std::optional<std::string> replacing);

// A user style's editor. `save` is called with the finished style, and throws to refuse it: the message is shown in the
// editor, which stays open. `remove` is set for a style that already exists, and gives the editor its Delete button.
struct StyleRequest {
    std::string title;
    kettlelogic::styles::UserStyle seed;  // the id is empty for a new style
    std::function<void(const kettlelogic::styles::UserStyle&)> save;
    std::function<void()> remove;
};
void ShowStyleEditor(Host& host, StyleRequest request);

// The new-recipe wizard: pick a style, set the batch and the brewing system, review, and create.
void ShowWizard(Host& host);

// The settings, which take effect as they are changed.
void ShowSettings(Host& host);

// Asks on the first launch which interface the user wants.
void ShowInterfaceChoice(Host& host);

// Asks for a name, such as a new folder's, or the new name of a folder or recipe. `placeholder` says what the field is for.
void ShowNameDialog(Host& host, std::string title, std::string actionTitle, std::string placeholder, std::string current,
                    std::function<void(const std::string&)> accept);

// A calculator in a dialog over a recipe, with the recipe's figures already in its fields.
void ShowToolDialog(Host& host, kettlelogic::app::ToolKind kind, std::map<std::string, std::string> prefill);

}  // namespace kl::dialogs
