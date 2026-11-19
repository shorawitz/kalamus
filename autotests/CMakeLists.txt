# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

include(ECMAddTests)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

ecm_add_tests(
    codehighlightingtest.cpp
    exportertest.cpp
    findreplacetest.cpp
    imageeditingtest.cpp
    markdowntabletest.cpp
    mathrenderertest.cpp
    matheditingtest.cpp
    outlinetest.cpp
    paragraphstylingtest.cpp
    spellcheckingtest.cpp
    tableeditingtest.cpp
    LINK_LIBRARIES kalamuscore Qt6::Test
)

# The main window, built from its sources (the app itself has no library).
ecm_add_test(mainwindowtest.cpp
    ${CMAKE_SOURCE_DIR}/src/kalamus.qrc
    TEST_NAME mainwindowtest
    LINK_LIBRARIES kalamusui Qt6::Test
)
