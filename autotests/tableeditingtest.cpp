// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QTest>
#include <QTextBlock>

using namespace Qt::StringLiterals;

class TableEditingTest : public QObject
{
    Q_OBJECT

private:
    static void placeCursor(MarkdownEditor &editor, const QString &needle)
    {
        const int pos = editor.markdown().indexOf(needle);
        QVERIFY(pos >= 0);
        QTextCursor c = editor.textCursor();
        c.setPosition(pos);
        editor.setTextCursor(c);
        QCoreApplication::processEvents(); // queued re-highlights
    }

    const QString m_doc = u"Intro\n\n|a|b|\n|-|-|\n|1|2|\n\nAfter\n"_s;

private Q_SLOTS:
    void rowsFitWrappedText()
    {
        // A long cell wraps; its row must be as tall as the wrapped text, and the grid row
        // must span from this row's top to the next row's top (Qt adds line spacing there).
        MarkdownEditor editor;
        editor.resize(700, 600);
        editor.show();
        editor.loadMarkdown(u"|a|b|\n|-|-|\n|x|one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty|\n|y|z|\n\nAfter"_s);
        placeCursor(editor, u"After"_s);
        QCoreApplication::processEvents();
        auto *layout = editor.document()->documentLayout();
        const qreal top = layout->blockBoundingRect(editor.document()->findBlockByNumber(2)).top();
        const qreal next = layout->blockBoundingRect(editor.document()->findBlockByNumber(3)).top();
        const qreal oneLine = next - top;
        const qreal shortRow = layout->blockBoundingRect(editor.document()->findBlockByNumber(4)).top() - next;
        QVERIFY2(oneLine > shortRow * 1.6, qPrintable(u"wrapped row %1, short row %2"_s.arg(oneLine).arg(shortRow)));
    }

    void blockStates()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(m_doc);
        QCoreApplication::processEvents();
        const QTextDocument *doc = editor.document();
        QCOMPARE(doc->findBlockByNumber(2).userState(), int(MarkdownHighlighter::TableHeader));
        QCOMPARE(doc->findBlockByNumber(3).userState(), int(MarkdownHighlighter::TableDelimiter));
        QCOMPARE(doc->findBlockByNumber(4).userState(), int(MarkdownHighlighter::TableRow));
        QCOMPARE(doc->findBlockByNumber(5).userState(), int(MarkdownHighlighter::Normal));
    }

    void typingADelimiterMakesATable()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"| a | b |\n"_s);
        QTextCursor c = editor.textCursor();
        c.movePosition(QTextCursor::End);
        editor.setTextCursor(c);
        QTest::keyClicks(&editor, u"|---|---|"_s);
        QCoreApplication::processEvents();
        QCOMPARE(editor.document()->firstBlock().userState(), int(MarkdownHighlighter::TableHeader));
        QVERIFY(editor.isInTable());
    }

    void tabMovesAndFormats()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(m_doc);
        placeCursor(editor, u"1|"_s);
        QVERIFY(editor.isInTable());

        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.markdown(), u"Intro\n\n| a   | b   |\n| --- | --- |\n| 1   | 2   |\n\nAfter\n"_s);
        QCOMPARE(editor.textCursor().selectedText(), u"2"_s);

        // Tab past the last cell adds a row.
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.markdown(), u"Intro\n\n| a   | b   |\n| --- | --- |\n| 1   | 2   |\n|     |     |\n\nAfter\n"_s);
        QCOMPARE(editor.textCursor().block().blockNumber(), 5);

        QTest::keyClicks(&editor, u"x"_s);
        QTest::keyClick(&editor, Qt::Key_Backtab);
        QCOMPARE(editor.textCursor().selectedText(), u"2"_s);

        // Undo takes back one step at a time.
        editor.undo();
        QVERIFY(editor.markdown().contains(u"| x"_s));
    }

    void enterAddsRowThenLeaves()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(m_doc);
        placeCursor(editor, u"2|"_s);

        QTest::keyClick(&editor, Qt::Key_Return);
        QCOMPARE(editor.markdown(), u"Intro\n\n| a   | b   |\n| --- | --- |\n| 1   | 2   |\n|     |     |\n\nAfter\n"_s);

        QTest::keyClick(&editor, Qt::Key_Return); // empty last row: leave the table
        QCOMPARE(editor.markdown(), u"Intro\n\n| a   | b   |\n| --- | --- |\n| 1   | 2   |\n\n\n\nAfter\n"_s);
        QVERIFY(!editor.isInTable());
        QCOMPARE(editor.textCursor().block().blockNumber(), 6);
    }

    void insertTable()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Text"_s);
        QTextCursor c = editor.textCursor();
        c.movePosition(QTextCursor::End);
        editor.setTextCursor(c);
        editor.insertTable(2, 1);
        QCOMPARE(editor.markdown(), u"Text\n\n| Column 1 | Column 2 |\n| -------- | -------- |\n|          |          |"_s);
        QCOMPARE(editor.textCursor().selectedText(), u"Column 1"_s);
        QVERIFY(editor.isInTable());
    }

    void columnOperations()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(m_doc);
        placeCursor(editor, u"1|"_s);
        editor.insertTableColumnAfter();
        QVERIFY(editor.markdown().contains(u"| a   |     | b   |"_s));
        editor.setTableColumnAlignment(MarkdownTable::Align::Right);
        QVERIFY(editor.markdown().contains(u"| --- | --: | --- |"_s));
        editor.deleteTableColumn();
        QVERIFY(editor.markdown().contains(u"| a   | b   |"_s));
    }

    void cursorOutsideHidesSource()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(m_doc);
        placeCursor(editor, u"After"_s);
        const QTextBlock header = editor.document()->findBlockByNumber(2);
        // Collapsed: everything after the first character uses the hidden (zero-width) format.
        bool hidden = false;
        for (const auto &range : header.layout()->formats()) {
            if (range.start > 0 && range.format.fontLetterSpacingType() == QFont::PercentageSpacing && range.format.fontLetterSpacing() < 5) {
                hidden = true;
            }
        }
        QVERIFY(hidden);
    }
};

QTEST_MAIN(TableEditingTest)
#include "tableeditingtest.moc"
