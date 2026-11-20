// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "imagecache.h"
#include "tablelayout.h"

#include <KSyntaxHighlighting/State>

#include <QSizeF>
#include <QTextBlockUserData>
#include <QUrl>
#include <QTextCursor>

class SpellChecker;

/// A formula the editor paints over its (collapsed) source.
struct MathSpan {
    int start = 0; ///< in the block
    int length = 0;
    QString latex;
    bool display = false; ///< $$ display math (centered on its own line)
};

/// An image the editor paints over its (collapsed) source.
struct ImageSpan {
    int start = 0; ///< in the block
    int length = 0;
    QUrl url; ///< resolved location
    QSizeF size; ///< as displayed, in logical pixels
    bool tall = false; ///< taller than a text line: sits on the line's bottom, not the baseline
};

/// Per-block data the highlighter leaves for the editor's painter.
class BlockRenderData : public QTextBlockUserData
{
public:
    QList<MathSpan> math;
    QList<ImageSpan> images;
    QString codeLanguage; ///< set on a fenced code block's lines ("" outside; " " if unnamed)
    KSyntaxHighlighting::State codeState; ///< syntax state at the end of this line
    size_t paragraphKey = 0; ///< what the line's paragraph neighbours depend on (see flush())
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
        SetextHeading1 = 10, ///< heading text with === below
        SetextHeading2 = 11, ///< heading text with --- below
        SetextUnderline = 12, ///< the === or --- line
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
    /// Text that becomes a heading when === or --- follows it.
    static bool isSetextCandidate(const QString &text);
    /// 1 or 2 when @p block is one of the text lines of a setext heading (the run of
    /// candidate lines just above an === or --- line), otherwise 0.
    static int setextLevel(const QTextBlock &block);

    /// Rebuild formats from the current font and palette (call on theme/font changes).
    void refreshStyles();
    /// The family used for code, math source and table source (size follows the text).
    void setCodeFont(const QFont &font);

    /// Pixel size formulas are rendered at, matching the editor's text.
    qreal mathPixelSize(bool display) const;

    /// Tables drawn as grids: the shared layout engine.
    void setTableLayout(TableLayout *layout);
    /// Where images come from; without one, images show as their alt text.
    void setImageCache(ImageCache *cache);
    /// Underlines misspelled words in prose; without one, nothing is checked.
    void setSpellChecker(SpellChecker *checker);
    /// The width tables and images may use.
    /// Returns true when the width changed (the caller then re-styles tables).
    bool setContentWidth(qreal width);
    qreal contentWidth() const;

Q_SIGNALS:
    /// Some blocks were re-styled outside an edit (queued follow-ups).
    void restyled();

protected:
    void highlightBlock(const QString &text) override;

private:
    using FormatList = QList<QTextCharFormat>;

    /// Spell checking only looks at @p checkFrom .. @p checkTo (-1: to the end).
    void highlightInline(const QString &text, int from, FormatList &formats, bool active, int checkFrom = 0, int checkTo = -1);
    /// A paragraph line: inline styles see the whole paragraph, so they can cross lines.
    void highlightParagraphLine(const QString &text, int offset, FormatList &formats, bool active);
    void queueParagraphRestyle(const QTextBlock &block);
    void restyleEditedParagraphs();
    /// An invisible character that makes its line tall (and wide) enough for a formula.
    /// With @p fitTotalHeight the line is sized by ascent + descent only (for centered or
    /// bottom-aligned objects); otherwise ascent and descent must each fit.
    QTextCharFormat mathStrut(qreal ascent, qreal descent, qreal width, QChar character, bool fitTotalHeight = false) const;
    /// An invisible character that makes a table row's source line as tall as the grid row.
    QTextCharFormat tableRowStrut(const QTextBlock &block) const;
    /// Font size whose line (with our line spacing) is @p height pixels tall.
    qreal pointSizeForLineHeight(qreal height) const;
    /// For a rendered $$ block: the formula and whether it renders; empty if it doesn't.
    QString displayMathSource(const QTextBlock &opening, int *lastBlock) const;
    void flush(const FormatList &formats);
    void updateActiveRange();
    void scheduleRehighlight(const QTextBlock &block);

    // QTextCursors track their blocks through edits.
    QTextCursor m_cursorBlock; // block the text cursor is in
    QTextCursor m_activeFirst; // first block shown as source
    QTextCursor m_activeLast; // last block shown as source
    QList<MathSpan> m_mathSpans; // collected while highlighting the current block
    QList<ImageSpan> m_imageSpans;
    QString m_codeLanguage;
    KSyntaxHighlighting::State m_codeState;
    TableLayout *m_tableLayout = nullptr;
    ImageCache *m_imageCache = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    qreal m_contentWidth = 0;
    QList<QTextCursor> m_pendingParagraphs; // around recent edits, re-styled once queued work runs
    bool m_paragraphRestyleQueued = false;
    bool m_restylingParagraphs = false;
    int m_blockCount = 1;
    QFont m_codeFont;

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
    QTextCharFormat m_imageError;
    QTextCharFormat m_quote;
    QTextCharFormat m_listMarker;
    QTextCharFormat m_taskDone;
    QTextCharFormat m_math;
    QTextCharFormat m_mathError;
    QTextCharFormat m_table;
    QTextCharFormat m_tableHeader;
    QTextCharFormat m_meta;
    QTextCharFormat m_misspelled;
};
