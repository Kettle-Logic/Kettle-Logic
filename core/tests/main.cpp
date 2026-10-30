#include "check.hpp"

#include <iostream>

namespace kettlelogic::test {

void runUnitTests();
void runCommandTests();
void runJsonTests();
void runModelTests();
void runCalcTests();
void runFermentTests();
void runDrinkTests();
void runCApiTests();
void runIoTests();
void runStyleTests();
void runToolTests();
void runBrewWaterTests();
void runCatalogTests();
void runBrewSheetTests();
void runBeerXmlTests();
void runRecipePdfTests();

}  // namespace kettlelogic::test

int main() {
    using namespace kettlelogic::test;
    runUnitTests();
    runCommandTests();
    runJsonTests();
    runModelTests();
    runCalcTests();
    runFermentTests();
    runDrinkTests();
    runCApiTests();
    runIoTests();
    runStyleTests();
    runToolTests();
    runBrewWaterTests();
    runCatalogTests();
    runBrewSheetTests();
    runBeerXmlTests();
    runRecipePdfTests();

    if (failureCount() != 0) {
        std::cerr << failureCount() << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
