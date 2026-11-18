# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

include(ECMAddTests)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

ecm_add_tests(
    exportertest.cpp
    markdowntabletest.cpp
    tableeditingtest.cpp
    LINK_LIBRARIES kalamuscore Qt6::Test
)
