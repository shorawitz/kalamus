// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "markdowntable.h"

#include <QFont>
#include <QHash>
#include <QList>

#include <memory>

class QTextDocument;
class SpellChecker;

/**
 * Column widths and row heights for a table drawn as a grid. Short columns keep their
 * natural width; long-text columns share the rest of the width and wrap their text,
 * so rows grow taller instead of text being cut off.
 *
 * Shared by the highlighter (which sizes each source line to its row's height) and the
 * editor (which paints the grid), so both always agree.
 */
class TableLayout
{
public:
    static constexpr qreal Padding = 10; ///< left/right inside a cell
    static constexpr qreal VerticalPadding = 5; ///< top/bottom inside a cell
    static constexpr qreal MinColumnWidth = 72;

    struct Result {
        QList<qreal> columnWidths;
        QList<qreal> rowHeights; ///< header first, then body rows
        qreal width = 0;
    };

    void setFont(const QFont &font);
    /// Underline misspelled words in cells (call clear() after spelling settings change).
    void setSpellChecker(const SpellChecker *checker);
    const QFont &font() const;

    /// @p availableWidth <= 0 means "unlimited" (natural widths).
    Result compute(const MarkdownTable &table, qreal availableWidth);

    /// A rendered cell, laid out at @p width with @p alignment, ready to draw.
    QTextDocument *cell(const QString &text, bool header, qreal width, Qt::Alignment alignment);

    void clear();

private:
    QTextDocument *document(const QString &text, bool header);

    void underlineMisspelled(QTextDocument *doc) const;

    QFont m_font;
    const SpellChecker *m_spellChecker = nullptr;
    QHash<QString, std::shared_ptr<QTextDocument>> m_documents;
    QHash<QString, Result> m_results;
};
