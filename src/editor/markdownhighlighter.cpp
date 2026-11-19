// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdownhighlighter.h"
#include "markdowntable.h"
#include "mathrenderer.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QScreen>

#include <cmath>
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
const QRegularExpression kInlineDisplayMath(uR"((?<![\\$])\$\$(?=\S)([^$\n]+?)(?<=\S)\$\$)"_s);
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
    // Hidden markers must take no space: a 1pt font at 1% letter spacing (measured:
    // 18 characters < 0.5px). Don't go below 1pt: a size that rounds to 0px sends Qt
    // through every fallback font, which took seconds per file.
    m_hidden.setFontPointSize(1);
    m_hidden.setFontLetterSpacingType(QFont::PercentageSpacing);
    m_hidden.setFontLetterSpacing(1);

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

    m_mathError = m_math;
    m_mathError.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    m_mathError.setUnderlineColor(QColor(0xda, 0x44, 0x53)); // Breeze negative text

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
    } else if (first.userState() == MathBlock || first.userState() == MathBlockEnd) {
        // A $$ block shows as source in full while the cursor is anywhere in it.
        while (first.previous().isValid() && first.previous().userState() == MathBlock) {
            first = first.previous();
        }
        while (last.userState() == MathBlock && last.next().isValid()) {
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
    m_spans.clear();

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
        setCurrentBlockState(text.trimmed().endsWith(u"$$"_s) ? MathBlockEnd : MathBlock);
        // When the opening line shows the rendered formula, the rest of the source collapses.
        QTextBlock opening = currentBlock().previous();
        while (opening.previous().isValid() && opening.previous().userState() == MathBlock) {
            opening = opening.previous();
        }
        const auto *data = dynamic_cast<MathBlockData *>(opening.userData());
        const bool rendered = !active && data && !data->spans.isEmpty() && data->spans.first().display;
        apply(0, text.size(), rendered ? m_hidden : m_math);
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
        const bool singleLine = t.size() > 4 && t.endsWith(u"$$"_s);
        setCurrentBlockState(singleLine ? MathLine : MathBlock);
        if (!active) {
            int last = -1;
            const QString latex = singleLine ? t.mid(2, t.size() - 4).trimmed() : displayMathSource(currentBlock(), &last);
            if (!latex.isEmpty()) {
                const auto metrics = MathRenderer::instance().measure(latex, true, mathPixelSize(true));
                if (!metrics.ok) {
                    apply(0, text.size(), m_mathError);
                    return flush(formats);
                }
                // Collapse the source to one line tall enough for the formula; the editor paints it.
                constexpr qreal padding = 4;
                apply(0, text.size(), m_hidden);
                formats[0] = mathStrut(metrics.ascent + padding, metrics.descent + padding, 0, text.at(0));
                m_spans.append({0, int(text.size()), latex, true});
                return flush(formats);
            }
        }
        apply(0, text.size(), m_math);
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
                    formats[0] = tableRowStrut(currentBlock());
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

void MarkdownHighlighter::highlightInline(const QString &text, int from, FormatList &formats, bool active)
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

    // $$...$$ inside a paragraph renders in display style but stays in the line.
    for (const auto *pattern : {&kInlineDisplayMath, &kInlineMath}) {
        const bool displayStyle = pattern == &kInlineDisplayMath;
        for (auto it = pattern->globalMatch(text, from); it.hasNext();) {
            const auto m = it.next();
            if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
                continue;
            }
            const int start = m.capturedStart(0);
            const int length = m.capturedLength(0);
            take(start, length);
            if (active) {
                apply(start, length, m_math);
                continue;
            }
            const QString latex = (displayStyle ? u"\\displaystyle "_s : QString()) + m.captured(1);
            const auto metrics = MathRenderer::instance().measure(latex, false, mathPixelSize(false));
            if (!metrics.ok) {
                apply(start, length, m_mathError);
                continue;
            }
            // Hide the source; its first character reserves the formula's width and height.
            apply(start, length, m_hidden);
            formats[start] = mathStrut(metrics.ascent, metrics.descent, metrics.width, text.at(start));
            m_spans.append({start, length, latex, false});
        }
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

qreal MarkdownHighlighter::mathPixelSize(bool display) const
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
    const qreal points = document()->defaultFont().pointSizeF() > 0 ? document()->defaultFont().pointSizeF() : 11.0;
    return points * dpi / 72.0 * (display ? 1.15 : 1.0);
}

void MarkdownHighlighter::setTableLayout(TableLayout *layout)
{
    m_tableLayout = layout;
}

bool MarkdownHighlighter::setTableWidth(qreal width)
{
    width = std::floor(width);
    if (qFuzzyCompare(width, m_tableWidth)) {
        return false;
    }
    m_tableWidth = width;
    return true;
}

qreal MarkdownHighlighter::tableWidth() const
{
    return m_tableWidth;
}

qreal MarkdownHighlighter::pointSizeForLineHeight(qreal height) const
{
    const QFont base = document()->defaultFont();
    const QFontMetricsF metrics(base);
    const qreal lineHeight = (metrics.ascent() + metrics.descent()) * LineHeightPercent / 100.0;
    return qMax<qreal>(1.0, base.pointSizeF() * height / lineHeight);
}

QTextCharFormat MarkdownHighlighter::tableRowStrut(const QTextBlock &block) const
{
    QTextCharFormat format = m_strut;
    if (!m_tableLayout) {
        return format;
    }
    // Find the header, then read the table from the text (states below may be stale).
    QTextBlock header = block;
    while (header.userState() != TableHeader && header.previous().isValid() && isTableState(header.previous().userState())) {
        header = header.previous();
    }
    QStringList lines;
    int row = -1;
    int index = 0;
    for (QTextBlock b = header; b.isValid(); b = b.next(), ++index) {
        const QString text = b.text();
        if (index >= 2 && (text.trimmed().isEmpty() || !text.contains(u'|'))) {
            break;
        }
        lines.append(text);
        if (b == block) {
            row = index == 0 ? 0 : index - 1;
        }
    }
    const auto table = MarkdownTable::parse(lines);
    if (!table || row < 0) {
        return format;
    }
    const auto layout = m_tableLayout->compute(*table, m_tableWidth);
    if (row < layout.rowHeights.size()) {
        format.setFontPointSize(pointSizeForLineHeight(layout.rowHeights.at(row)));
    }
    return format;
}

QTextCharFormat MarkdownHighlighter::mathStrut(qreal ascent, qreal descent, qreal width, QChar character) const
{
    const QFont base = document()->defaultFont();
    const QFontMetricsF metrics(base);
    // Inline formulas sit on the baseline: the line needs their ascent, and their descent
    // may use the extra line spacing below the text. Display formulas are centered, so
    // only the total height matters (width == 0).
    const qreal fontHeight = metrics.ascent() + metrics.descent();
    const qreal below = metrics.descent() + fontHeight * (LineHeightPercent - 100) / 100.0;
    const qreal scale = width > 0 ? qMax<qreal>(1.0, qMax(ascent / metrics.ascent(), descent / below))
                                  : qMax<qreal>(1.0, (ascent + descent) / fontHeight);
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * scale);

    QTextCharFormat format;
    format.setForeground(Qt::transparent);
    format.setFontPointSize(font.pointSizeF());
    if (width > 0) {
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(width - QFontMetricsF(font).horizontalAdvance(character));
    }
    return format;
}

QString MarkdownHighlighter::displayMathSource(const QTextBlock &opening, int *lastBlock) const
{
    QString latex = opening.text().trimmed().mid(2);
    for (QTextBlock b = opening.next(); b.isValid(); b = b.next()) {
        const QString t = b.text().trimmed();
        if (t.endsWith(u"$$"_s)) {
            latex += u'\n' + t.chopped(2);
            *lastBlock = b.blockNumber();
            return latex.trimmed();
        }
        latex += u'\n' + b.text();
    }
    return {}; // never closed: leave it as source
}

void MarkdownHighlighter::flush(const FormatList &formats)
{
    if (m_spans.isEmpty()) {
        setCurrentBlockUserData(nullptr);
    } else {
        auto *data = new MathBlockData;
        data->spans = m_spans;
        setCurrentBlockUserData(data);
    }

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
