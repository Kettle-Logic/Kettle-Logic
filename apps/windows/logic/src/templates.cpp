#include "kettlelogic/app/templates.hpp"

#include "kettlelogic/app/text.hpp"

namespace kettlelogic::app {

std::vector<std::string> tipsIn(const RecipeTemplate& recipe, UnitSystem system) {
    const Units units{system};
    std::vector<std::string> out;
    for (std::string text : recipe.tips) {
        for (;;) {
            const std::size_t open = text.find('{');
            const std::size_t close = open == std::string::npos ? open : text.find('}', open);
            if (close == std::string::npos) {
                break;
            }
            const auto celsius = parseNumber(text.substr(open + 1, close - open - 1));
            if (!celsius) {
                break;
            }
            text.replace(open, close - open + 1, units.temperatureText(*celsius));
        }
        out.push_back(std::move(text));
    }
    return out;
}

}  // namespace kettlelogic::app
