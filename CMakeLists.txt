# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)
project(kalamus VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets PrintSupport Network)

# Third-party code first, so KDE's strict compiler settings below don't apply to it.
add_subdirectory(3rdparty)

include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    ConfigWidgets
    CoreAddons
    I18n
    SyntaxHighlighting
    WidgetsAddons
    XmlGui
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMARK_GFM REQUIRED IMPORTED_TARGET libcmark-gfm)
# The GFM extensions (tables, strikethrough, task lists, autolinks) ship as a separate library.
find_library(CMARK_GFM_EXTENSIONS_LIBRARY NAMES cmark-gfm-extensions REQUIRED)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

install(FILES io.github.shorawitz.Kalamus.desktop DESTINATION ${KDE_INSTALL_APPDIR})

include(ECMInstallIcons)
ecm_install_icons(ICONS icons/sc-apps-io.github.shorawitz.Kalamus.svg DESTINATION ${KDE_INSTALL_ICONDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
