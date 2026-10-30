#pragma once

#include <string>
#include <vector>

// The choices the add and edit dialogs offer for a recipe or an ingredient, by BeerJSON name. The first
// entry of a list is the default for a new ingredient. A recipe or ingredient read from a file keeps any
// value it has that is not listed, so a dialog adds the current value when it is missing.
namespace kettlelogic::catalog {

// The kinds of mash step a mash can have. The first, "Infusion", is the default for a new step. Saved plans
// keep these names as written, so a name here must not change.
const std::vector<std::string>& mashStepKinds();
// How a batch can be carbonated. The first, bottle conditioning with corn sugar, is the default for a new
// brew session. Saved plans keep these names as written, so a name here must not change.
const std::vector<std::string>& carbonationMethods();
// The currencies a recipe's currency picker offers. A recipe keeps any other code it was given.
const std::vector<std::string>& currencyCodes();
// The recipe types a user can pick, by BeerJSON name.
const std::vector<std::string>& recipeTypes();
// The fermentable types, by BeerJSON name.
const std::vector<std::string>& fermentableTypes();
// The units a fermentable amount can use.
const std::vector<std::string>& fermentableUnits();
// The miscellaneous ingredient types, by BeerJSON name.
const std::vector<std::string>& miscellaneousTypes();
// The units a miscellaneous amount can use.
const std::vector<std::string>& miscellaneousUnits();
// The hop forms, by BeerJSON name.
const std::vector<std::string>& hopForms();
// The hop uses, by BeerJSON timing name.
const std::vector<std::string>& hopUses();
// The units a hop amount can use.
const std::vector<std::string>& hopUnits();
// The units a hop addition time can use.
const std::vector<std::string>& hopTimeUnits();
// The culture types, by BeerJSON name.
const std::vector<std::string>& cultureTypes();
// The culture forms, by BeerJSON name.
const std::vector<std::string>& cultureForms();
// The units a culture amount can use.
const std::vector<std::string>& cultureUnits();

// The units a price can be per, for an ingredient measured in `amountUnit`. A price has to measure the same
// thing as the amount, so a price per kg suits an amount in lb, and a price per package suits a count. A unit
// that belongs to no family (not a BeerJSON amount unit) can only be priced per itself.
std::vector<std::string> priceUnits(const std::string& amountUnit);

}  // namespace kettlelogic::catalog
