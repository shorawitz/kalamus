// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tablelayout.h"

#include <QTextDocument>
#include <QTextOption>

#include <cmath>

using namespace Qt::StringLiterals;

namespace
{
constexpr int kCacheLimit = 2000;
}

void TableLayout::setFont(const QFont &font)
{
    if (font != m_font) {
        m_font = font;
        clear();
    }
}

const QFont &TableLayout::font() const
{
    return m_font;
}

void TableLayout::clear()
{
    m_documents.clear();
    m_results.clear();
}

QTextDocument *TableLayout::document(const QString &text, bool header)
{
    const QString key = (header ? u"h:"_s : u"b:"_s) + text;
    if (const auto it = m_documents.constFind(key); it != m_documents.cend()) {
        return it->get();
    }
    if (m_documents.size() > kCacheLimit) {
        m_documents.clear();
    }
    auto doc = std::make_shared<QTextDocument>();
    QFont f = m_font;
    f.setBold(header);
    doc->setDefaultFont(f);
    doc->setDocumentMargin(0);
    doc->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    m_documents.insert(key, doc);
    return doc.get();
}

QTextDocument *TableLayout::cell(const QString &text, bool header, qreal width, Qt::Alignment alignment)
{
    QTextDocument *doc = document(text, header);
    QTextOption option = doc->defaultTextOption();
    option.setAlignment(alignment);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc->setDefaultTextOption(option);
    doc->setTextWidth(qMax<qreal>(1, width));
    return doc;
}

TableLayout::Result TableLayout::compute(const MarkdownTable &table, qreal availableWidth)
{
    QString key = QString::number(qRound(availableWidth)) + u'\x1f';
    for (int r = 0; r < table.rowCount(); ++r) {
        for (int c = 0; c < table.columnCount(); ++c) {
            key += table.cell(r, c) + u'\x1e';
        }
        key += u'\x1f';
    }
    if (const auto it = m_results.constFind(key); it != m_results.cend()) {
        return *it;
    }
    if (m_results.size() > kCacheLimit) {
        m_results.clear();
    }

    const int columns = table.columnCount();
    QList<qreal> natural(columns, MinColumnWidth);
    for (int r = 0; r < table.rowCount(); ++r) {
        for (int c = 0; c < columns; ++c) {
            QTextDocument *doc = document(table.cell(r, c), r == 0);
            doc->setTextWidth(-1);
            natural[c] = qMax(natural[c], std::ceil(doc->idealWidth()) + 2 * Padding);
        }
    }

    Result result;
    result.columnWidths = natural;
    qreal total = 0;
    for (qreal w : std::as_const(natural)) {
        total += w;
    }
    if (availableWidth > 0 && total > availableWidth) {
        // Water-filling: columns narrower than an even share keep their natural width;
        // the remaining columns split what is left and wrap their text.
        QList<bool> fixed(columns, false);
        qreal remaining = availableWidth;
        int flexible = columns;
        bool changed = true;
        while (changed && flexible > 0) {
            changed = false;
            const qreal share = remaining / flexible;
            for (int c = 0; c < columns; ++c) {
                if (!fixed[c] && natural[c] <= share) {
                    fixed[c] = true;
                    remaining -= natural[c];
                    --flexible;
                    changed = true;
                }
            }
        }
        const qreal share = flexible > 0 ? remaining / flexible : 0;
        for (int c = 0; c < columns; ++c) {
            if (!fixed[c]) {
                result.columnWidths[c] = qMax(MinColumnWidth, std::floor(share));
            }
        }
    }

    for (qreal w : std::as_const(result.columnWidths)) {
        result.width += w;
    }
    for (int r = 0; r < table.rowCount(); ++r) {
        qreal height = 0;
        for (int c = 0; c < columns; ++c) {
            QTextDocument *doc = cell(table.cell(r, c), r == 0, result.columnWidths[c] - 2 * Padding, Qt::AlignLeft);
            height = qMax(height, doc->size().height());
        }
        result.rowHeights.append(std::ceil(height + 2 * VerticalPadding));
    }
    m_results.insert(key, result);
    return result;
}
