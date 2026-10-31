cmake_minimum_required(VERSION 3.25)
project(KettleLogicAppLogic VERSION 0.1.0 LANGUAGES CXX)

# The portable half of the Windows app: the engine adapter, the app's data and settings, and its state.
# It has no UI and no platform API beyond the C++ standard library, so it builds and is tested on any
# platform. The WinUI 3 project (KettleLogic/KettleLogic.vcxproj) links the library this builds.
#
#   cmake -S apps/windows -B build/windows-logic
#   cmake --build build/windows-logic
#   ctest --test-dir build/windows-logic --output-on-failure

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine, built from this repository.
if(NOT TARGET kettlelogic_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core ${CMAKE_BINARY_DIR}/core)
endif()

add_library(kettlelogic_app_logic STATIC
    logic/src/app_updates.cpp
    logic/src/brew_day.cpp
    logic/src/brew_sheet.cpp
    logic/src/document_store.cpp
    logic/src/engine.cpp
    logic/src/equipment.cpp
    logic/src/help_markdown.cpp
    logic/src/ingredient_draft.cpp
    logic/src/ingredient_search.cpp
    logic/src/planning.cpp
    logic/src/preferences.cpp
    logic/src/readings_csv.cpp
    logic/src/recipe_builder.cpp
    logic/src/recipe_library.cpp
    logic/src/recipe_text.cpp
    logic/src/templates.cpp
    logic/src/text.cpp
    logic/src/water_profiles.cpp
)
target_include_directories(kettlelogic_app_logic PUBLIC logic/include)
target_link_libraries(kettlelogic_app_logic PUBLIC kettlelogic_core)
# A build can check another update list, such as a local one for testing: -DKETTLELOGIC_UPDATE_MANIFEST_URL=...
if(KETTLELOGIC_UPDATE_MANIFEST_URL)
    target_compile_definitions(kettlelogic_app_logic PUBLIC KETTLELOGIC_UPDATE_MANIFEST_URL="${KETTLELOGIC_UPDATE_MANIFEST_URL}")
endif()

if(MSVC)
    # The sources hold UTF-8 text, such as "Côte des Blancs".
    target_compile_options(kettlelogic_app_logic PUBLIC /utf-8)
    target_compile_options(kettlelogic_app_logic PRIVATE /W4 /WX)
    # The sources use standard sscanf, which MSVC flags as deprecated and /WX would turn into an error.
    target_compile_definitions(kettlelogic_app_logic PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(kettlelogic_app_logic PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_executable(kettlelogic_app_tests logic/tests/main.cpp)
target_link_libraries(kettlelogic_app_tests PRIVATE kettlelogic_app_logic)
target_compile_definitions(kettlelogic_app_tests PRIVATE
    KETTLELOGIC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/data"
    KETTLELOGIC_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME kettlelogic_app_logic COMMAND kettlelogic_app_tests)
