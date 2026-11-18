// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdownhighlighter.h"
#include "markdowntable.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QTextDocument>

using namespace Qt::StringLiterals;

namespace
{
// Block-level patterns
const QRegularExpression kFenceOpen(uR"(^ {0,3}(`{3,}|~{3,}))"_s);
const QRegularExpression kFenceClose(uR"(^ {0,3}(`{3,}|~{3,})[ \t]*$)"_s);
const QRegularExpression kHeading(uR"(^ {0,3}(#{1,6})(?:[ \t]+|$))"_s);
const QRegularExpression kRule(uR"(^ {0,3}([-*_])(?:[ \t]*\1){2,}[ \t]*$)"_s);
const QRegularExpression kQuote(uR"(^ {0,3}(?:>[ \t]?)+)"_s);
const QRegularExpression kList(uR"(^([ \t]*)([-*+]|\d{1,9}[.)])([ \t]+)(\[[ xX]\][ \t]+)?)"_s);
const QRegularExpression kRefDefinition(uR"(^ {0,3}\[[^\]]+\]:[ \t])"_s);

// Inline patterns
const QRegularExpression kCodeSpan(uR"((`+)(.+?)(?<!`)\1(?!`))"_s);
const QRegularExpression kInlineMath(uR"((?<![\\$])\$(?=\S)([^$\n]+?)(?<=\S)\$(?![\d$]))"_s);
const QRegularExpression kImage(uR"(!\[([^\]]*)\]\(([^)\s]*)(?:[ \t]+"[^"]*")?\))"_s);
const QRegularExpression kLink(uR"((?<!!)\[([^\]]+)\]\(([^)\s]*)(?:[ \t]+"[^"]*")?\))"_s);
const QRegularExpression kAutoLink(uR"(<(https?://[^>\s]+)>|\bhttps?://[^\s<>()]+[^\s<>().,;:!?'")\]])"_s);
const QRegularExpression kBold(uR"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)"_s);
const QRegularExpression kItalic(uR"((?<![*\w])(\*|_)(?=[^\s*_])(.+?)(?<=[^\s*_])\1(?![*\w]))"_s);
const QRegularExpression kStrike(uR"(~~(?=\S)(.+?)(?<=\S)~~)"_s);

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    refreshStyles();
}

void MarkdownHighlighter::refreshStyles()
{
    const QPalette pal = QGuiApplication::palette();
    const QFont base = document()->defaultFont();
    const qreal size = base.pointSizeF() > 0 ? base.pointSizeF() : 11.0;
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QColor dim = pal.color(QPalette::PlaceholderText);

    m_hidden = QTextCharFormat();
    m_hidden.setForeground(Qt::transparent);
    // Tiny enough that hidden markers leave no visible gap.
    m_hidden.setFontPointSize(0.05);

    m_strut = QTextCharFormat();
    m_strut.setForeground(Qt::transparent);

    m_marker = QTextCharFormat();
    m_marker.setForeground(dim);

    static constexpr qreal scale[6] = {1.9, 1.55, 1.3, 1.15, 1.05, 1.0};
    for (int i = 0; i < 6; ++i) {
        m_heading[i] = QTextCharFormat();
        m_heading[i].setFontWeight(QFont::Bold);
        m_heading[i].setFontPointSize(size * scale[i]);
    }
    m_heading[5].setForeground(dim);

    m_bold = QTextCharFormat();
    m_bold.setFontWeight(QFont::Bold);

    m_italic = QTextCharFormat();
    m_italic.setFontItalic(true);

    m_strike = QTextCharFormat();
    m_strike.setFontStrikeOut(true);

    m_code = QTextCharFormat();
    m_code.setFontFamilies({fixed.family()});
    m_code.setBackground(pal.color(QPalette::AlternateBase));

    m_codeBlock = m_code;

    m_fence = QTextCharFormat();
    m_fence.setFontFamilies({fixed.family()});
    m_fence.setForeground(dim);

    m_link = QTextCharFormat();
    m_link.setForeground(pal.color(QPalette::Link));
    m_link.setFontUnderline(true);

    m_image = QTextCharFormat();
    m_image.setForeground(pal.color(QPalette::Link));
    m_image.setFontItalic(true);

    m_quote = QTextCharFormat();
    m_quote.setForeground(dim);
    m_quote.setFontItalic(true);

    m_listMarker = QTextCharFormat();
    m_listMarker.setForeground(pal.color(QPalette::Highlight));
    m_listMarker.setFontWeight(QFont::Bold);

    m_taskDone = QTextCharFormat();
    m_taskDone.setForeground(dim);
    m_taskDone.setFontStrikeOut(true);

    m_math = QTextCharFormat();
    m_math.setFontFamilies({fixed.family()});
    m_math.setForeground(pal.color(QPalette::LinkVisited));

    m_table = QTextCharFormat();
    m_table.setFontFamilies({fixed.family()});

    m_tableHeader = m_table;
    m_tableHeader.setFontWeight(QFont::Bold);

    m_meta = QTextCharFormat();
    m_meta.setFontFamilies({fixed.family()});
    m_meta.setForeground(dim);

    rehighlight();
}

void MarkdownHighlighter::setActiveBlock(const QTextBlock &block)
{
    m_cursorBlock = QTextCursor(block);
    updateActiveRange();
}

bool MarkdownHighlighter::isActive(const QTextBlock &block) const
{
    if (m_activeFirst.isNull() || m_activeLast.isNull()) {
        return false;
    }
    const int n = block.blockNumber();
    return n >= m_activeFirst.block().blockNumber() && n <= m_activeLast.block().blockNumber();
}

void MarkdownHighlighter::updateActiveRange()
{
    if (m_cursorBlock.isNull()) {
        return;
    }
    QTextBlock first = m_cursorBlock.block();
    QTextBlock last = first;
    if (isTableState(first.userState())) {
        while (first.userState() != TableHeader && first.previous().isValid() && isTableState(first.previous().userState())) {
            first = first.previous();
        }
        while (last.next().isValid() && (last.next().userState() == TableDelimiter || last.next().userState() == TableRow)) {
            last = last.next();
        }
    }

    const QTextBlock oldFirst = m_activeFirst.isNull() ? QTextBlock() : m_activeFirst.block();
    const QTextBlock oldLast = m_activeLast.isNull() ? QTextBlock() : m_activeLast.block();
    if (oldFirst == first && oldLast == last) {
        return;
    }
    m_activeFirst = QTextCursor(first);
    m_activeLast = QTextCursor(last);

    auto restyle = [this](QTextBlock from, const QTextBlock &to) {
        if (!from.isValid() || !to.isValid()) {
            return;
        }
        for (; from.isValid() && from.blockNumber() <= to.blockNumber(); from = from.next()) {
            rehighlightBlock(from);
        }
    };
    restyle(oldFirst, oldLast);
    restyle(first, last);
}

void MarkdownHighlighter::scheduleRehighlight(const QTextBlock &block)
{
    if (!block.isValid()) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, tracked = QTextCursor(block)] {
            if (!tracked.isNull()) {
                rehighlightBlock(tracked.block());
                updateActiveRange(); // the block may have just become (or stopped being) a table
            }
        },
        Qt::QueuedConnection);
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    const int prev = qMax(previousBlockState(), 0);
    const bool active = isActive(currentBlock());
    FormatList formats(text.size());

    auto apply = [&](int start, int length, const QTextCharFormat &fmt) {
        const int end = qMin<int>(text.size(), start + length);
        for (int i = qMax(0, start); i < end; ++i) {
            formats[i].merge(fmt);
        }
    };
    auto syntax = [&](int start, int length) {
        apply(start, length, active ? m_marker : m_hidden);
    };

    setCurrentBlockState(Normal);

    // YAML front matter at the very top of the file.
    if (currentBlock().blockNumber() == 0 && text == u"---"_s) {
        apply(0, text.size(), m_meta);
        setCurrentBlockState(FrontMatter);
        return flush(formats);
    }
    if (prev == FrontMatter) {
        apply(0, text.size(), m_meta);
        if (text != u"---"_s && text != u"..."_s) {
            setCurrentBlockState(FrontMatter);
        }
        return flush(formats);
    }

    // Inside a fenced code block.
    if (prev == FencedBacktick || prev == FencedTilde) {
        const auto m = kFenceClose.match(text);
        const QChar fenceChar = prev == FencedBacktick ? u'`' : u'~';
        if (m.hasMatch() && m.captured(1).at(0) == fenceChar) {
            apply(0, text.size(), m_fence);
        } else {
            apply(0, text.size(), m_codeBlock);
            setCurrentBlockState(prev);
        }
        return flush(formats);
    }

    // Inside a $$ math block.
    if (prev == MathBlock) {
        apply(0, text.size(), m_math);
        if (text.trimmed() != u"$$"_s) {
            setCurrentBlockState(MathBlock);
        }
        return flush(formats);
    }

    // A table header is decided by looking at the next line, which QSyntaxHighlighter
    // does not re-check when that line changes. Re-style the line above when needed.
    const bool delimiterRow = MarkdownTable::isDelimiterRow(text);
    const QTextBlock previous = currentBlock().previous();
    if (prev == TableHeader && !delimiterRow) {
        scheduleRehighlight(previous);
    } else if (delimiterRow && prev != TableHeader && prev != TableDelimiter && prev != TableRow && previous.text().contains(u'|')) {
        scheduleRehighlight(previous);
    }

    if (const auto m = kFenceOpen.match(text); m.hasMatch()) {
        apply(0, text.size(), m_fence);
        setCurrentBlockState(m.captured(1).at(0) == u'`' ? FencedBacktick : FencedTilde);
        return flush(formats);
    }

    if (const QString t = text.trimmed(); t.startsWith(u"$$"_s)) {
        apply(0, text.size(), m_math);
        const bool singleLine = t.size() > 4 && t.endsWith(u"$$"_s);
        if (!singleLine) {
            setCurrentBlockState(MathBlock);
        }
        return flush(formats);
    }

    if (const auto m = kHeading.match(text); m.hasMatch()) {
        const int level = m.capturedLength(1);
        apply(0, text.size(), m_heading[level - 1]);
        syntax(0, m.capturedEnd(0));
        highlightInline(text, m.capturedEnd(0), formats, active);
        return flush(formats);
    }

    if (kRule.match(text).hasMatch()) {
        apply(0, text.size(), m_marker);
        return flush(formats);
    }

    if (kRefDefinition.match(text).hasMatch()) {
        apply(0, text.size(), m_marker);
        return flush(formats);
    }

    if (text.contains(u'|')) {
        int tableState = Normal;
        if (prev == TableHeader && delimiterRow) {
            tableState = TableDelimiter;
        } else if ((prev == TableDelimiter || prev == TableRow) && !text.trimmed().isEmpty()) {
            tableState = TableRow;
        } else if (const QTextBlock next = currentBlock().next(); next.isValid() && MarkdownTable::isDelimiterRow(next.text())
                   && MarkdownTable::splitRow(next.text()).size() == MarkdownTable::splitRow(text).size()) {
            tableState = TableHeader;
        }

        if (tableState != Normal) {
            setCurrentBlockState(tableState);
            if (!active) {
                // The editor paints this table as a grid. Collapse the source; rows keep
                // one invisible full-size character so each grid row gets a line's height.
                apply(0, text.size(), m_hidden);
                if (tableState != TableDelimiter) {
                    formats[0] = m_strut;
                }
                return flush(formats);
            }
            apply(0, text.size(), tableState == TableHeader ? m_tableHeader : m_table);
            if (tableState == TableDelimiter) {
                apply(0, text.size(), m_marker);
                return flush(formats);
            }
            for (int i = 0; i < text.size(); ++i) {
                if (text.at(i) == u'|' && (i == 0 || text.at(i - 1) != u'\\')) {
                    apply(i, 1, m_marker);
                }
            }
            highlightInline(text, 0, formats, active);
            return flush(formats);
        }
    }

    int offset = 0;
    if (const auto m = kQuote.match(text); m.hasMatch()) {
        offset = m.capturedEnd(0);
        apply(0, offset, m_marker);
        apply(offset, text.size() - offset, m_quote);
    }

    if (const auto m = kList.match(text, offset, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        m.hasMatch()) {
        apply(m.capturedStart(2), m.capturedLength(2), m_listMarker);
        if (m.capturedLength(4) > 0) {
            apply(m.capturedStart(4), 3, m_listMarker);
            if (m.captured(4).at(1) != u' ') {
                apply(m.capturedEnd(4), text.size() - m.capturedEnd(4), m_taskDone);
            }
        }
        offset = m.capturedEnd(0);
    }

    highlightInline(text, offset, formats, active);
    flush(formats);
}

void MarkdownHighlighter::highlightInline(const QString &text, int from, FormatList &formats, bool active) const
{
    QList<bool> taken(text.size(), false);

    auto apply = [&](int start, int length, const QTextCharFormat &fmt) {
        const int end = qMin<int>(text.size(), start + length);
        for (int i = qMax(0, start); i < end; ++i) {
            formats[i].merge(fmt);
        }
    };
    auto syntax = [&](int start, int length) {
        apply(start, length, active ? m_marker : m_hidden);
    };
    auto isFree = [&](int start, int length) {
        for (int i = start; i < start + length; ++i) {
            if (taken[i]) {
                return false;
            }
        }
        return true;
    };
    auto take = [&](int start, int length) {
        for (int i = start; i < start + length; ++i) {
            taken[i] = true;
        }
    };

    // Code spans and math first: nothing inside them is Markdown.
    for (auto it = kCodeSpan.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const int ticks = m.capturedLength(1);
        syntax(m.capturedStart(0), ticks);
        apply(m.capturedStart(2), m.capturedLength(2), m_code);
        syntax(m.capturedEnd(2), ticks);
        take(m.capturedStart(0), m.capturedLength(0));
    }

    for (auto it = kInlineMath.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        apply(m.capturedStart(0), m.capturedLength(0), m_math);
        take(m.capturedStart(0), m.capturedLength(0));
    }

    for (auto it = kImage.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        syntax(m.capturedStart(0), 2); // ![
        apply(m.capturedStart(1), m.capturedLength(1), m_image);
        syntax(m.capturedEnd(1), m.capturedEnd(0) - m.capturedEnd(1)); // ](url)
        take(m.capturedStart(0), m.capturedLength(0));
    }

    for (auto it = kLink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        syntax(m.capturedStart(0), 1); // [
        apply(m.capturedStart(1), m.capturedLength(1), m_link);
        syntax(m.capturedEnd(1), m.capturedEnd(0) - m.capturedEnd(1)); // ](url)
        // The link text may still carry emphasis, so only the URL part is taken.
        take(m.capturedEnd(1), m.capturedEnd(0) - m.capturedEnd(1));
    }

    for (auto it = kAutoLink.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        apply(m.capturedStart(0), m.capturedLength(0), m_link);
        take(m.capturedStart(0), m.capturedLength(0));
    }

    struct Emphasis {
        const QRegularExpression *pattern;
        const QTextCharFormat *format;
        int markerGroup; // capture group holding the opening marker, 0 = fixed width
        int fixedMarker;
        int contentGroup;
    };
    const Emphasis emphasis[] = {
        {&kBold, &m_bold, 1, 0, 2},
        {&kStrike, &m_strike, 0, 2, 1},
        {&kItalic, &m_italic, 1, 0, 2},
    };
    for (const auto &e : emphasis) {
        for (auto it = e.pattern->globalMatch(text, from); it.hasNext();) {
            const auto m = it.next();
            const int marker = e.markerGroup ? m.capturedLength(e.markerGroup) : e.fixedMarker;
            if (!isFree(m.capturedStart(0), marker) || !isFree(m.capturedEnd(0) - marker, marker)) {
                continue;
            }
            syntax(m.capturedStart(0), marker);
            apply(m.capturedStart(e.contentGroup), m.capturedLength(e.contentGroup), *e.format);
            syntax(m.capturedEnd(0) - marker, marker);
            take(m.capturedStart(0), marker);
            take(m.capturedEnd(0) - marker, marker);
        }
    }
}

void MarkdownHighlighter::flush(const FormatList &formats)
{
    const int n = formats.size();
    int i = 0;
    while (i < n) {
        int j = i + 1;
        while (j < n && formats[j] == formats[i]) {
            ++j;
        }
        if (formats[i] != QTextCharFormat()) {
            setFormat(i, j - i, formats[i]);
        }
        i = j;
    }
}
