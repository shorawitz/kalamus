// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mathrenderer.h"

#include <QTest>

using namespace Qt::StringLiterals;

class MathRendererTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
