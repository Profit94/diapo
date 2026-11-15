cmake_minimum_required(VERSION 3.21)
project(diapo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 DBus Concurrent Test)

# HEIC and AVIF, JPEG XL, and camera RAW come from the system's own
# libraries; everything else Qt reads through its image plugins.
find_package(PkgConfig REQUIRED)
pkg_check_modules(codecs REQUIRED IMPORTED_TARGET libheif libjxl libjxl_threads libraw)

# Everything but main.cpp, shared by the app and the tests, so each source
# compiles once and a new file in src/ needs no line here.
file(GLOB diapo_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM diapo_sources ${CMAKE_SOURCE_DIR}/src/main.cpp)
add_library(diapo_core STATIC ${diapo_sources})
target_include_directories(diapo_core PUBLIC src)
target_link_libraries(diapo_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Concurrent
  PkgConfig::codecs)
target_compile_options(diapo_core PUBLIC -Wall -Wextra)

add_executable(diapo src/main.cpp src/resources.qrc)
target_link_libraries(diapo PRIVATE diapo_core)

# The tests load the real Main.qml, so they carry the resources too.
enable_testing()
add_executable(diapo_tests tests/diapo_tests.cpp src/resources.qrc)
target_link_libraries(diapo_tests PRIVATE diapo_core Qt6::Test)
add_test(NAME diapo_tests COMMAND diapo_tests)
set_tests_properties(diapo_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# `cmake --install build --prefix ~/.local` puts the app in the launcher
# before it is packaged; a package installs the same files under /usr.
include(GNUInstallDirs)
install(TARGETS diapo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES share/diapo.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES share/diapo.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/diapo)
