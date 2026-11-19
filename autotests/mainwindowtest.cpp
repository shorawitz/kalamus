// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "kalamussettings.h"
#include "markdowneditor.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextLayout>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <KToolBar>

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
        QCoreApplication::setApplicationName(u"kalamus"_s); // so the window finds kalamusui.rc
        KLocalizedString::setApplicationDomain("kalamus");
        KSharedConfig::openConfig()->deleteGroup(u"Recent Files"_s);
        KalamusSettings::self()->setDefaults(); // a clean start, whatever an earlier run left
        KalamusSettings::self()->save();
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

    void settingsDialogAppliesToTheEditor()
    {
        MainWindow window;
        window.openPath(write(u"s.md"_s, u"Text with `code` in it."_s));
        auto *editor = window.findChild<MarkdownEditor *>();
        QCOMPARE(editor->readableWidth(), MarkdownEditor::DefaultReadableWidth);

        SettingsDialog dialog(&window);
        QObject::connect(&dialog, &KConfigDialog::settingsChanged, &window, [&window] {
            QMetaObject::invokeMethod(&window, "applySettings");
        });
        auto *width = dialog.findChild<QSpinBox *>(u"kcfg_ReadableWidth"_s);
        auto *limit = dialog.findChild<QCheckBox *>(u"kcfg_LimitLineWidth"_s);
        auto *spelling = dialog.findChild<QCheckBox *>(u"kcfg_Enabled"_s);
        auto *remote = dialog.findChild<QCheckBox *>(u"kcfg_LoadRemoteImages"_s);
        auto *codeFont = dialog.findChild<QFontComboBox *>(u"kcfg_CodeFont"_s);
        QVERIFY(width && limit && spelling && remote && codeFont);
        QVERIFY(limit->isChecked());

        width->setValue(80);
        spelling->setChecked(false);
        remote->setChecked(false);
        QFont mono(u"DejaVu Sans Mono"_s);
        codeFont->setCurrentFont(mono);
        QPushButton *apply = dialog.button(QDialogButtonBox::Apply);
        QVERIFY(apply->isEnabled());
        apply->click();

        QCOMPARE(KalamusSettings::readableWidth(), 80);
        QCOMPARE(editor->readableWidth(), 80);
        QVERIFY(!editor->spellCheckingEnabled());
        QVERIFY(!editor->loadRemoteImages());
        // The Tools menu switch follows the dialog.
        QVERIFY(!window.actionCollection()->action(u"tools_spelling_auto"_s)->isChecked());

        // Code spans use the chosen family.
        const auto formats = editor->document()->firstBlock().layout()->formats();
        bool found = false;
        for (const auto &range : formats) {
            if (editor->document()->firstBlock().text().mid(range.start, range.length) == u"code"_s) {
                found = range.format.fontFamilies().toStringList().contains(codeFont->currentFont().family());
            }
        }
        QVERIFY(found);

        // Defaults puts everything back.
        dialog.button(QDialogButtonBox::RestoreDefaults)->click();
        QCoreApplication::processEvents(); // the dialog updates its buttons on the next pass
        apply->click();
        QCOMPARE(editor->readableWidth(), MarkdownEditor::DefaultReadableWidth);
        QVERIFY(editor->spellCheckingEnabled());
        QVERIFY(editor->loadRemoteImages());
    }

    void menuSwitchesSaveSettings()
    {
        MainWindow window;
        QAction *limit = window.actionCollection()->action(u"view_limit_line_width"_s);
        QVERIFY(limit->isChecked());
        limit->trigger();
        QVERIFY(!KalamusSettings::limitLineWidth());
        QVERIFY(!window.findChild<MarkdownEditor *>()->limitLineWidth());
        limit->trigger();
        QVERIFY(KalamusSettings::limitLineWidth());
    }

    void settingsActionExists()
    {
        MainWindow window;
        QVERIFY(window.actionCollection()->action(u"options_configure"_s));
    }

    void toolbarOrder()
    {
        MainWindow window;
        QStringList names;
        for (QAction *action : window.toolBar(u"mainToolBar"_s)->actions()) {
            names.append(action->isSeparator() ? u"|"_s : action->objectName());
        }
        QCOMPARE(names.join(u' '),
                 u"file_new file_open file_open_folder file_save file_save_as | edit_undo edit_redo | edit_cut edit_copy edit_paste | "
                 "format_bold format_italic format_strikethrough format_code format_code_block format_link insert_image table_insert"_s);
    }
};

QTEST_MAIN(MainWindowTest)
#include "mainwindowtest.moc"
