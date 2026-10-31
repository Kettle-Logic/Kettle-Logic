cmake_minimum_required(VERSION 3.25)
project(KettleLogicCore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost is used header-only (multiprecision). No compiled Boost libraries.
# If BOOST_ROOT holds a boost\ folder of headers (the official zip, or vcpkg's include folder), use it directly. A
# find_package search can pick up another install, such as MSYS2's MinGW Boost on PATH, whose headers break MSVC builds.
if(NOT BOOST_ROOT AND DEFINED ENV{BOOST_ROOT})
    set(BOOST_ROOT "$ENV{BOOST_ROOT}")
endif()
if(BOOST_ROOT AND EXISTS "${BOOST_ROOT}/boost/version.hpp")
    file(STRINGS "${BOOST_ROOT}/boost/version.hpp" boost_version_line REGEX "^#define BOOST_VERSION [0-9]+")
    string(REGEX REPLACE "^#define BOOST_VERSION ([0-9]+).*" "\\1" boost_version "${boost_version_line}")
    if(boost_version LESS 108000)
        message(FATAL_ERROR "Boost 1.80 or newer is required; ${BOOST_ROOT} has BOOST_VERSION ${boost_version}.")
    endif()
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_ROOT}"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${BOOST_ROOT}")
    message(STATUS "Boost headers: ${BOOST_ROOT}")
else()
    find_package(Boost 1.80 REQUIRED)
endif()

add_library(kettlelogic_core
    src/json.cpp
    src/measure.cpp
    src/quantity.cpp
    src/rational.cpp
    src/model/beerjson_read.cpp
    src/model/beerjson_write.cpp
    src/model/commands.cpp
    src/model/document.cpp
    src/model/sidecar.cpp
    src/uuid.cpp
    src/crypto/sha256.cpp
    src/calc/recipe.cpp
    src/calc/style.cpp
    src/styles/style.cpp
    src/calc/starter.cpp
    src/calc/brew_plan.cpp
    src/calc/journal.cpp
    src/calc/procedure.cpp
    src/calc/water.cpp
    src/calc/fermented.cpp
    src/calc/cider.cpp
    src/calc/mead.cpp
    src/calc/wine.cpp
    src/calc/drink.cpp
    src/io/document_file.cpp
    src/io/recipe_export.cpp
    src/io/beerxml_import.cpp
    src/io/brew_sheet_pdf.cpp
    src/io/pdf_writer.cpp
    src/io/recipe_pdf.cpp
    src/calc/tools.cpp
    src/calc/brew_water.cpp
    src/calc/brew_sheet.cpp
    src/catalog/equipment.cpp
    src/catalog/fermentation_presets.cpp
    src/catalog/glossary.cpp
    src/catalog/ingredient_query.cpp
    src/catalog/ingredients.cpp
    src/catalog/options.cpp
    src/catalog/recipe_templates.cpp
    src/catalog/tool_specs.cpp
)
target_include_directories(kettlelogic_core PUBLIC include)
target_link_libraries(kettlelogic_core PUBLIC Boost::headers)
target_compile_features(kettlelogic_core PUBLIC cxx_std_20)
# The engine library, kettlelogic_engine below, links this in, and it is a shared library.
set_target_properties(kettlelogic_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The engine's version is kettlelogic/version.hpp. Keep PROJECT_VERSION above equal to it.

# The C facade, for front ends that cannot link C++ directly (SwiftUI).
add_library(kettlelogic_c STATIC src/c/kettlelogic_c.cpp src/c/builtin_data.cpp)
target_link_libraries(kettlelogic_c PUBLIC kettlelogic_core)

# The engine as a shared library, for the macOS app, which loads it at launch so the engine can be updated apart from
# the app (see docs/updates.md). It has the same facade, and exports it by name.
add_library(kettlelogic_engine SHARED src/c/kettlelogic_c.cpp src/c/builtin_data.cpp)
target_link_libraries(kettlelogic_engine PUBLIC kettlelogic_core)

if(MSVC)
    target_compile_options(kettlelogic_core PRIVATE /W4 /WX)
    target_compile_options(kettlelogic_c PRIVATE /W4 /WX)
    target_compile_options(kettlelogic_engine PRIVATE /W4 /WX)
else()
    target_compile_options(kettlelogic_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kettlelogic_c PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kettlelogic_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()

add_executable(kettlelogic_tests
    tests/main.cpp
    tests/test_calc.cpp
    tests/test_ferment.cpp
    tests/test_drink.cpp
    tests/test_io.cpp
    tests/test_commands.cpp
    tests/test_json.cpp
    tests/test_model.cpp
    tests/test_units.cpp
    tests/test_c_api.cpp
    tests/test_style.cpp
    tests/test_tools.cpp
    tests/test_brew_water.cpp
    tests/test_catalog.cpp
    tests/test_brew_sheet.cpp
    tests/test_beerxml.cpp
    tests/test_recipe_pdf.cpp
)
target_link_libraries(kettlelogic_tests PRIVATE kettlelogic_c)
target_compile_definitions(kettlelogic_tests PRIVATE
    KETTLELOGIC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    KETTLELOGIC_PROJECT_VERSION="${PROJECT_VERSION}")
add_test(NAME kettlelogic_core COMMAND kettlelogic_tests)

# The macOS app's loader, the engine's shared library loaded the way the app loads it. The generated loader is
# checked against the header below, and the test loads the built engine and calls it.
# It uses dlopen, so it is not built on Windows, where the app loads the engine DLL its own way.
if(NOT WIN32)
    add_executable(kettlelogic_loader_test tests/loader/test_engine_loader.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../apps/macos/Sources/CKettleLogic/engine_loader.c)
    target_include_directories(kettlelogic_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../apps/macos/Sources/CKettleLogic/include)
    target_compile_definitions(kettlelogic_loader_test PRIVATE
        KETTLELOGIC_ENGINE_PATH="$<TARGET_FILE:kettlelogic_engine>"
        KETTLELOGIC_PROJECT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(kettlelogic_loader_test PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(kettlelogic_loader_test kettlelogic_engine)
    add_test(NAME kettlelogic_engine_loader COMMAND kettlelogic_loader_test)
endif()

# Checks the writer's output against the vendored BeerJSON schema.
# Needs Python 3 with the `jsonschema` package.
add_executable(kettlelogic_roundtrip tools/roundtrip.cpp)
target_link_libraries(kettlelogic_roundtrip PRIVATE kettlelogic_core)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # Fails if schema.hpp was not regenerated after a schema change.
    add_test(NAME schema_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_schema.py --check)

    # Fails if the macOS app's engine loader was not regenerated after a facade change.
    add_test(NAME engine_loader_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_engine_loader.py --check)

    add_test(NAME schema_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/check_writer_output.py
            $<TARGET_FILE:kettlelogic_roundtrip>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_recipe.json)
    # Input with undefined properties: the output must still validate.
    add_test(NAME schema_conformance_unknown_keys
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/check_writer_output.py
            $<TARGET_FILE:kettlelogic_roundtrip>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/unknown_keys.json)
    # A cider: juice by volume, a sulfite, and the drink recipe type.
    add_test(NAME schema_conformance_cider
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/check_writer_output.py
            $<TARGET_FILE:kettlelogic_roundtrip>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/cider_recipe.json)
    # A BeerXML file: the import, converted to BeerJSON, must validate.
    add_test(NAME schema_conformance_beerxml
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/check_writer_output.py
            $<TARGET_FILE:kettlelogic_roundtrip>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_beerxml.xml)
endif()
