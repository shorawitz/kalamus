// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mathrenderer.h"

#include <QTest>

using namespace Qt::StringLiterals;

class MathRendererTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Regressions from issues #1 and #2 (fixed in 3rdparty/jkqtmathtext-kalamus.patch and MathRenderer).
    void sizeFollowsTheTextSize()
    {
        auto &math = MathRenderer::instance();
        for (bool display : {false, true}) {
            const qreal small = math.measure(u"a+b"_s, display, 14).width;
            const qreal large = math.measure(u"a+b"_s, display, 28).width;
            QVERIFY2(large > small * 1.7, qPrintable(u"display=%1: %2 vs %3"_s.arg(display).arg(small).arg(large)));
        }
    }

    void italicLettersHaveNoTrailingGap()
    {
        auto &math = MathRenderer::instance();
        // One "b" is about half of "bb"; the slanted stand-in italic made it nearly as wide.
        QVERIFY(math.measure(u"b"_s, false, 14).width < math.measure(u"bb"_s, false, 14).width * 0.65);
    }

    void signsAreNotSpacedLikeOperators()
    {
        auto &math = MathRenderer::instance();
        const qreal sign = math.measure(u"(-y)"_s, false, 28).width;
        const qreal minus = math.measure(u"(a-y)"_s, false, 28).width;
        const qreal a = math.measure(u"a"_s, false, 28).width;
        QVERIFY(minus - sign > a + 4); // the binary minus gets room on both sides
    }

    void bigOperatorsGrowInDisplayStyle()
    {
        auto &math = MathRenderer::instance();
        const auto inlineSum = math.measure(u"\\sum x"_s, false, 14);
        const auto displaySum = math.measure(u"\\sum x"_s, true, 14);
        QVERIFY(displaySum.ascent + displaySum.descent > (inlineSum.ascent + inlineSum.descent) * 1.2);
    }

    void rendersCommonFormulas_data()
    {
        QTest::addColumn<QString>("latex");
        QTest::addColumn<bool>("display");
        QTest::newRow("einstein") << u"E = mc^2"_s << false;
        QTest::newRow("fraction") << u"\\frac{a+b}{2}"_s << false;
        QTest::newRow("integral") << u"\\int_0^1 x^2 \\, dx"_s << true;
        QTest::newRow("sum") << u"\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}"_s << true;
        QTest::newRow("greek sqrt") << u"\\alpha + \\sqrt{\\beta}"_s << false;
        QTest::newRow("matrix") << u"\\begin{matrix} a & b \\\\ c & d \\end{matrix}"_s << true;
    }

    void rendersCommonFormulas()
    {
        QFETCH(QString, latex);
        QFETCH(bool, display);
        const auto metrics = MathRenderer::instance().measure(latex, display, 16);
        QVERIFY2(metrics.ok, qPrintable(metrics.error));
        QVERIFY(metrics.width > 5);
        QVERIFY(metrics.ascent > 0);

        const QImage image = MathRenderer::instance().render(latex, display, 16, Qt::black, 2.0);
        QVERIFY(!image.isNull());
        QCOMPARE(image.devicePixelRatio(), 2.0);
        // Something was actually drawn.
        bool inked = false;
        for (int y = 0; y < image.height() && !inked; ++y) {
            for (int x = 0; x < image.width() && !inked; ++x) {
                inked = qAlpha(image.pixel(x, y)) > 0;
            }
        }
        QVERIFY(inked);
    }

    void fractionIsTallerThanText()
    {
        const auto plain = MathRenderer::instance().measure(u"x"_s, false, 16);
        const auto fraction = MathRenderer::instance().measure(u"\\frac{a}{b}"_s, false, 16);
        QVERIFY(fraction.ascent + fraction.descent > plain.ascent + plain.descent);
    }

    void reportsErrors()
    {
        const auto metrics = MathRenderer::instance().measure(u"\\frac{a"_s, false, 16);
        QVERIFY(!metrics.ok);
        QVERIFY(!metrics.error.isEmpty());
    }
};

QTEST_MAIN(MathRendererTest)
#include "mathrenderertest.moc"
