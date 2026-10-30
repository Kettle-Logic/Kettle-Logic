#pragma once

#include "host.hpp"

#include "kettlelogic/app/ingredient_draft.hpp"

#include <cstddef>
#include <functional>
#include <vector>

// The ingredient picker, for a list too long for a menu: search, sort, filters, and each ingredient's key figures under
// its name, so the brewer can compare them without opening the directory.
namespace kl::dialogs {

// A list with at least this many entries opens the picker. A shorter one stays a menu.
inline constexpr std::size_t kIngredientPickerThreshold = 15;

// Opens the picker in a dialog over `parent`. `pick` is called with the entry the brewer chooses, and then the dialog closes.
void ShowIngredientPicker(GtkWidget* parent, kettlelogic::app::IngredientKind kind,
                          std::vector<kettlelogic::app::MenuEntry> entries,
                          std::function<void(const kettlelogic::app::MenuEntry&)> pick);

}  // namespace kl::dialogs
