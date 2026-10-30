#include "kettlelogic/catalog/glossary.hpp"

namespace kettlelogic::catalog {

const std::string& explanation(Term term) {
    static const std::string texts[] = {
        "How much sugar is in the wort before fermentation. Higher means a stronger beer.",
        "How much sugar is left after fermentation. The gap between original and final gravity is what turned into alcohol.",
        "Alcohol by volume: the share of the finished beer that is alcohol.",
        "International Bitterness Units. Roughly 20 is mildly bitter, 40 is hoppy, 60 and above is a strong IPA.",
        "Color, on a scale where 2 is pale straw, 10 is amber, and 30 and above is black.",
        "How much of the sugar in your grain ends up in the kettle. 65–75% is typical for a first system. Measure yours and adjust.",
        "The share of sugar a yeast strain turns into alcohol. It sets the final gravity. The packet lists a typical figure.",
        "The bittering strength of the hops, printed on the package. Higher means more bitterness per gram.",
        "A second group of hop acids, printed beside the alpha acid. Fresh, they add little bitterness, but they oxidize as hops age. The bitterness estimate does not use them, so this is for your records.",
        "How much liquid boils off each hour. It concentrates the wort, so it changes the batch volume and gravity.",
        "The wort left behind with the sediment in the kettle and chiller. Plan on losing about a liter.",
        "The share of sugar that yeast turns into alcohol. Used to plan the sugar a drink needs.",
    };
    switch (term) {
        case Term::originalGravity: return texts[0];
        case Term::finalGravity: return texts[1];
        case Term::abv: return texts[2];
        case Term::ibu: return texts[3];
        case Term::srm: return texts[4];
        case Term::brewhouseEfficiency: return texts[5];
        case Term::attenuation: return texts[6];
        case Term::alphaAcid: return texts[7];
        case Term::betaAcid: return texts[8];
        case Term::evaporation: return texts[9];
        case Term::trubLoss: return texts[10];
        case Term::fermentationEfficiency: return texts[11];
    }
    return texts[0];
}

const std::vector<Term>& allTerms() {
    static const std::vector<Term> terms = {
        Term::originalGravity, Term::finalGravity, Term::abv, Term::ibu, Term::srm,
        Term::brewhouseEfficiency, Term::attenuation, Term::alphaAcid, Term::betaAcid, Term::evaporation,
        Term::trubLoss, Term::fermentationEfficiency,
    };
    return terms;
}

std::string termName(Term term) {
    switch (term) {
        case Term::originalGravity: return "originalGravity";
        case Term::finalGravity: return "finalGravity";
        case Term::abv: return "abv";
        case Term::ibu: return "ibu";
        case Term::srm: return "srm";
        case Term::brewhouseEfficiency: return "brewhouseEfficiency";
        case Term::attenuation: return "attenuation";
        case Term::alphaAcid: return "alphaAcid";
        case Term::betaAcid: return "betaAcid";
        case Term::evaporation: return "evaporation";
        case Term::trubLoss: return "trubLoss";
        case Term::fermentationEfficiency: return "fermentationEfficiency";
    }
    return "";
}

}  // namespace kettlelogic::catalog
