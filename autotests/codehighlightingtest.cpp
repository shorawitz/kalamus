// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "codehighlighter.h"
#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>
#include <QTextBlock>

using namespace Qt::StringLiterals;

class CodeHighlightingTest : public QObject
{
    Q_OBJECT

private:
    /// The foreground colour at @p column of block @p number, or an invalid colour.
    static QColor colorAt(MarkdownEditor &editor, int number, int column)
    {
        const auto formats = editor.document()->findBlockByNumber(number).layout()->formats();
        QColor color;
        for (const auto &range : formats) {
            if (column >= range.start && column < range.start + range.length && range.format.hasProperty(QTextFormat::ForegroundBrush)) {
                color = range.format.foreground().color();
            }
        }
        return color;
    }

    static void settle()
    {
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
        }
    }

private Q_SLOTS:
    void pasteYamlIntoYamlBlock()
    {
        MarkdownEditor editor;
        editor.resize(900, 600);
        editor.show();
        editor.loadMarkdown(u"Intro\n\n```yaml\n\n```\n\nAfter"_s);
        QTextCursor cursor(editor.document()->findBlockByNumber(3));
        editor.setTextCursor(cursor);
        QMimeData mime;
        mime.setText(u"# config\nserver:\n  host: example.org   # inline comment\n  ports: [80, 443]\n  tls: true\n"
                     "list:\n  - name: \"a: b\"\n    value: |\n      multi\n      line\n---\nanchors: &base\n  x: 1\nother:\n  <<: *base\n"_s);
        QGuiApplication::clipboard()->setMimeData(new QMimeData);
        QGuiApplication::clipboard()->setText(mime.text());
        editor.paste();
        settle();
        QVERIFY(editor.markdown().contains(u"ports: [80, 443]"_s));
        // Moving the cursor out and back re-styles the block both ways.
        editor.moveCursor(QTextCursor::End);
        settle();
        editor.moveCursor(QTextCursor::Start);
        settle();
        QVERIFY(colorAt(editor, 4, 0).isValid()); // "server:" is coloured as YAML
    }

    void languageNames_data()
    {
        QTest::addColumn<QString>("fence");
        QTest::addColumn<QString>("expected");
        QTest::newRow("cpp") << u"cpp"_s << u"C++"_s;
        QTest::newRow("c++") << u"c++"_s << u"C++"_s;
        QTest::newRow("py") << u"py"_s << u"Python"_s;
        QTest::newRow("Python") << u"Python"_s << u"Python"_s;
        QTest::newRow("sh") << u"sh"_s << u"Bash"_s;
        QTest::newRow("bash") << u"bash"_s << u"Bash"_s;
        QTest::newRow("js") << u"js"_s << u"JavaScript"_s;
        QTest::newRow("yml") << u"yml"_s << u"YAML"_s;
        QTest::newRow("json") << u"json"_s << u"JSON"_s;
        QTest::newRow("unknown") << u"unknownlang"_s << QString();
        QTest::newRow("text") << u"text"_s << QString();
    }

    void languageNames()
    {
        QFETCH(QString, fence);
        QFETCH(QString, expected);
        const auto definition = CodeHighlighter::instance().definitionFor(fence);
        QCOMPARE(definition.isValid() ? definition.name() : QString(), expected);
    }

    void shebangs_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("language");
        QTest::newRow("env python3") << u"#!/usr/bin/env python3"_s << u"python"_s;
        QTest::newRow("bash") << u"#!/bin/bash"_s << u"bash"_s;
        QTest::newRow("env -S node") << u"#!/usr/bin/env -S node --flag"_s << u"javascript"_s;
        QTest::newRow("python3.12") << u"#!/usr/local/bin/python3.12"_s << u"python"_s;
        QTest::newRow("not a shebang") << u"import os"_s << QString();
    }

    void shebangs()
    {
        QFETCH(QString, line);
        QFETCH(QString, language);
        QCOMPARE(CodeHighlighter::languageFromShebang(line), language);
    }

    void unnamedScriptIsColouredFromItsShebang()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"```\n#!/usr/bin/env python3\ndef f():\n    return 1\n```\n\nEnd"_s);
        settle();
        QVERIFY(colorAt(editor, 3, 11).isValid()); // the number in "    return 1"
    }

    void codeBlockWrapsSelectedLines()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Intro\nimport os\nprint(os.name)\nAfter"_s);
        QTextCursor c = editor.textCursor();
        c.setPosition(editor.markdown().indexOf(u"port os"_s)); // mid-line start
        c.setPosition(editor.markdown().indexOf(u"name"_s), QTextCursor::KeepAnchor);
        editor.setTextCursor(c);
        QVERIFY(editor.selectionSpansLines());
        editor.insertCodeBlock(u"python"_s);
        QCOMPARE(editor.markdown(), u"Intro\n```python\nimport os\nprint(os.name)\n```\nAfter"_s);
        settle();
        QVERIFY(colorAt(editor, 2, 0).isValid()); // "import" is coloured
        editor.undo();
        QCOMPARE(editor.markdown(), u"Intro\nimport os\nprint(os.name)\nAfter"_s); // one undo step
    }

    void emptyCodeBlock()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Text"_s);
        QTextCursor c = editor.textCursor();
        c.movePosition(QTextCursor::End);
        editor.setTextCursor(c);
        editor.insertCodeBlock(u"bash"_s);
        QCOMPARE(editor.markdown(), u"Text\n```bash\n\n```"_s);
        QCOMPARE(editor.textCursor().blockNumber(), 2);
    }

    void keywordsAreColoured()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"Intro\n\n```cpp\nint x = 42;\n```\n"_s);
        settle();
        QVERIFY(colorAt(editor, 3, 0).isValid()); // "int"
        QVERIFY(colorAt(editor, 3, 8).isValid()); // "42"
        QVERIFY(colorAt(editor, 3, 0) != colorAt(editor, 3, 8));
    }

    void stateCarriesAcrossLines()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"```cpp\n/* open\nstill a comment\n*/ int x;\n```\n\nEnd"_s);
        settle();
        const QColor comment = colorAt(editor, 1, 0);
        QVERIFY(comment.isValid());
        QCOMPARE(colorAt(editor, 2, 0), comment);

        // Remove the "/*": the line below stops being a comment, though it wasn't edited.
        QTextCursor c(editor.document()->findBlockByNumber(1));
        c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 2);
        c.removeSelectedText();
        settle();
        QVERIFY(colorAt(editor, 2, 0) != comment);
    }

    void fencesHideWhenTheCursorIsOutside()
    {
        MarkdownEditor editor;
        editor.loadMarkdown(u"```cpp\nint x;\n```\n\nEnd"_s);
        QTextCursor c = editor.textCursor();
        c.movePosition(QTextCursor::End);
        editor.setTextCursor(c);
        settle();
        auto fenceHidden = [&](int block) {
            for (const auto &range : editor.document()->findBlockByNumber(block).layout()->formats()) {
                if (range.start == 0 && range.format.hasProperty(QTextFormat::FontLetterSpacing) && range.format.fontLetterSpacing() < 5) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(fenceHidden(0));
        QVERIFY(fenceHidden(2));

        // Cursor inside the code: both fences show.
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(1)));
        settle();
        QVERIFY(!fenceHidden(0));
        QVERIFY(!fenceHidden(2));
    }
};

QTEST_MAIN(CodeHighlightingTest)
#include "codehighlightingtest.moc"
