// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "outline.h"
#include "outlinepanel.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>

using namespace Qt::StringLiterals;

class OutlineTest : public QObject
{
    Q_OBJECT

private:
    static QStringList titles(const QList<OutlineHeading> &headings)
    {
        QStringList out;
        for (const auto &h : headings) {
            out.append(QString::number(h.level) + u' ' + h.title);
        }
        return out;
    }

private Q_SLOTS:
    void plainTitles_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"Introduction"_s << u"Introduction"_s;
        QTest::newRow("closing hashes") << u"Setup ##"_s << u"Setup"_s;
        QTest::newRow("emphasis") << u"**Bold** and *italic* and ~~gone~~"_s << u"Bold and italic and gone"_s;
        QTest::newRow("code") << u"The `main()` function"_s << u"The main() function"_s;
        QTest::newRow("link") << u"See [the docs](https://kde.org)"_s << u"See the docs"_s;
        QTest::newRow("snake_case kept") << u"Using my_var_name"_s << u"Using my_var_name"_s;
        QTest::newRow("escape") << u"C\\# notes"_s << u"C# notes"_s;
        QTest::newRow("hash inside") << u"Issue #42"_s << u"Issue #42"_s;
    }

    void plainTitles()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(Outline::plainTitle(text), expected);
    }

    void headingsSkipCodeMathAndFrontMatter()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"---\ntitle: x\n# not a heading\n---\n"
                            "# One\n\ntext #not\n\n"
                            "```bash\n# a comment\n```\n\n"
                            "## Two ##\n\n"
                            "$$\n# x\n$$\n\n"
                            "####### seven is not a heading\n\n"
                            "#nospace\n\n"
                            "### Three\n\n"
                            "# Four"_s);
        QCOMPARE(titles(Outline::headings(editor.document())), (QStringList{u"1 One"_s, u"2 Two"_s, u"3 Three"_s, u"1 Four"_s}));
    }

    void setextHeadings()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Top\n===\n\nSub *title*\n---\n\ntext\n\n---\n\n- item\n---\n"_s);
        QCoreApplication::processEvents();
        QCOMPARE(titles(Outline::headings(editor.document())), (QStringList{u"1 Top"_s, u"2 Sub title"_s}));
    }

    void sectionAt()
    {
        const QList<OutlineHeading> headings{{1, u"A"_s, 2}, {2, u"B"_s, 5}, {1, u"C"_s, 9}};
        QCOMPARE(Outline::sectionAt(headings, 0), -1);
        QCOMPARE(Outline::sectionAt(headings, 2), 0);
        QCOMPARE(Outline::sectionAt(headings, 7), 1);
        QCOMPARE(Outline::sectionAt(headings, 100), 2);
    }

    void panelNestsByLevel()
    {
        OutlinePanel panel;
        panel.setHeadings({{1, u"A"_s, 0}, {3, u"A.x"_s, 2}, {2, u"B"_s, 4}, {3, u"B.1"_s, 6}, {1, u"C"_s, 8}});
        QCOMPARE(panel.topLevelItemCount(), 2);
        QTreeWidgetItem *a = panel.topLevelItem(0);
        QCOMPARE(a->childCount(), 2); // A.x (skipped a level) and B
        QCOMPARE(a->child(1)->text(0), u"B"_s);
        QCOMPARE(a->child(1)->child(0)->text(0), u"B.1"_s);

        panel.setCurrentBlock(7);
        QCOMPARE(panel.currentItem()->text(0), u"B.1"_s);
        a->setExpanded(false); // a folded section marks its visible parent
        panel.setCurrentBlock(7);
        QCOMPARE(panel.currentItem()->text(0), u"A"_s);

        // Same headings again: folds stay.
        panel.setHeadings(panel.headings());
        QVERIFY(!panel.topLevelItem(0)->isExpanded());
        // Changed headings: the fold of a surviving section stays too.
        panel.setHeadings({{1, u"A"_s, 0}, {2, u"B"_s, 4}, {1, u"C"_s, 8}});
        QVERIFY(!panel.topLevelItem(0)->isExpanded());

        panel.setHeadings({});
        QCOMPARE(panel.topLevelItemCount(), 1); // "No headings"
        QCOMPARE(panel.topLevelItem(0)->flags(), Qt::NoItemFlags);
    }

    void clickingAHeadingGoesThere()
    {
        MarkdownEditor editor;
        editor.resize(800, 300);
        editor.show();
        QStringList lines;
        for (int i = 0; i < 100; ++i) {
            lines.append(u"Paragraph %1"_s.arg(i));
        }
        lines.append(u"## Far down"_s);
        for (int i = 0; i < 30; ++i) {
            lines.append(u"More %1"_s.arg(i)); // room to scroll the heading to the top
        }
        editor.loadMarkdown(lines.join(u"\n\n"_s));

        OutlinePanel panel;
        panel.setHeadings(Outline::headings(editor.document()));
        QObject::connect(&panel, &OutlinePanel::headingActivated, &editor, &MarkdownEditor::goToBlock);
        QSignalSpy spy(&panel, &OutlinePanel::headingActivated);
        Q_EMIT panel.itemClicked(panel.topLevelItem(0), 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(editor.textCursor().block().text(), u"## Far down"_s);
        QVERIFY(editor.verticalScrollBar()->value() > 0);
        // The heading is at the top of the view.
        const QRect cursor = editor.cursorRect();
        QVERIFY2(cursor.top() >= 0 && cursor.top() < 60, qPrintable(QString::number(cursor.top())));
    }
};

QTEST_MAIN(OutlineTest)
#include "outlinetest.moc"
