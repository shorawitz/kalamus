# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

# JKQTMathText (LGPL-2.1-or-later) renders LaTeX math with QPainter. It lives in the
# JKQtPlotter repository. We download that source at a pinned commit and compile only
# the two parts we need into a private static library, without running JKQtPlotter's
# own CMake (which would also install its libraries and headers next to Kalamus).
# The bundled XITS fonts are under the SIL Open Font License 1.1.

include(FetchContent)
FetchContent_Declare(jkqtplotter
    GIT_REPOSITORY https://github.com/jkriege2/JKQtPlotter.git
    GIT_TAG 9e9f0af8b140192e3de7ff866a0013e7da5e1e93 # master, 2026-10-23
    SOURCE_SUBDIR do-not-add # download only
)
FetchContent_MakeAvailable(jkqtplotter)

find_package(Qt6 REQUIRED COMPONENTS Xml)

set(_jk ${jkqtplotter_SOURCE_DIR}/lib)
file(GLOB_RECURSE _jk_sources CONFIGURE_DEPENDS
    ${_jk}/jkqtcommon/*.cpp
    ${_jk}/jkqtmathtext/*.cpp
)
add_library(jkqtmathtext STATIC ${_jk_sources} ${_jk}/jkqtmathtext/resources/xits.qrc)
set_target_properties(jkqtmathtext PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    CXX_STANDARD 20
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(jkqtmathtext SYSTEM PUBLIC ${_jk})
target_compile_definitions(jkqtmathtext PUBLIC JKQTMATHTEXT_COMPILED_WITH_XITS)
target_compile_options(jkqtmathtext PRIVATE -w) # third-party code: keep our build output clean
target_link_libraries(jkqtmathtext PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml)

install(FILES ${_jk}/jkqtmathtext/resources/xits/OFL.txt
        DESTINATION share/doc/kalamus RENAME XITS-OFL.txt)
install(FILES ${jkqtplotter_SOURCE_DIR}/LICENSE
        DESTINATION share/doc/kalamus RENAME JKQtPlotter-LICENSE.txt)
