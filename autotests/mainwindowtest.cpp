// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "markdowneditor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <KActionCollection>
#include <KLocalizedString>
#include <KRecentFilesAction>
#include <KSharedConfig>

using namespace Qt::StringLiterals;

class MainWindowTest : public QObject
{
    Q_OBJECT

private:
    // Not under /tmp: KDE keeps temporary files off the recent list on purpose.
    std::unique_ptr<QTemporaryDir> m_dir;

    QString write(const QString &name, const QString &text)
    {
        const QString path = m_dir->filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(text.toUtf8());
        return path;
    }

    static QStringList recent(MainWindow &window)
    {
        auto *action = qobject_cast<KRecentFilesAction *>(window.actionCollection()->action(u"file_open_recent"_s));
        QStringList out;
        if (action) {
            for (const QUrl &url : action->urls()) {
                out.append(QFileInfo(url.toLocalFile()).fileName());
            }
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // a scratch config, not the user's
        KLocalizedString::setApplicationDomain("kalamus");
        KSharedConfig::openConfig()->deleteGroup(u"Recent Files"_s);
        const QString scratch = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QDir().mkpath(scratch);
        m_dir = std::make_unique<QTemporaryDir>(scratch + u"/kalamus-test-XXXXXX"_s);
        QVERIFY(m_dir->isValid());
    }

    void openedAndSavedFilesAreRecent()
    {
        const QString a = write(u"a.md"_s, u"# A"_s);
        const QString b = write(u"b.md"_s, u"# B"_s);
        {
            MainWindow window;
            window.openPath(a);
            window.openPath(b);
            QVERIFY(recent(window).size() >= 2);
            QVERIFY(recent(window).contains(u"a.md"_s) && recent(window).contains(u"b.md"_s));
            QVERIFY(window.actionCollection()->action(u"file_open_recent"_s)->isEnabled());
        }
        // A new window (the next run) still has them.
        MainWindow again;
        QVERIFY(recent(again).contains(u"a.md"_s));
        QVERIFY(recent(again).contains(u"b.md"_s));
    }

    void pickingARecentFileOpensIt()
    {
        const QString c = write(u"c.md"_s, u"Hello from C"_s);
        MainWindow window;
        window.openPath(c);
        window.openPath(write(u"d.md"_s, u"D"_s));
        auto *action = qobject_cast<KRecentFilesAction *>(window.actionCollection()->action(u"file_open_recent"_s));
        Q_EMIT action->urlSelected(QUrl::fromLocalFile(c));
        QCOMPARE(window.findChild<MarkdownEditor *>()->markdown(), u"Hello from C"_s);
    }

    void aMissingFileIsDropped()
    {
        const QString gone = write(u"gone.md"_s, u"x"_s);
        MainWindow window;
        window.openPath(gone);
        window.openPath(write(u"e.md"_s, u"E"_s));
        QVERIFY(recent(window).contains(u"gone.md"_s));
        QFile::remove(gone);

        // Dismiss the "is no longer there" message.
        bool told = false;
        QTimer::singleShot(200, this, [&told] {
            if (QWidget *box = QApplication::activeModalWidget()) {
                told = true;
                box->close();
            }
        });
        auto *action = qobject_cast<KRecentFilesAction *>(window.actionCollection()->action(u"file_open_recent"_s));
        Q_EMIT action->urlSelected(QUrl::fromLocalFile(gone));
        QVERIFY(told);
        QVERIFY(!recent(window).contains(u"gone.md"_s));
    }

    void clearListIsRemembered()
    {
        {
            MainWindow window;
            window.openPath(write(u"f.md"_s, u"F"_s));
            auto *action = qobject_cast<KRecentFilesAction *>(window.actionCollection()->action(u"file_open_recent"_s));
            action->clear();
        }
        MainWindow again;
        QCOMPARE(recent(again), QStringList());
    }
};

QTEST_MAIN(MainWindowTest)
#include "mainwindowtest.moc"
