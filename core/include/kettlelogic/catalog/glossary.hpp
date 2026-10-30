#pragma once

#include <string>
#include <vector>

namespace kettlelogic::catalog {

// A brewing term, with a plain-language explanation for someone new to it.
enum class Term {
    originalGravity, finalGravity, abv, ibu, srm,
    brewhouseEfficiency, attenuation, alphaAcid, betaAcid, evaporation, trubLoss, fermentationEfficiency,
};

const std::string& explanation(Term term);

// Every term, in declaration order.
const std::vector<Term>& allTerms();

// The stable name of a term, such as "originalGravity".
std::string termName(Term term);

}  // namespace kettlelogic::catalog
