#pragma once

#include "kettlelogic/catalog/ingredients.hpp"

// The built-in ingredients that prefill the add dialogs. The data lives in the engine
// (`kettlelogic/catalog/ingredients.hpp`), so every front end offers the same entries. These names
// keep the app's code reading `Catalog::hops()`.
namespace kettlelogic::app {

using catalog::CultureTemplate;
using catalog::FermentableTemplate;
using catalog::HopTemplate;
using catalog::MiscellaneousTemplate;
using catalog::WaterTemplate;

namespace Catalog {
using catalog::cultures;
using catalog::fermentables;
using catalog::hops;
using catalog::miscellaneous;
using catalog::waters;
}  // namespace Catalog

}  // namespace kettlelogic::app
