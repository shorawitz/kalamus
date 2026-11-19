// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "markdownhighlighter.h"
#include "mathrenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QTextLayout>
#include <QTest>
#include <QTextBlock>

using namespace Qt::StringLiterals;

class MathEditingTest : public QObject
{
    Q_OBJECT

private:
    static qreal blockHeight(MarkdownEditor &editor, int number)
    {
        const QTextBlock block = editor.document()->findBlockByNumber(number);
        return editor.document()->documentLayout()->blockBoundingRect(block).height();
    }

    static void moveTo(MarkdownEditor &editor, const QString &needle)
    {
        QTextCursor c = editor.textCursor();
        c.setPosition(editor.markdown().indexOf(needle));
        editor.setTextCursor(c);
        QCoreApplication::processEvents();
    }

private Q_SLOTS:
    void hiddenMarkersTakeNoSpace()
    {
        MarkdownEditor editor;
        editor.resize(800, 600);
        editor.show();
        QCoreApplication::processEvents();
        editor.loadMarkdown(u"A [x](https://kde.org) B and $a$ C\n\nOther"_s);
        moveTo(editor, u"Other"_s);
        const QTextBlock block = editor.document()->firstBlock();
        const QTextLine line = block.layout()->lineAt(0);
        const QString text = block.text();
        // "x" is followed by the hidden "](https://kde.org)".
        const qreal gap = line.cursorToX(text.indexOf(u" B"_s)) - line.cursorToX(text.indexOf(u"x]"_s) + 1);
        QVERIFY2(gap < 1.0, qPrintable(QString::number(gap)));
        // The formula's hidden source reserves about one formula width ("a" in math italic).
        QVERIFY(dynamic_cast<BlockRenderData *>(block.userData()));
        const qreal reserved = line.cursorToX(text.indexOf(u" C"_s)) - line.cursorToX(text.indexOf(u"$a$"_s));
        QVERIFY(reserved > 3 && reserved < 30);
    }

    void displayBlockCollapsesToFormulaHeight()
    {
        MarkdownEditor editor;
        editor.resize(800, 600);
        editor.show();
        QCoreApplication::processEvents();
        editor.loadMarkdown(u"Intro\n\n$$\n\\int_0^1 x^2 \\, dx = \\frac{1}{3}\n$$\n\nAfter\n"_s);
        moveTo(editor, u"After"_s);

        const auto *data = dynamic_cast<BlockRenderData *>(editor.document()->findBlockByNumber(2).userData());
        QVERIFY(data);
        QCOMPARE(data->math.size(), 1);
        QVERIFY(data->math.first().display);

        const auto metrics = MathRenderer::instance().measure(data->math.first().latex, true, 20);
        const qreal formula = metrics.ascent + metrics.descent;
        const qreal text = blockHeight(editor, 0);
        qInfo() << "formula" << formula << "opening" << blockHeight(editor, 2) << "inner" << blockHeight(editor, 3) << "closing"
                << blockHeight(editor, 4) << "text line" << text;
        QVERIFY(blockHeight(editor, 3) < 4);
        QVERIFY(blockHeight(editor, 4) < 4);
        QVERIFY(blockHeight(editor, 2) < (formula + 16) * 1.6);
    }

    void cursorInsideShowsSource()
    {
        MarkdownEditor editor;
        editor.show();
        QCoreApplication::processEvents();
        editor.loadMarkdown(u"$$\nx^2\n$$\n\nAfter"_s);
        moveTo(editor, u"x^2"_s);
        QVERIFY(!editor.document()->findBlockByNumber(0).userData()); // no rendered span
        QVERIFY(blockHeight(editor, 1) > 8); // source line visible again
        moveTo(editor, u"After"_s);
        QVERIFY(editor.document()->findBlockByNumber(0).userData());
    }

    void inlineSpans()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Text $a^2$ and $$\\sum_i i$$ and $5 and `$x$`.\n\nOther"_s);
        moveTo(editor, u"Other"_s);
        const auto *data = dynamic_cast<BlockRenderData *>(editor.document()->firstBlock().userData());
        QVERIFY(data);
        QStringList latex;
        for (const auto &span : data->math) {
            latex.append(span.latex);
        }
        QCOMPARE(latex, (QStringList{u"\\displaystyle \\sum_i i"_s, u"a^2"_s}));
    }
};

QTEST_MAIN(MathEditingTest)
#include "matheditingtest.moc"
