# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

# Everything except the window and main(), so the tests can use it too.
add_library(kalamuscore STATIC
    exporter.cpp exporter.h
    editor/markdowneditor.cpp editor/markdowneditor_tables.cpp editor/markdowneditor.h
    editor/markdownhighlighter.cpp editor/markdownhighlighter.h
    editor/markdowntable.cpp editor/markdowntable.h
)
target_include_directories(kalamuscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(kalamuscore PUBLIC
    Qt6::Widgets
    Qt6::PrintSupport
    KF6::I18n
    PkgConfig::CMARK_GFM
    ${CMARK_GFM_EXTENSIONS_LIBRARY}
)

add_executable(kalamus
    main.cpp
    mainwindow.cpp mainwindow.h
    filetree.cpp filetree.h
    kalamus.qrc
)
target_compile_definitions(kalamus PRIVATE KALAMUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(kalamus PRIVATE
    kalamuscore
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::XmlGui
)

install(TARGETS kalamus ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
