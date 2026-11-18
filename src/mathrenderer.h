// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

#include <memory>

class JKQTMathText;

/**
 * Renders LaTeX math (the part between the dollar signs) with JKQTMathText.
 * Sizes and images are cached; everything runs on the GUI thread.
 */
class MathRenderer
{
public:
    struct Metrics {
        bool ok = false;
        qreal width = 0;
        qreal ascent = 0; ///< above the baseline
        qreal descent = 0; ///< below the baseline
        QString error;
    };

    static MathRenderer &instance();

    Metrics measure(const QString &latex, bool display, qreal pixelSize);
    /// Transparent image, logical size = width x (ascent + descent).
    QImage render(const QString &latex, bool display, qreal pixelSize, const QColor &color, qreal devicePixelRatio);

    void clearCache();

private:
    MathRenderer();
    ~MathRenderer();
    bool parse(const QString &latex, bool display, qreal pixelSize);

    std::unique_ptr<JKQTMathText> m_math;
    QString m_parsedKey;
    QHash<QString, Metrics> m_metrics;
    QHash<QString, QImage> m_images;
};
