# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

include(ECMAddTests)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

ecm_add_test(exportertest.cpp ../src/exporter.cpp
    TEST_NAME exportertest
    LINK_LIBRARIES Qt6::Test Qt6::Widgets Qt6::PrintSupport KF6::I18n PkgConfig::CMARK_GFM ${CMARK_GFM_EXTENSIONS_LIBRARY}
)
target_include_directories(exportertest PRIVATE ${CMAKE_SOURCE_DIR}/src)
