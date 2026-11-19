// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

// Math support for MarkdownEditor: paints formulas over the space the highlighter
// reserved for them (see MarkdownHighlighter::mathStrut()).

#include "markdowneditor.h"
#include "markdownhighlighter.h"
#include "mathrenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QScrollBar>
#include <QTextLayout>

using namespace Qt::StringLiterals;

void MarkdownEditor::paintInlineObjects(QPainter &painter) const
{
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const qreal bottom = viewport()->height();
    const QColor color = palette().color(QPalette::Text);
    const qreal dpr = viewport()->devicePixelRatioF();
    MathRenderer &renderer = MathRenderer::instance();

    for (QTextBlock block = cursorForPosition(QPoint(0, 0)).block(); block.isValid(); block = block.next()) {
        const QRectF rect = layout->blockBoundingRect(block).translated(offset);
        if (rect.top() > bottom) {
            break;
        }
        const auto *data = dynamic_cast<BlockRenderData *>(block.userData());
        if (!data || !block.layout()) {
            continue;
        }
        for (const ImageSpan &span : data->images) {
            const QTextLine line = block.layout()->lineForTextPosition(span.start);
            if (!line.isValid()) {
                continue;
            }
            // Scaled once per size and screen scale, smoothly, then cached.
            const QSize pixels = (span.size * dpr).toSize();
            const QString key = span.url.toString() + u'|' + QString::number(pixels.width()) + u'x' + QString::number(pixels.height());
            QPixmap pixmap = m_scaledImages.value(key);
            if (pixmap.isNull()) {
                if (m_scaledImages.size() > 200) {
                    m_scaledImages.clear();
                }
                pixmap = QPixmap::fromImage(m_imageCache->image(span.url).scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                pixmap.setDevicePixelRatio(dpr);
                m_scaledImages.insert(key, pixmap);
            }
            // Small images on the text baseline; tall ones fill their line and its spacing.
            const qreal x = rect.left() + line.cursorToX(span.start);
            const qreal y = span.tall ? rect.top() + line.y() : rect.top() + line.y() + line.ascent() - span.size.height();
            painter.drawPixmap(QPointF(x, y), pixmap);
        }
        for (const MathSpan &span : data->math) {
            const qreal pixelSize = m_highlighter->mathPixelSize(span.display);
            const auto metrics = renderer.measure(span.latex, span.display, pixelSize);
            const QImage image = renderer.render(span.latex, span.display, pixelSize, color, dpr);
            const QTextLine line = block.layout()->lineForTextPosition(span.start);
            if (image.isNull() || !line.isValid()) {
                continue;
            }
            const qreal height = metrics.ascent + metrics.descent;
            QPointF topLeft;
            if (span.display) {
                // Centered in its line plus the line spacing below it.
                const qreal space = line.height() * MarkdownHighlighter::LineHeightPercent / 100.0;
                topLeft = QPointF(rect.left() + (rect.width() - metrics.width) / 2, rect.top() + line.y() + (space - height) / 2);
            } else {
                // On the text baseline, where the hidden source starts.
                const qreal baseline = rect.top() + line.y() + line.ascent();
                topLeft = QPointF(rect.left() + line.cursorToX(span.start), baseline - metrics.ascent);
            }
            painter.drawImage(topLeft, image);
        }
    }
}

void MarkdownEditor::paintCodeBackgrounds(QPainter &painter) const
{
    auto fenced = [](const QTextBlock &block) {
        return block.isValid() && (block.userState() == MarkdownHighlighter::FencedBacktick || block.userState() == MarkdownHighlighter::FencedTilde);
    };
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const qreal bottom = viewport()->height();
    const qreal margin = document()->documentMargin();
    const qreal left = margin - 10;
    const qreal width = viewport()->width() - 2 * margin + 20;

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::AlternateBase));

    // Start at the opening fence of a block that begins above the top of the view.
    QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
    while (fenced(block.previous())) {
        block = block.previous();
    }
    while (block.isValid()) {
        const qreal top = layout->blockBoundingRect(block).translated(offset).top();
        if (top > bottom) {
            break;
        }
        if (!fenced(block) || fenced(block.previous())) {
            block = block.next();
            continue;
        }
        // From the opening fence through the closing one (or the end of the document).
        QTextBlock last = block;
        while (fenced(last) && last.next().isValid()) {
            last = last.next();
        }
        const qreal end = last.next().isValid() ? layout->blockBoundingRect(last.next()).translated(offset).top()
                                                : layout->blockBoundingRect(last).translated(offset).bottom();
        painter.drawRoundedRect(QRectF(left, top - 4, width, end - top + 4), 6, 6);
        block = last.next();
    }
}
