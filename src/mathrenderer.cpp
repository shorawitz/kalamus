// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mathrenderer.h"

#include <QPainter>

#include <jkqtmathtext/jkqtmathtext.h>

#include <cmath>

using namespace Qt::StringLiterals;

namespace
{
constexpr int kCacheLimit = 1000;

QString keyOf(const QString &latex, bool display, qreal pixelSize)
{
    return (display ? u"D"_s : u"I"_s) + QString::number(pixelSize, 'f', 2) + u'|' + latex;
}
} // namespace

MathRenderer &MathRenderer::instance()
{
    static MathRenderer renderer;
    return renderer;
}

MathRenderer::MathRenderer()
    : m_math(std::make_unique<JKQTMathText>())
{
    m_math->useXITS();
}

MathRenderer::~MathRenderer() = default;

bool MathRenderer::parse(const QString &latex, bool display, qreal pixelSize)
{
    const QString key = keyOf(latex, display, pixelSize);
    if (key == m_parsedKey) {
        return !m_math->hadErrors();
    }
    m_parsedKey = key;
    m_math->setFontSizePixels(pixelSize);
    const QString markup = display ? u"\\displaystyle "_s + latex : latex;
    JKQTMathText::ParseOptions options;
    options.setFlag(JKQTMathText::StartWithMathMode);
    options.setFlag(JKQTMathText::AllowLinebreaks);
    return m_math->parse(markup, JKQTMathText::LatexParser, options);
}

MathRenderer::Metrics MathRenderer::measure(const QString &latex, bool display, qreal pixelSize)
{
    const QString key = keyOf(latex, display, pixelSize);
    if (const auto it = m_metrics.constFind(key); it != m_metrics.cend()) {
        return *it;
    }
    if (m_metrics.size() > kCacheLimit) {
        m_metrics.clear();
    }

    Metrics metrics;
    if (latex.trimmed().isEmpty()) {
        metrics.error = u"Empty formula."_s;
    } else {
        const bool parsed = parse(latex, display, pixelSize);
        if (parsed && !m_math->hadErrors()) {
            QImage scratch(1, 1, QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&scratch);
            const auto size = m_math->getSizeDetail(painter);
            metrics.ok = size.width > 0;
            metrics.width = size.width;
            metrics.ascent = size.getAscent();
            metrics.descent = size.getDescent();
        }
        if (!metrics.ok) {
            metrics.error = m_math->getErrorList().join(u'\n');
            if (metrics.error.isEmpty()) {
                metrics.error = u"The formula could not be read."_s;
            }
        }
    }
    m_metrics.insert(key, metrics);
    return metrics;
}

QImage MathRenderer::render(const QString &latex, bool display, qreal pixelSize, const QColor &color, qreal devicePixelRatio)
{
    const QString key = keyOf(latex, display, pixelSize) + u'|' + color.name(QColor::HexArgb) + u'|' + QString::number(devicePixelRatio);
    if (const auto it = m_images.constFind(key); it != m_images.cend()) {
        return *it;
    }
    const Metrics metrics = measure(latex, display, pixelSize);
    if (!metrics.ok || !parse(latex, display, pixelSize)) {
        return {};
    }
    if (m_images.size() > kCacheLimit) {
        m_images.clear();
    }

    const QSize pixels(int(std::ceil(metrics.width * devicePixelRatio)), int(std::ceil((metrics.ascent + metrics.descent) * devicePixelRatio)));
    QImage image(pixels.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        m_math->setFontColor(color);
        m_math->draw(painter, 0.0, metrics.ascent);
    }
    m_images.insert(key, image);
    return image;
}

void MathRenderer::clearCache()
{
    m_metrics.clear();
    m_images.clear();
    m_parsedKey.clear();
}
