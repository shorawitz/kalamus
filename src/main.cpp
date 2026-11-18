// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
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
    about.setDesktopFileName(u"kalamus"_s);
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(u"text-markdown"_s));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(u"path"_s, i18n("Markdown file or folder to open"), u"[path]"_s);
    // Developer aid: render the window off-screen and save it as a PNG, then quit.
    const QCommandLineOption grabOption(u"grab"_s, i18n("Save a screenshot of the window to <file> and quit."), u"file"_s);
    parser.addOption(grabOption);
    parser.process(app);
    about.processCommandLine(&parser);

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
        window->resize(1100, 760); // the off-screen platform has no saved geometry
        QTimer::singleShot(1000, window, [window, target] {
            window->grab().save(target);
            QApplication::quit();
        });
    }

    return app.exec();
}
