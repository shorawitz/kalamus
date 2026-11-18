// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextBlockUserData>
#include <QTextCursor>

/// A formula the editor paints over its (collapsed) source.
struct MathSpan {
    int start = 0; ///< in the block
    int length = 0;
    QString latex;
    bool display = false; ///< $$ display math (centered on its own line)
};

/// Per-block data the highlighter leaves for the editor's painter.
class MathBlockData : public QTextBlockUserData
{
public:
    QList<MathSpan> spans;
};

/**
 * Live-preview styling for Markdown source.
 *
 * The document text is always the Markdown file, byte for byte. This class only
 * changes how it looks: headings grow, **bold** turns bold, and the syntax markers
 * (#, **, `, [](url) ...) are hidden on every line except the one the cursor is on,
 * where they show dimmed so they can be edited.
 */
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /// Line height as a percentage of the font height; the extra space goes below each line.
    static constexpr int LineHeightPercent = 135;

    enum BlockState {
        Normal = 0,
        FencedBacktick = 1,
        FencedTilde = 2,
        MathBlock = 3,
        FrontMatter = 4,
        TableHeader = 5,
        TableDelimiter = 6,
        TableRow = 7,
        MathBlockEnd = 8, ///< the closing $$ line (MathBlock = opening and inner lines)
        MathLine = 9, ///< $$ ... $$ on one line
    };

    static bool isTableState(int state)
    {
        return state == TableHeader || state == TableDelimiter || state == TableRow;
    }

    explicit MarkdownHighlighter(QTextDocument *document);

    /// The block whose markers stay visible. Re-styles the old and the new block.
    /// Inside a table the whole table becomes active, so it shows as source.
    void setActiveBlock(const QTextBlock &block);
    bool isActive(const QTextBlock &block) const;

    /// Rebuild formats from the current font and palette (call on theme/font changes).
    void refreshStyles();

    /// Pixel size formulas are rendered at, matching the editor's text.
    qreal mathPixelSize(bool display) const;

protected:
    void highlightBlock(const QString &text) override;

private:
    using FormatList = QList<QTextCharFormat>;

    void highlightInline(const QString &text, int from, FormatList &formats, bool active);
    /// An invisible character that makes its line tall (and wide) enough for a formula.
    QTextCharFormat mathStrut(qreal ascent, qreal descent, qreal width, QChar character) const;
    /// For a rendered $$ block: the formula and whether it renders; empty if it doesn't.
    QString displayMathSource(const QTextBlock &opening, int *lastBlock) const;
    void flush(const FormatList &formats);
    void updateActiveRange();
    void scheduleRehighlight(const QTextBlock &block);

    // QTextCursors track their blocks through edits.
    QTextCursor m_cursorBlock; // block the text cursor is in
    QTextCursor m_activeFirst; // first block shown as source
    QTextCursor m_activeLast; // last block shown as source
    QList<MathSpan> m_spans; // collected while highlighting the current block

    QTextCharFormat m_hidden;
    QTextCharFormat m_strut; // invisible, but keeps the line at full height
    QTextCharFormat m_marker;
    QTextCharFormat m_heading[6];
    QTextCharFormat m_bold;
    QTextCharFormat m_italic;
    QTextCharFormat m_strike;
    QTextCharFormat m_code;
    QTextCharFormat m_codeBlock;
    QTextCharFormat m_fence;
    QTextCharFormat m_link;
    QTextCharFormat m_image;
    QTextCharFormat m_quote;
    QTextCharFormat m_listMarker;
    QTextCharFormat m_taskDone;
    QTextCharFormat m_math;
    QTextCharFormat m_mathError;
    QTextCharFormat m_table;
    QTextCharFormat m_tableHeader;
    QTextCharFormat m_meta;
};
