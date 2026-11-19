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
        QTimer::singleShot(1000, window, [window, target] {
            window->grab().save(target);
            QApplication::quit();
        });
    }

    return app.exec();
}
