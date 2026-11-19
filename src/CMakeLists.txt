# SPDX-FileCopyrightText: 2026 David Christensen
# SPDX-License-Identifier: GPL-3.0-or-later

# Everything except the window and main(), so the tests can use it too.
add_library(kalamuscore STATIC
    exporter.cpp exporter.h
    mathrenderer.cpp mathrenderer.h
    editor/markdowneditor.cpp editor/markdowneditor_tables.cpp editor/markdowneditor_math.cpp editor/markdowneditor_images.cpp editor/markdowneditor_spelling.cpp editor/markdowneditor.h
    editor/markdownhighlighter.cpp editor/markdownhighlighter.h
    editor/markdowntable.cpp editor/markdowntable.h
    editor/tablelayout.cpp editor/tablelayout.h
    editor/imagecache.cpp editor/imagecache.h
    editor/codehighlighter.cpp editor/codehighlighter.h
    editor/spellchecker.cpp editor/spellchecker.h
    editor/outline.cpp editor/outline.h
    outlinepanel.cpp outlinepanel.h
    findbar.cpp findbar.h
)
# JKQTMathText's header has const char* default arguments for QString parameters.
set_source_files_properties(mathrenderer.cpp PROPERTIES COMPILE_OPTIONS "-UQT_NO_CAST_FROM_ASCII")
target_include_directories(kalamuscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(kalamuscore PUBLIC
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network
    KF6::I18n
    KF6::WidgetsAddons
    KF6::SyntaxHighlighting
    KF6::SonnetCore
    PkgConfig::CMARK_GFM
    ${CMARK_GFM_EXTENSIONS_LIBRARY}
    jkqtmathtext
)

# The window, its panels and settings: shared by the app and its tests.
add_library(kalamusui STATIC
    mainwindow.cpp mainwindow.h
    filetree.cpp filetree.h
    settingsdialog.cpp settingsdialog.h settingsdefaults.h
    recentdocuments.cpp recentdocuments.h
)
kconfig_add_kcfg_files(kalamusui kalamussettings.kcfgc)
target_include_directories(kalamusui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kalamusui PUBLIC
    kalamuscore
    Qt6::DBus
    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::XmlGui
)

add_executable(kalamus
    main.cpp
    kalamus.qrc
)
target_compile_definitions(kalamus PRIVATE KALAMUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(kalamus PRIVATE kalamusui)

install(FILES kalamus.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
install(TARGETS kalamus ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
