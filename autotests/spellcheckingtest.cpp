// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "spellchecker.h"
#include "tablelayout.h"

#include <QMenu>
#include <QStandardPaths>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

using namespace Qt::StringLiterals;

class SpellCheckingTest : public QObject
{
    Q_OBJECT

private:
    /// The words wordsToCheck() finds in @p text, as strings.
    static QStringList words(const QString &text)
    {
        QStringList out;
        const auto found = SpellChecker::wordsToCheck(text);
        for (const auto &word : found) {
            out.append(text.mid(word.start, word.length));
        }
        return out;
    }

    /// The words of block @p number that carry the spelling underline.
    static QStringList flagged(const MarkdownEditor &editor, int number)
    {
        const QTextBlock block = editor.document()->findBlockByNumber(number);
        QList<bool> underlined(block.length(), false);
        const auto formats = block.layout()->formats();
        for (const auto &range : formats) {
            if (range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline) {
                for (int i = range.start; i < range.start + range.length; ++i) {
                    underlined[i] = true;
                }
            }
        }
        QStringList out;
        for (int i = 0; i < underlined.size(); ++i) {
            if (underlined[i] && (i == 0 || !underlined[i - 1])) {
                out.append(QString());
            }
            if (underlined[i]) {
                out.last() += block.text().at(i);
            }
        }
        return out;
    }

    static void setUpEditor(MarkdownEditor &editor, const QString &markdown)
    {
        editor.setSpellCheckingLanguage(u"en_US"_s);
        editor.loadMarkdown(markdown);
        // Keep the cursor away from the lines under test (the active line shows source).
        QTextCursor end(editor.document());
        end.movePosition(QTextCursor::End);
        editor.setTextCursor(end);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // never touch the real personal dictionary
        if (!SpellChecker::availableLanguages().values().contains(u"en_US"_s)) {
            QSKIP("No en_US dictionary installed");
        }
    }

    void wordsSkipNonProse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("plain") << u"Hello there, world."_s << QStringList{u"Hello"_s, u"there"_s, u"world"_s};
        QTest::newRow("code span") << u"Run `git stauts` now"_s << QStringList{u"Run"_s, u"now"_s};
        QTest::newRow("math") << u"So $\\alpha x$ holds"_s << QStringList{u"So"_s, u"holds"_s};
        QTest::newRow("link target") << u"See [the docs](https://exmaple.org/pathh) here"_s
                                     << QStringList{u"See"_s, u"the"_s, u"docs"_s, u"here"_s};
        QTest::newRow("bare url") << u"Go to https://kde.org/foo today"_s << QStringList{u"Go"_s, u"to"_s, u"today"_s};
        QTest::newRow("identifiers") << u"Use setText and KDE or foo_bar or v2 tool"_s << QStringList{u"Use"_s, u"and"_s, u"or"_s, u"or"_s, u"tool"_s};
        QTest::newRow("file name") << u"Edit main.cpp first"_s << QStringList{u"Edit"_s, u"first"_s};
        QTest::newRow("path") << u"Open src/editor now"_s << QStringList{u"Open"_s, u"now"_s};
        QTest::newRow("apostrophe") << u"It’s fine, don't worry"_s << QStringList{u"It’s"_s, u"fine"_s, u"don't"_s, u"worry"_s};
        QTest::newRow("hyphen") << u"A well-known fact"_s << QStringList{u"well"_s, u"known"_s, u"fact"_s};
        QTest::newRow("html") << u"A <span class=\"x\">word</span>"_s << QStringList{u"word"_s};
    }

    void wordsSkipNonProse()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, expected);
        QCOMPARE(words(text), expected);
    }

    void underlinesMisspelledProse()
    {
        MarkdownEditor editor;
        setUpEditor(editor,
                    u"This sentense has a mistake.\n\n"
                    "Inline `wrongg` code and [linkk](https://exmaple.org) text.\n\n"
                    "```python\nprnt(\"helo\")\n```\n\n"
                    "end"_s);
        QCOMPARE(flagged(editor, 0), QStringList{u"sentense"_s});
        QCOMPARE(flagged(editor, 2), QStringList{u"linkk"_s}); // the link's text, not its target
        QCOMPARE(flagged(editor, 5), QStringList{}); // inside a code block
    }

    void headingsAndTableCellsAreChecked()
    {
        MarkdownEditor editor;
        setUpEditor(editor, u"# A headng\n\n| Nmae | Age |\n| --- | --- |\n| Bob | 3 |\n\nend"_s);
        QCOMPARE(flagged(editor, 0), QStringList{u"headng"_s});
        // An inactive table is painted as a grid; the source line is checked when it is edited.
        QTextCursor inTable(editor.document()->findBlockByNumber(2));
        editor.setTextCursor(inTable);
        QCOMPARE(flagged(editor, 2), QStringList{u"Nmae"_s});
    }

    void paintedTableCellsAreUnderlined()
    {
        SpellChecker checker;
        checker.setLanguage(u"en_US"_s);
        TableLayout layout;
        layout.setSpellChecker(&checker);
        QTextDocument *doc = layout.cell(u"Speling and `mispel`"_s, false, 400, Qt::AlignLeft);
        QStringList underlined;
        for (auto it = doc->firstBlock().begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().underlineStyle() == QTextCharFormat::SpellCheckUnderline) {
                underlined.append(it.fragment().text());
            }
        }
        QCOMPARE(underlined, QStringList{u"Speling"_s}); // not the code span
    }

    void linksShowTheSpellingWave()
    {
        MarkdownEditor editor;
        setUpEditor(editor, u"A [linkk](https://kde.org) here.\n\nend"_s);
        const auto formats = editor.document()->firstBlock().layout()->formats();
        for (const auto &range : formats) {
            if (range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline) {
                QVERIFY(!range.format.fontUnderline()); // no plain link underline on top of the wave
                return;
            }
        }
        QFAIL("linkk is not underlined");
    }

    void switchingOffRemovesUnderlines()
    {
        MarkdownEditor editor;
        setUpEditor(editor, u"A mistaek here.\n\nend"_s);
        QCOMPARE(flagged(editor, 0).size(), 1);
        editor.setSpellCheckingEnabled(false);
        QCOMPARE(flagged(editor, 0).size(), 0);
        QVERIFY(editor.misspelledWordAt(4).isNull());
        editor.setSpellCheckingEnabled(true);
        QCOMPARE(flagged(editor, 0).size(), 1);
    }

    void contextMenuOffersSuggestionsAndReplaces()
    {
        MarkdownEditor editor;
        setUpEditor(editor, u"A mistaek here.\n\nend"_s);
        const QTextCursor word = editor.misspelledWordAt(4);
        QVERIFY(!word.isNull());
        QCOMPARE(word.selectedText(), u"mistaek"_s);
        QVERIFY(editor.misspelledWordAt(13).isNull()); // "here" is fine

        QMenu menu;
        menu.addAction(u"Copy"_s);
        editor.addSpellingActions(&menu, 4);
        QStringList texts;
        for (QAction *action : menu.actions()) {
            texts.append(action->text());
        }
        QVERIFY2(texts.contains(u"mistake"_s), qPrintable(texts.join(u", "_s)));
        QVERIFY(texts.contains(u"Ignore"_s));
        QCOMPARE(texts.last(), u"Copy"_s); // spelling entries come first

        for (QAction *action : menu.actions()) {
            if (action->text() == u"mistake"_s) {
                action->trigger();
            }
        }
        QCOMPARE(editor.document()->firstBlock().text(), u"A mistake here."_s);
    }

    void ignoreClearsEveryOccurrence()
    {
        MarkdownEditor editor;
        setUpEditor(editor, u"Kalamuss is here.\n\nAnd Kalamuss there.\n\nend"_s);
        QCOMPARE(flagged(editor, 0), QStringList{u"Kalamuss"_s});
        editor.acceptWord(u"Kalamuss"_s, false);
        QCOMPARE(flagged(editor, 0), QStringList{});
        QCOMPARE(flagged(editor, 2), QStringList{});
    }
};

QTEST_MAIN(SpellCheckingTest)
#include "spellcheckingtest.moc"
