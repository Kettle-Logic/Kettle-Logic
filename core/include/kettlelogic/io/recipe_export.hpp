#pragma once

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/model/model.hpp"

#include <string>

// Exporting one recipe as text: BeerXML 2.01 for BeerSmith and others, a plain-text
// summary, and BeerJSON. Each export is a string the caller writes to a file. See
// docs/formats/beerxml-import.md for the BeerXML mapping.
namespace kettlelogic::io {

// The recipe alone as a BeerJSON document. Folders, style, and the app's own
// planning data are not part of BeerJSON, so they are left out.
std::string exportBeerJson(const model::Recipe& recipe);

// The recipe as BeerXML 2.01, in the units BeerXML uses (kilograms, liters, degrees C, minutes).
// Ingredients BeerXML cannot represent are left out, and an XML comment at the top lists them.
std::string exportBeerXml(const model::Recipe& recipe, const calc::Results& results);

// A readable summary of the recipe, in the units it was entered in.
std::string exportText(const model::Recipe& recipe, const calc::Results& results);

}  // namespace kettlelogic::io
