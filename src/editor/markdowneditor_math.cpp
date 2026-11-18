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

void MarkdownEditor::paintMath(QPainter &painter) const
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
        const auto *data = dynamic_cast<MathBlockData *>(block.userData());
        if (!data || !block.layout()) {
            continue;
        }
        for (const MathSpan &span : data->spans) {
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
                // Centered on its own line.
                topLeft = QPointF(rect.left() + (rect.width() - metrics.width) / 2, rect.top() + line.y() + (line.height() - height) / 2);
            } else {
                // On the text baseline, where the hidden source starts.
                const qreal baseline = rect.top() + line.y() + line.ascent();
                topLeft = QPointF(rect.left() + line.cursorToX(span.start), baseline - metrics.ascent);
            }
            painter.drawImage(topLeft, image);
        }
    }
}
