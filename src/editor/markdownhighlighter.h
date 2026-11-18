// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>

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
    enum BlockState {
        Normal = 0,
        FencedBacktick = 1,
        FencedTilde = 2,
        MathBlock = 3,
        FrontMatter = 4,
    };

    explicit MarkdownHighlighter(QTextDocument *document);

    /// The block whose markers stay visible. Re-styles the old and the new block.
    void setActiveBlock(const QTextBlock &block);

    /// Rebuild formats from the current font and palette (call on theme/font changes).
    void refreshStyles();

protected:
    void highlightBlock(const QString &text) override;

private:
    using FormatList = QList<QTextCharFormat>;

    void highlightInline(const QString &text, int from, FormatList &formats, bool active) const;
    void flush(const FormatList &formats);

    QTextCursor m_active; // tracks the active block through edits

    QTextCharFormat m_hidden;
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
    QTextCharFormat m_table;
    QTextCharFormat m_tableHeader;
    QTextCharFormat m_meta;
};
