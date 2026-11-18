// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowntable.h"

#include <QTest>

using namespace Qt::StringLiterals;

class MarkdownTableTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void splitRow_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QStringList>("cells");
        QTest::newRow("outer pipes") << u"| a | b |"_s << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("no outer pipes") << u"a | b"_s << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("escaped pipe") << u"| a \\| b | c |"_s << QStringList{u"a \\| b"_s, u"c"_s};
        QTest::newRow("empty cell") << u"| a |   |"_s << QStringList{u"a"_s, QString()};
        QTest::newRow("tight") << u"|a|bb|"_s << QStringList{u"a"_s, u"bb"_s};
    }

    void splitRow()
    {
        QFETCH(QString, line);
        QFETCH(QStringList, cells);
        QStringList texts;
        for (const auto &cell : MarkdownTable::splitRow(line)) {
            texts.append(cell.text);
            QCOMPARE(line.mid(cell.start, cell.end - cell.start), cell.text);
        }
        QCOMPARE(texts, cells);
    }

    void delimiterRow()
    {
        QVERIFY(MarkdownTable::isDelimiterRow(u"|---|:-:|"_s));
        QVERIFY(MarkdownTable::isDelimiterRow(u"--- | ---"_s));
        QVERIFY(!MarkdownTable::isDelimiterRow(u"---"_s)); // a horizontal rule
        QVERIFY(!MarkdownTable::isDelimiterRow(u"| a | b |"_s));
    }

    void formatAligns()
    {
        const auto table = MarkdownTable::parse({u"|a|bb|"_s, u"|:-|-:|"_s, u"|1|2|"_s, u"|long cell|x|"_s});
        QVERIFY(table);
        QCOMPARE(table->toMarkdown(),
                 u"| a         | bb  |\n"
                 u"| :-------- | --: |\n"
                 u"| 1         | 2   |\n"
                 u"| long cell | x   |"_s);
        QCOMPARE(table->alignment(0), MarkdownTable::Align::Left);
        QCOMPARE(table->alignment(1), MarkdownTable::Align::Right);
    }

    void cellOffsets()
    {
        auto table = MarkdownTable::parse({u"| h1 | header two |"_s, u"|---|---|"_s, u"| x | y |"_s});
        QVERIFY(table);
        table->insertColumn(1);
        table->insertRow(1);
        const QString text = table->toMarkdown();
        for (int r = 0; r < table->rowCount(); ++r) {
            for (int c = 0; c < table->columnCount(); ++c) {
                const QString cell = table->cell(r, c);
                QCOMPARE(text.mid(table->cellOffset(r, c), cell.size()), cell);
                QCOMPARE(text.at(table->cellOffset(r, c) - 2), u'|');
            }
        }
    }

    void raggedRowsArePadded()
    {
        const auto table = MarkdownTable::parse({u"| a | b | c |"_s, u"|---|---|---|"_s, u"| 1 |"_s});
        QVERIFY(table);
        QCOMPARE(table->columnCount(), 3);
        QCOMPARE(table->cell(1, 2), QString());
    }

    void editing()
    {
        MarkdownTable table = MarkdownTable::create(2, 1);
        QCOMPARE(table.rowCount(), 2);
        table.removeRow(0); // the header stays
        QCOMPARE(table.rowCount(), 2);
        table.removeColumn(0);
        table.removeColumn(0); // the last column stays
        QCOMPARE(table.columnCount(), 1);
        table.setAlignment(0, MarkdownTable::Align::Center);
        QCOMPARE(table.toMarkdown(), u"| Column 2 |\n| :------: |\n|          |"_s);
    }
};

QTEST_GUILESS_MAIN(MarkdownTableTest)
#include "markdowntabletest.moc"
