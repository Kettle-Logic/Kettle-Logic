cmake_minimum_required(VERSION 3.25)
project(KettleLogicGtk VERSION 0.1.0 LANGUAGES CXX)

# The GTK4 and libadwaita front end. It has the same features as the macOS and Windows apps.
#
#   cmake -S apps/linux -B build/linux
#   cmake --build build/linux
#   build/linux/kettlelogic-gtk [Recipe.json ...]
#
# The app's state, settings, planning, and file handling are the portable C++ in apps/windows/logic, which has no UI
# and no platform API, so this front end builds only the GTK screens on top of it.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GTK 4.12 (for GtkListView scrolling and GtkFileDialog) and libadwaita 1.5 (for AdwDialog) come from the system package manager. See README.md.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1>=1.5)
# libsoup fetches the update list. The GNOME runtime provides it in Flatpak.
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0)

# The engine and the portable app logic. The logic's own tests are not built here: build apps/windows to run them.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../windows windows-logic EXCLUDE_FROM_ALL)

# Everything but main() is a library, so the smoke test drives the same code the app runs.
add_library(kettlelogic_gtk_app STATIC
    src/platform.cpp
    src/ui.cpp
    src/markdown_view.cpp
    src/main_window.cpp
    src/recipe_view.cpp
    src/planning_tabs.cpp
    src/journal_tab.cpp
    src/procedure_tab.cpp
    src/dialogs.cpp
    src/ingredient_picker.cpp
    src/directory.cpp
    src/tool_view.cpp
    src/aux_windows.cpp
    src/app_update_check.cpp
    src/telemetry.cpp
)
target_include_directories(kettlelogic_gtk_app PUBLIC src)
target_link_libraries(kettlelogic_gtk_app PUBLIC kettlelogic_app_logic PkgConfig::GTK4 PkgConfig::ADW PkgConfig::SOUP)
if(MSVC)
    target_compile_options(kettlelogic_gtk_app PRIVATE /W4)
else()
    target_compile_options(kettlelogic_gtk_app PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated-declarations)
endif()

add_executable(kettlelogic-gtk src/main.cpp)
target_link_libraries(kettlelogic-gtk PRIVATE kettlelogic_gtk_app)

# Opens the app's windows and drives its screens, so a crash or a GTK critical shows up. It needs a display: on a
# build machine without one, run it under xvfb-run. Set KETTLELOGIC_SMOKE_SHOTS to a folder to also save PNGs.
enable_testing()
add_executable(kettlelogic-gtk-smoke EXCLUDE_FROM_ALL tests/smoke.cpp)
target_link_libraries(kettlelogic-gtk-smoke PRIVATE kettlelogic_gtk_app)
add_test(NAME kettlelogic_gtk_smoke COMMAND kettlelogic-gtk-smoke)

# The user guide sits beside the executable in the build folder, and in share/kettlelogic when installed.
add_custom_command(TARGET kettlelogic-gtk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Help $<TARGET_FILE_DIR:kettlelogic-gtk>/Help)
add_custom_command(TARGET kettlelogic-gtk-smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Help $<TARGET_FILE_DIR:kettlelogic-gtk-smoke>/Help)

include(GNUInstallDirs)
install(TARGETS kettlelogic-gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY Help DESTINATION ${CMAKE_INSTALL_DATADIR}/kettlelogic)
install(FILES data/org.kettlelogic.KettleLogic.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.kettlelogic.KettleLogic.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
install(FILES data/org.kettlelogic.KettleLogic.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
