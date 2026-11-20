// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

// Bold, italic, strikethrough and code that run across the lines of one paragraph.

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

using namespace Qt::StringLiterals;

class ParagraphStylingTest : public QObject
{
    Q_OBJECT

private:
    static QTextCharFormat formatAt(MarkdownEditor &editor, int block, int column)
    {
        QTextCharFormat format;
        for (const auto &range : editor.document()->findBlockByNumber(block).layout()->formats()) {
            if (column >= range.start && column < range.start + range.length) {
                format.merge(range.format);
            }
        }
        return format;
    }

    static bool isBold(MarkdownEditor &editor, int block, int column)
    {
        return formatAt(editor, block, column).fontWeight() >= QFont::Bold;
    }

    static bool isHidden(MarkdownEditor &editor, int block, int column)
    {
        const auto format = formatAt(editor, block, column);
        return format.hasProperty(QTextFormat::ForegroundBrush) && format.foreground().color().alpha() == 0;
    }

    static bool isCode(MarkdownEditor &editor, int block, int column)
    {
        return formatAt(editor, block, column).hasProperty(QTextFormat::BackgroundBrush); // code spans have a background
    }

    /// Loads @p markdown with the cursor on its last line, out of the way.
    static void load(MarkdownEditor &editor, const QString &markdown)
    {
        editor.loadMarkdown(markdown + u"\n\nEnd"_s);
        editor.moveCursor(QTextCursor::End);
        settle();
    }

    static void settle()
    {
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
        }
    }

    static void insert(MarkdownEditor &editor, int block, int column, const QString &text)
    {
        QTextCursor cursor(editor.document()->findBlockByNumber(block));
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
        cursor.insertText(text);
        editor.moveCursor(QTextCursor::End);
        settle();
    }

private Q_SLOTS:
    void boldAcrossLines()
    {
        MarkdownEditor editor;
        load(editor, u"Start **bold\ntext** end"_s);
        QVERIFY(!isBold(editor, 0, 0)); // Start
        QVERIFY(isBold(editor, 0, 8)); // bold
        QVERIFY(isBold(editor, 1, 0)); // text
        QVERIFY(!isBold(editor, 1, 8)); // end
        QVERIFY(isHidden(editor, 0, 6)); // **
        QVERIFY(isHidden(editor, 1, 4)); // **
    }

    void italicStrikeAndCodeAcrossLines()
    {
        MarkdownEditor editor;
        load(editor, u"An *italic\nphrase* and ~~struck\nout~~ and `some\ncode` here"_s);
        QVERIFY(formatAt(editor, 1, 0).fontItalic()); // phrase
        QVERIFY(formatAt(editor, 2, 0).fontStrikeOut()); // out
        QVERIFY(isCode(editor, 2, 12)); // some
        QVERIFY(isCode(editor, 3, 0)); // code
        QVERIFY(!isCode(editor, 3, 6)); // here
    }

    void closingOnAnotherLineRestylesTheOpeningLine()
    {
        MarkdownEditor editor;
        load(editor, u"Start **bold\ntext end"_s);
        QVERIFY(!isBold(editor, 0, 8));

        insert(editor, 1, 4, u"**"_s); // "text** end"
        QVERIFY(isBold(editor, 0, 8)); // the line above was not edited, yet turns bold
        QVERIFY(isBold(editor, 1, 0));

        // A blank line splits the paragraph: neither half is bold any more.
        insert(editor, 0, 12, u"\n"_s);
        QVERIFY(!isBold(editor, 0, 8));
        QVERIFY(!isBold(editor, 2, 0));
    }

    void separateParagraphsDoNotJoin()
    {
        MarkdownEditor editor;
        load(editor, u"One **a\n\nb** two"_s);
        QVERIFY(!isBold(editor, 0, 6));
        QVERIFY(!isBold(editor, 2, 0));
    }

    void headingsAndListItemsStartTheirOwnParagraph()
    {
        MarkdownEditor editor;
        load(editor, u"# Head **a\nb** text\n\n- one **c\n- two d** e"_s);
        QVERIFY(!isBold(editor, 1, 0));
        QVERIFY(!isBold(editor, 4, 6));
    }

    void listAndQuoteContinuationsJoin()
    {
        MarkdownEditor editor;
        load(editor, u"- item **a\n  b** rest\n\n> quote **c\n> d** rest"_s);
        QVERIFY(isBold(editor, 0, 9)); // a
        QVERIFY(isBold(editor, 1, 2)); // b
        QVERIFY(!isBold(editor, 1, 7)); // rest
        QVERIFY(isBold(editor, 3, 10)); // c
        QVERIFY(isBold(editor, 4, 2)); // d
    }

    void continuationIndentStaysOutOfCode()
    {
        MarkdownEditor editor;
        load(editor, u"- item `code\n  continued` rest"_s);
        QVERIFY(!isCode(editor, 1, 0)); // the indent
        QVERIFY(isCode(editor, 1, 2)); // continued
    }

    void unclosedMarkerDoesNotSwallowTheNextPair()
    {
        MarkdownEditor editor;
        load(editor, u"Stray **star\nthen **real** bold"_s);
        QVERIFY(!isBold(editor, 0, 8));
        QVERIFY(!isBold(editor, 1, 0)); // then
        QVERIFY(isBold(editor, 1, 7)); // real
    }

    void setextHeadings()
    {
        MarkdownEditor editor;
        load(editor, u"Title\n=====\n\nSection\n-------\n\nAfter a **break\n\n---\n\nrule** above"_s);
        const qreal body = formatAt(editor, 11, 0).fontPointSize();
        QVERIFY(formatAt(editor, 0, 0).fontPointSize() > formatAt(editor, 3, 0).fontPointSize()); // H1 > H2
        QVERIFY(formatAt(editor, 3, 0).fontPointSize() > body || body == 0);
        QVERIFY(isHidden(editor, 1, 0)); // the underlines hide
        QVERIFY(isHidden(editor, 4, 0));
        QVERIFY(!isHidden(editor, 9, 0)); // a --- after a blank line is a rule, still shown

        // Typing the underline turns the line above into a heading; deleting it turns it back.
        MarkdownEditor second;
        load(second, u"Plain line\nnext"_s);
        QTextCursor cursor(second.document()->findBlockByNumber(1));
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(u"==="_s);
        second.moveCursor(QTextCursor::End);
        settle();
        QCOMPARE(second.document()->firstBlock().userState(), int(MarkdownHighlighter::SetextHeading1));
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(u"next"_s);
        settle();
        QCOMPARE(second.document()->firstBlock().userState(), int(MarkdownHighlighter::Normal));
    }

    void headingActionConvertsSetextHeadings()
    {
        MarkdownEditor editor;
        load(editor, u"Title\n---\n\nText"_s);
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.setHeading(1);
        QVERIFY(editor.markdown().startsWith(u"# Title\n\nText"_s));
    }

    void listsInsideQuotes()
    {
        MarkdownEditor editor;
        load(editor, u"- plain\n\n> - first **a\n> - second b** c\n> 1. one"_s);
        const QColor marker = formatAt(editor, 0, 0).foreground().color();
        QCOMPARE(formatAt(editor, 2, 2).foreground().color(), marker); // the - after >
        QCOMPARE(formatAt(editor, 4, 2).foreground().color(), marker); // 1.
        QVERIFY(!isBold(editor, 3, 4)); // each item is its own paragraph: no bold across items

        // Enter continues the list inside the quote; Enter on an empty item leaves just the quote.
        MarkdownEditor typing;
        typing.show();
        typing.loadMarkdown(u"> - first"_s);
        typing.moveCursor(QTextCursor::End);
        QTest::keyClick(&typing, Qt::Key_Return);
        QCOMPARE(typing.document()->lastBlock().text(), u"> - "_s);
        QTest::keyClick(&typing, Qt::Key_Return);
        QCOMPARE(typing.document()->lastBlock().text(), u"> "_s);

        // Tab indents the item after the quote's prefix.
        typing.loadMarkdown(u"> - first\n> - second"_s);
        typing.moveCursor(QTextCursor::End);
        QTest::keyClick(&typing, Qt::Key_Tab);
        QCOMPARE(typing.document()->lastBlock().text(), u">   - second"_s);
        QTest::keyClick(&typing, Qt::Key_Backtab);
        QCOMPARE(typing.document()->lastBlock().text(), u"> - second"_s);
    }

    void activeLineShowsItsOwnMarkers()
    {
        MarkdownEditor editor;
        load(editor, u"Start **bold\ntext** end"_s);
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(0)));
        settle();
        QVERIFY(!isHidden(editor, 0, 6)); // ** on the cursor's line shows
        QVERIFY(isHidden(editor, 1, 4)); // ** on the other line stays hidden
        QVERIFY(isBold(editor, 1, 0));
    }
};

QTEST_MAIN(ParagraphStylingTest)

#include "paragraphstylingtest.moc"
