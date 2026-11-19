// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "findbar.h"
#include "markdowneditor.h"

#include <QApplication>
#include <KLocalizedString>
#include <QTest>
#include <QTextBlock>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

class FindReplaceTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        QWidget window;
        MarkdownEditor *editor;
        FindBar *bar;
        explicit Fixture(const QString &markdown)
        {
            auto *layout = new QVBoxLayout(&window);
            editor = new MarkdownEditor(&window);
            bar = new FindBar(editor, &window);
            layout->addWidget(editor);
            layout->addWidget(bar);
            window.resize(800, 500);
            window.show();
            editor->loadMarkdown(markdown);
        }
        QString selected() const
        {
            return editor->textCursor().selectedText();
        }
    };

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("kalamus");
    }

    void patterns_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("matchCase");
        QTest::addColumn<bool>("wholeWords");
        QTest::addColumn<bool>("regex");
        QTest::addColumn<QString>("subject");
        QTest::addColumn<int>("count");
        QTest::newRow("plain, any case") << u"cat"_s << false << false << false << u"Cat cat concat"_s << 3;
        QTest::newRow("match case") << u"cat"_s << true << false << false << u"Cat cat concat"_s << 2;
        QTest::newRow("whole words") << u"cat"_s << false << true << false << u"Cat cat concat"_s << 2;
        QTest::newRow("special chars are literal") << u"a.b (c)"_s << false << false << false << u"a.b (c) axb (c)"_s << 1;
        QTest::newRow("regex") << u"c\\w+t"_s << false << false << true << u"cat coat cut"_s << 3;
        QTest::newRow("whole word with symbols") << u"C++"_s << false << true << false << u"C++ and C++11"_s << 1;
    }

    void patterns()
    {
        QFETCH(QString, text);
        QFETCH(bool, matchCase);
        QFETCH(bool, wholeWords);
        QFETCH(bool, regex);
        QFETCH(QString, subject);
        QFETCH(int, count);
        const auto re = FindBar::pattern(text, {matchCase, wholeWords, regex});
        QVERIFY(re.isValid());
        int n = 0;
        for (auto it = re.globalMatch(subject); it.hasNext(); it.next()) {
            ++n;
        }
        QCOMPARE(n, count);
    }

    void expandsCaptures()
    {
        const auto m = QRegularExpression(u"(\\w+)@(\\w+)"_s).match(u"me@host"_s);
        QCOMPARE(FindBar::expand(u"\\2 at $1, \\\\1 costs \\$1"_s, m), u"host at me, \\1 costs $1"_s);
        QCOMPARE(FindBar::expand(u"a\\nb\\tc"_s, m), u"a\nb\tc"_s);
    }

    void findsAsYouTypeAndCounts()
    {
        Fixture f(u"One apple.\n\nTwo apples and an Apple pie.\n\nNo fruit."_s);
        f.bar->openFind();
        QVERIFY(f.bar->isVisible());
        f.bar->setFindText(u"apple"_s);
        QCOMPARE(f.bar->matchCount(), 3);
        QCOMPARE(f.bar->currentMatch(), 1);
        QCOMPARE(f.bar->status(), u"1 of 3"_s);
        QCOMPARE(f.editor->extraSelections().size(), 3);

        QVERIFY(f.bar->findNext());
        QCOMPARE(f.bar->currentMatch(), 2);
        QVERIFY(f.bar->findNext());
        QVERIFY(f.bar->findNext()); // wraps to the first
        QCOMPARE(f.bar->currentMatch(), 1);
        QVERIFY(f.bar->status().contains(u"wrapped"_s));
        QVERIFY(f.bar->findPrevious()); // and back to the last
        QCOMPARE(f.bar->currentMatch(), 3);
        QCOMPARE(f.selected(), u"Apple"_s);

        f.bar->setOptions({true, false, false}); // match case
        QCOMPARE(f.bar->matchCount(), 2);

        f.bar->setFindText(u"banana"_s);
        QCOMPARE(f.bar->matchCount(), 0);
        QCOMPARE(f.bar->status(), u"Not found"_s);

        f.bar->closeBar();
        QVERIFY(!f.bar->isVisible());
        QCOMPARE(f.editor->extraSelections().size(), 0);
    }

    void openFindUsesTheSelection()
    {
        Fixture f(u"alpha beta gamma beta"_s);
        QTextCursor cursor(f.editor->document());
        cursor.setPosition(6);
        cursor.setPosition(10, QTextCursor::KeepAnchor);
        f.editor->setTextCursor(cursor);
        f.bar->openFind();
        QCOMPARE(f.bar->matchCount(), 2);
        QCOMPARE(f.bar->currentMatch(), 1);
    }

    void invalidRegexSaysSo()
    {
        Fixture f(u"text"_s);
        f.bar->openFind();
        f.bar->setOptions({false, false, true});
        f.bar->setFindText(u"(unclosed"_s);
        QCOMPARE(f.bar->status(), u"Invalid pattern"_s);
        QCOMPARE(f.bar->matchCount(), 0);
    }

    void replaceOneThenNext()
    {
        Fixture f(u"red, red, red"_s);
        f.bar->openReplace();
        f.bar->setFindText(u"red"_s);
        f.bar->setReplaceText(u"blue"_s);
        QVERIFY(f.bar->replace());
        QCOMPARE(f.editor->markdown(), u"blue, red, red"_s);
        QCOMPARE(f.bar->currentMatch(), 1); // the next "red" is selected
        QCOMPARE(f.editor->textCursor().selectionStart(), 6);
        QVERIFY(f.bar->replace());
        QCOMPARE(f.editor->markdown(), u"blue, blue, red"_s);
    }

    void replaceAllIsOneUndoStep()
    {
        Fixture f(u"# Title\n\nfoo bar foo\n\nfood foo"_s);
        f.bar->openReplace();
        f.bar->setOptions({false, true, false}); // whole words: not "food"
        f.bar->setFindText(u"foo"_s);
        f.bar->setReplaceText(u"baz"_s);
        QCOMPARE(f.bar->replaceAll(), 3);
        QCOMPARE(f.editor->markdown(), u"# Title\n\nbaz bar baz\n\nfood baz"_s);
        QCOMPARE(f.bar->status(), u"Replaced 3"_s);
        f.editor->undo();
        QCOMPARE(f.editor->markdown(), u"# Title\n\nfoo bar foo\n\nfood foo"_s);
    }

    void regexReplaceWithCaptures()
    {
        Fixture f(u"Date: 2026-11-19 and 2025-01-02"_s);
        f.bar->openReplace();
        f.bar->setOptions({false, false, true});
        f.bar->setFindText(u"(\\d{4})-(\\d\\d)-(\\d\\d)"_s);
        f.bar->setReplaceText(u"\\3/\\2/\\1"_s);
        QCOMPARE(f.bar->replaceAll(), 2);
        QCOMPARE(f.editor->markdown(), u"Date: 19/11/2026 and 02/01/2025"_s);
    }

    void matchesFollowEdits()
    {
        Fixture f(u"one two"_s);
        f.bar->openFind();
        f.bar->setFindText(u"two"_s);
        QCOMPARE(f.bar->matchCount(), 1);
        QTextCursor end(f.editor->document());
        end.movePosition(QTextCursor::End);
        end.insertText(u" two"_s);
        QTRY_COMPARE(f.bar->matchCount(), 2);
    }

    void escapeClosesAndReturnsFocus()
    {
        Fixture f(u"text"_s);
        f.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.window));
        f.bar->openFind();
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QVERIFY(!f.bar->isVisible());
        QVERIFY(f.editor->hasFocus());
    }
};

QTEST_MAIN(FindReplaceTest)
#include "findreplacetest.moc"
