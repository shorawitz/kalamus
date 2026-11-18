# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(kalamus)

target_sources(kalamus PRIVATE
    main.cpp
    mainwindow.cpp mainwindow.h
    filetree.cpp filetree.h
    exporter.cpp exporter.h
    editor/markdowneditor.cpp editor/markdowneditor.h
    editor/markdownhighlighter.cpp editor/markdownhighlighter.h
    kalamus.qrc
)

target_compile_definitions(kalamus PRIVATE KALAMUS_VERSION="${PROJECT_VERSION}")

target_link_libraries(kalamus PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
    KF6::XmlGui
    PkgConfig::CMARK_GFM
    ${CMARK_GFM_EXTENSIONS_LIBRARY}
)

install(TARGETS kalamus ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
