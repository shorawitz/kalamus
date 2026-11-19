// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "findbar.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPalette>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>

#include <KAboutData>
#include <KLocalizedString>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kalamus");

    KAboutData about(u"kalamus"_s,
                     i18n("Kalamus"),
                     QStringLiteral(KALAMUS_VERSION),
                     i18n("A Markdown editor for KDE with live formatting"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 David Christensen"));
    about.setDesktopFileName(u"io.github.shorawitz.Kalamus"_s);
    KAboutData::setApplicationData(about);
    // The installed theme icon wins; the built-in copy covers running from the build folder.
    QApplication::setWindowIcon(QIcon::fromTheme(u"io.github.shorawitz.Kalamus"_s, QIcon(u":/icons/kalamus.svg"_s)));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(u"path"_s, i18n("Markdown file or folder to open"), u"[path]"_s);
    // Developer aid: render the window off-screen and save it as a PNG, then quit.
    const QCommandLineOption grabOption(u"grab"_s, i18n("Save a screenshot of the window to <file> and quit."), u"file"_s);
    parser.addOption(grabOption);
    parser.process(app);
    about.processCommandLine(&parser);

    if (parser.isSet(grabOption) && qEnvironmentVariableIsSet("KALAMUS_GRAB_DARK")) {
        // Developer aid: Breeze Dark colours for off-screen screenshots.
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(0x20, 0x23, 0x26));
        dark.setColor(QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
        dark.setColor(QPalette::Base, QColor(0x14, 0x16, 0x18));
        dark.setColor(QPalette::AlternateBase, QColor(0x1d, 0x1f, 0x22));
        dark.setColor(QPalette::Text, QColor(0xfc, 0xfc, 0xfc));
        dark.setColor(QPalette::Button, QColor(0x29, 0x2c, 0x30));
        dark.setColor(QPalette::ButtonText, QColor(0xfc, 0xfc, 0xfc));
        dark.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
        dark.setColor(QPalette::Link, QColor(0x1d, 0x99, 0xf3));
        dark.setColor(QPalette::LinkVisited, QColor(0x9b, 0x59, 0xb6));
        dark.setColor(QPalette::PlaceholderText, QColor(0xa1, 0xa9, 0xb1));
        QApplication::setPalette(dark);
    }

    auto *window = new MainWindow;
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        window->restoreSession();
    } else {
        window->openPath(args.first());
    }
    window->show();

    if (parser.isSet(grabOption)) {
        const QString target = parser.value(grabOption);
        // The off-screen platform has no saved geometry; KALAMUS_GRAB_SIZE=WxH overrides.
        const QStringList size = qEnvironmentVariable("KALAMUS_GRAB_SIZE", u"1100x760"_s).split(u'x');
        window->resize(size.value(0).toInt(), size.value(1).toInt());
        // KALAMUS_GRAB_LINE=n scrolls block n to the top first.
        if (const int line = qEnvironmentVariableIntValue("KALAMUS_GRAB_LINE"); line > 0) {
            QTimer::singleShot(500, window, [window, line] {
                if (auto *editor = window->findChild<QTextEdit *>()) {
                    const QTextBlock block = editor->document()->findBlockByNumber(line);
                    editor->verticalScrollBar()->setValue(int(editor->document()->documentLayout()->blockBoundingRect(block).top()));
                }
            });
        }
        // KALAMUS_GRAB_REPLACE=text opens the find/replace bar searching for it.
        if (const QString find = qEnvironmentVariable("KALAMUS_GRAB_REPLACE"); !find.isEmpty()) {
            QTimer::singleShot(600, window, [window, find] {
                if (auto *bar = window->findChild<FindBar *>()) {
                    bar->openReplace();
                    bar->setFindText(find);
                }
            });
        }
        QTimer::singleShot(1000, window, [window, target] {
            window->grab().save(target);
            QApplication::quit();
        });
    }

    return app.exec();
}
