// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdownhighlighter.h"
#include "codehighlighter.h"
#include "markdowntable.h"
#include "mathrenderer.h"
#include "spellchecker.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QScreen>

#include <cmath>
#include <utility>
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

// Inline patterns. They run on a whole paragraph on a whole paragraph, so a span may cross a line break ([\s\S]).
const QRegularExpression kCodeSpan(uR"((`+)([\s\S]+?)(?<!`)\1(?!`))"_s);
const QRegularExpression kInlineDisplayMath(uR"((?<![\\$])\$\$(?=\S)([^$\n]+?)(?<=\S)\$\$)"_s);
const QRegularExpression kInlineMath(uR"((?<![\\$])\$(?=\S)([^$\n]+?)(?<=\S)\$(?![\d$]))"_s);
// Destinations may be <in angle brackets> (then they can contain spaces).
const QRegularExpression kImage(uR"(!\[([^\]\n]*)\]\((<[^>\n]*>|[^)\s]*)(?:[ \t]+"[^"]*")?\))"_s);
// [![alt](image)](link), as in README badges.
const QRegularExpression kLinkedImage(uR"(\[!\[([^\]\n]*)\]\((<[^>\n]*>|[^)\s]*)(?:[ \t]+"[^"]*")?\)\]\(([^)\s]*)(?:[ \t]+"[^"]*")?\))"_s);
const QRegularExpression kLink(uR"((?<!!)\[([^\]]+)\]\(([^)\s]*)(?:[ \t]+"[^"]*")?\))"_s);
const QRegularExpression kAutoLink(uR"(<(https?://[^>\s]+)>|\bhttps?://[^\s<>()]+[^\s<>().,;:!?'")\]])"_s);
// Bold and strike stop at the next same marker, so an unclosed ** cannot swallow the next pair.
const QRegularExpression kBold(uR"((\*\*|__)(?=\S)((?:(?!\1)[\s\S])+?)(?<=\S)\1)"_s);
const QRegularExpression kItalic(uR"((?<![*\w])(\*|_)(?=[^\s*_])([\s\S]+?)(?<=[^\s*_])\1(?![*\w]))"_s);
const QRegularExpression kStrike(uR"(~~(?=\S)((?:(?!~~)[\s\S])+?)(?<=\S)~~)"_s);

// Paragraphs: emphasis and code spans may run across the lines of one paragraph.
// How far up and down a line looks for the rest of its paragraph. Spans longer than this are
// rare, and the limit keeps typing fast in a very long paragraph.
constexpr int kParagraphReach = 20;

struct LineInfo {
    enum Kind {
        Blank,
        Breaker, ///< a line that is never part of a paragraph (heading, fence, rule, table ...)
        ItemStart, ///< a list item: starts a paragraph that later lines may continue
        Text, ///< plain text: continues the paragraph above, if any
    };
    Kind kind = Text;
    int quoteDepth = 0;
    int contentStart = 0; ///< after any > and list marker
};

/// @p styled: the block's state is up to date (the blocks above the one being styled).
LineInfo classify(const QTextBlock &block, bool styled)
{
    const QString text = block.text();
    LineInfo info;
    if (text.trimmed().isEmpty()) {
        info.kind = LineInfo::Blank;
        return info;
    }
    if ((styled && block.userState() > 0) || (block.blockNumber() == 0 && text == u"---"_s) || kFenceOpen.match(text).hasMatch()
        || text.trimmed().startsWith(u"$$"_s) || kHeading.match(text).hasMatch() || kRule.match(text).hasMatch()
        || kRefDefinition.match(text).hasMatch()
        || (text.contains(u'|') && (MarkdownTable::isDelimiterRow(text) || MarkdownTable::isDelimiterRow(block.next().text())))) {
        info.kind = LineInfo::Breaker;
        return info;
    }
    if (const auto m = kQuote.match(text); m.hasMatch()) {
        info.quoteDepth = int(m.captured(0).count(u'>'));
        info.contentStart = m.capturedEnd(0);
        if (info.contentStart == text.size()) {
            info.kind = LineInfo::Blank; // ">" alone separates paragraphs in a quote
            return info;
        }
    }
    if (const auto m = kList.match(text, info.contentStart, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        m.hasMatch()) {
        info.kind = LineInfo::ItemStart;
        info.contentStart = m.capturedEnd(0);
    }
    // A continuation line's indent is not part of the text (it would show inside a code span).
    while (info.contentStart < text.size() && (text.at(info.contentStart) == u' ' || text.at(info.contentStart) == u'\t')) {
        ++info.contentStart;
    }
    return info;
}

/// Whether @p lower continues the paragraph of the line above it, @p upper.
bool continues(const LineInfo &upper, const LineInfo &lower)
{
    // A line without > after a quote still belongs to the quote's paragraph ("lazy" continuation).
    return (upper.kind == LineInfo::Text || upper.kind == LineInfo::ItemStart) && lower.kind == LineInfo::Text
        && (lower.quoteDepth == upper.quoteDepth || lower.quoteDepth == 0);
}

/// What other lines of a paragraph can see of this line: its kind and its emphasis and code
/// markers with the characters beside them. Typing ordinary words leaves it unchanged.
size_t paragraphKey(const QString &text, const LineInfo &info)
{
    auto kindOf = [&](qsizetype i) {
        if (i < 0 || i >= text.size() || text.at(i).isSpace()) {
            return u' ';
        }
        return text.at(i).isLetterOrNumber() || text.at(i) == u'_' ? u'w' : u'.';
    };
    QString key = QString::number(int(info.kind)) + QString::number(info.quoteDepth) + QString::number(text.count(u"://"_s));
    for (qsizetype i = info.contentStart; i < text.size(); ++i) {
        if (QStringView(u"*_~`$[]()!<>\\").contains(text.at(i))) {
            key += kindOf(i - 1);
            key += text.at(i);
            key += kindOf(i + 1);
        }
    }
    return qHash(key);
}

/// The first and last line of the paragraph @p block is in (both @p block when it stands alone).
/// @p contentStarts, if given, gets where each line's text starts (after > and list markers).
void paragraphExtent(const QTextBlock &block, QTextBlock *first, QTextBlock *last, QList<int> *contentStarts = nullptr)
{
    *first = *last = block;
    const LineInfo self = classify(block, false);
    if (contentStarts) {
        *contentStarts = {self.contentStart};
    }
    if (self.kind == LineInfo::Blank || self.kind == LineInfo::Breaker) {
        return;
    }
    LineInfo below = self;
    for (int n = 0; n < kParagraphReach; ++n) {
        const QTextBlock above = first->previous();
        if (!above.isValid()) {
            break;
        }
        const LineInfo info = classify(above, true);
        if (!continues(info, below)) {
            break;
        }
        if (contentStarts) {
            contentStarts->prepend(info.contentStart);
        }
        *first = above;
        below = info;
    }
    LineInfo above = self;
    for (int n = 0; n < kParagraphReach; ++n) {
        const QTextBlock next = last->next();
        if (!next.isValid()) {
            break;
        }
        const LineInfo info = classify(next, false);
        if (!continues(above, info)) {
            break;
        }
        if (contentStarts) {
            contentStarts->append(info.contentStart);
        }
        *last = next;
        above = info;
    }
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    refreshStyles();
    m_blockCount = document->blockCount();

    // An edit can change how the other lines of its paragraph look: "**" typed on one line
    // closes a bold opened on another, or a new blank line splits a paragraph in two.
    // QSyntaxHighlighter only re-styles the edited lines, so re-style their paragraphs too.
    // Changes inside a line are caught in flush(); here, line breaks added or removed.
    connect(document, &QTextDocument::contentsChange, this, [this](int position, int, int added) {
        if (this->document()->blockCount() == m_blockCount) {
            return;
        }
        m_blockCount = this->document()->blockCount();
        const QTextBlock first = this->document()->findBlock(position);
        QTextBlock last = this->document()->findBlock(position + added);
        if (!last.isValid()) {
            last = this->document()->lastBlock();
        }
        if (first.isValid()) {
            queueParagraphRestyle(first.previous().isValid() ? first.previous() : first);
            queueParagraphRestyle(last.next().isValid() ? last.next() : last);
        }
    });
}

void MarkdownHighlighter::queueParagraphRestyle(const QTextBlock &block)
{
    m_pendingParagraphs.append(QTextCursor(block));
    if (!m_paragraphRestyleQueued) {
        m_paragraphRestyleQueued = true;
        QMetaObject::invokeMethod(this, &MarkdownHighlighter::restyleEditedParagraphs, Qt::QueuedConnection);
    }
}

void MarkdownHighlighter::restyleEditedParagraphs()
{
    m_paragraphRestyleQueued = false;
    const auto pending = std::exchange(m_pendingParagraphs, {});
    if (pending.isEmpty()) {
        return;
    }
    // From the line before the edit to the line after it, widened to whole paragraphs.
    int from = -1;
    int to = -1;
    for (const QTextCursor &cursor : pending) {
        if (cursor.isNull()) {
            continue;
        }
        QTextBlock first;
        QTextBlock last;
        paragraphExtent(cursor.block(), &first, &last);
        from = from < 0 ? first.blockNumber() : qMin(from, first.blockNumber());
        to = qMax(to, last.blockNumber());
    }
    if (from < 0) {
        return;
    }
    bool changed = false;
    m_restylingParagraphs = true;
    for (QTextBlock b = document()->findBlockByNumber(from); b.isValid() && b.blockNumber() <= to; b = b.next()) {
        const auto before = b.layout()->formats();
        rehighlightBlock(b);
        changed = changed || b.layout()->formats() != before;
    }
    m_restylingParagraphs = false;
    if (changed) {
        Q_EMIT restyled();
    }
}

void MarkdownHighlighter::refreshStyles()
{
    const QPalette pal = QGuiApplication::palette();
    const QFont base = document()->defaultFont();
    const qreal size = base.pointSizeF() > 0 ? base.pointSizeF() : 11.0;
    const QFont fixed = m_codeFont.family().isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : m_codeFont;
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

    // Code blocks get a full-width background from the editor, so no per-character one.
    m_codeBlock = QTextCharFormat();
    m_codeBlock.setFontFamilies({fixed.family()});
    CodeHighlighter::instance().setDarkTheme(pal.color(QPalette::Base).lightness() < 128);

    m_fence = QTextCharFormat();
    m_fence.setFontFamilies({fixed.family()});
    m_fence.setForeground(dim);

    m_link = QTextCharFormat();
    m_link.setForeground(pal.color(QPalette::Link));
    m_link.setFontUnderline(true);

    m_image = QTextCharFormat();
    m_image.setForeground(pal.color(QPalette::Link));
    m_image.setFontItalic(true);

    m_imageError = m_image;
    m_imageError.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    m_imageError.setUnderlineColor(QColor(0xda, 0x44, 0x53));

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

    m_misspelled = QTextCharFormat();
    m_misspelled.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    m_misspelled.setUnderlineColor(QColor(0xda, 0x44, 0x53)); // Breeze "negative" red

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
    } else if (first.userState() == FencedBacktick || first.userState() == FencedTilde
               || (first.previous().isValid() && (first.previous().userState() == FencedBacktick || first.previous().userState() == FencedTilde))) {
        // A fenced code block shows its fences while the cursor is anywhere in it.
        while (first.previous().isValid() && (first.previous().userState() == FencedBacktick || first.previous().userState() == FencedTilde)) {
            first = first.previous();
        }
        while ((last.userState() == FencedBacktick || last.userState() == FencedTilde) && last.next().isValid()) {
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
                Q_EMIT restyled();
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
    m_mathSpans.clear();
    m_imageSpans.clear();
    m_codeLanguage.clear();
    m_codeState = {};

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
            apply(0, text.size(), active ? m_fence : m_hidden);
            return flush(formats);
        }
        setCurrentBlockState(prev);
        apply(0, text.size(), m_codeBlock);

        // Syntax colours, carrying the highlighter's state from line to line.
        const auto *above = dynamic_cast<BlockRenderData *>(currentBlock().previous().userData());
        if (above && !above->codeLanguage.isEmpty()) {
            const auto definition = CodeHighlighter::instance().definitionFor(above->codeLanguage);
            if (definition.isValid()) {
                QList<CodeHighlighter::Span> spans;
                m_codeState = CodeHighlighter::instance().highlight(text, above->codeState, definition, &spans);
                m_codeLanguage = above->codeLanguage;
                for (const auto &span : std::as_const(spans)) {
                    apply(span.start, span.length, span.format);
                }
                // A change here (e.g. an unclosed comment) can change every line below, but
                // QSyntaxHighlighter only moves on when the block's int state changes.
                const auto *before = dynamic_cast<BlockRenderData *>(currentBlock().userData());
                const QTextBlock next = currentBlock().next();
                if (before ? !(before->codeState == m_codeState) : next.userData() != nullptr) {
                    scheduleRehighlight(next);
                }
            }
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
        const auto *data = dynamic_cast<BlockRenderData *>(opening.userData());
        const bool rendered = !active && data && !data->math.isEmpty() && data->math.first().display;
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
        setCurrentBlockState(m.captured(1).at(0) == u'`' ? FencedBacktick : FencedTilde);
        // ```lang: the lines below are coloured as that language.
        m_codeLanguage = text.mid(m.capturedEnd(1)).trimmed().section(u' ', 0, 0);
        apply(0, text.size(), m_fence);
        if (!active) {
            apply(0, m.capturedEnd(1), m_hidden); // only the language name stays, dimmed
        }
        if (m_codeLanguage.isEmpty()) {
            // No language named: a script's "#!" line can tell us.
            m_codeLanguage = CodeHighlighter::languageFromShebang(currentBlock().next().text());
        }
        if (m_codeLanguage.isEmpty()) {
            m_codeLanguage = u" "_s; // still mark the block, for its background
        }
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
                formats[0] = mathStrut(metrics.ascent + padding, metrics.descent + padding, 0, text.at(0), true);
                m_mathSpans.append({0, int(text.size()), latex, true});
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

    highlightParagraphLine(text, offset, formats, active);
    flush(formats);
}

void MarkdownHighlighter::highlightParagraphLine(const QString &text, int offset, FormatList &formats, bool active)
{
    QTextBlock first;
    QTextBlock last;
    QList<int> contentStarts;
    paragraphExtent(currentBlock(), &first, &last, &contentStarts);
    if (first == last) {
        highlightInline(text, offset, formats, active);
        return;
    }

    // Style the paragraph's text (markers like > and - left out) as one, then keep this line's part.
    QString paragraph;
    int lineStart = 0;
    int index = 0;
    for (QTextBlock b = first;; b = b.next(), ++index) {
        if (b != first) {
            paragraph += u'\n';
        }
        if (b == currentBlock()) {
            offset = qMax(offset, contentStarts.at(index));
            lineStart = int(paragraph.size());
            paragraph += QStringView(text).mid(offset);
        } else {
            paragraph += QStringView(b.text()).mid(contentStarts.at(index));
        }
        if (b == last) {
            break;
        }
    }
    const int length = int(text.size()) - offset;
    FormatList paragraphFormats(paragraph.size());
    for (int i = 0; i < length; ++i) {
        paragraphFormats[lineStart + i] = formats[offset + i];
    }
    highlightInline(paragraph, 0, paragraphFormats, active, lineStart, lineStart + length);
    for (int i = 0; i < length; ++i) {
        formats[offset + i] = paragraphFormats[lineStart + i];
    }

    // Formulas and images never cross a line; keep this line's, at this line's offsets.
    auto keepOwn = [&](auto &spans) {
        for (qsizetype i = spans.size() - 1; i >= 0; --i) {
            auto &span = spans[i];
            if (span.start < lineStart || span.start + span.length > lineStart + length) {
                spans.removeAt(i);
            } else {
                span.start += offset - lineStart;
            }
        }
    };
    keepOwn(m_mathSpans);
    keepOwn(m_imageSpans);
}

void MarkdownHighlighter::highlightInline(const QString &text, int from, FormatList &formats, bool active, int checkFrom, int checkTo)
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
            m_mathSpans.append({start, length, latex, false});
        }
    }

    // Images: drawn in place when the cursor is elsewhere and the image has loaded.
    // Their hidden source reserves the image's width and height on the line.
    auto renderImage = [&](int start, int length, const QString &destination) {
        if (active || !m_imageCache) {
            return false;
        }
        const QUrl url = m_imageCache->resolve(destination);
        if (m_imageCache->status(url) != ImageCache::Status::Ready) {
            return false;
        }
        const QImage image = m_imageCache->image(url);
        QSizeF size = QSizeF(image.size()) / image.devicePixelRatio();
        // A few pixels short of the full width: Qt's text layout rounds advances, and an
        // overflow of even one pixel would make the page scroll sideways.
        const qreal maxWidth = m_contentWidth - 4;
        if (m_contentWidth > 0 && size.width() > maxWidth) {
            size = size.scaled(maxWidth, size.height() * maxWidth / size.width(), Qt::KeepAspectRatio);
        }
        // Small images (badges, icons) sit on the baseline like text. Taller ones get a line
        // exactly their height and sit on its bottom, so no gap opens up below them.
        const QFontMetricsF metrics(document()->defaultFont());
        const bool tall = size.height() > metrics.ascent() + metrics.descent();
        apply(start, length, m_hidden);
        formats[start] = mathStrut(size.height(), 0, size.width(), text.at(start), tall);
        m_imageSpans.append({start, length, url, size, tall});
        take(start, length);
        return true;
    };
    auto imageFailed = [&](const QString &destination) {
        return !active && m_imageCache && m_imageCache->status(m_imageCache->resolve(destination)) == ImageCache::Status::Failed;
    };

    for (auto it = kLinkedImage.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        renderImage(m.capturedStart(0), m.capturedLength(0), m.captured(2));
    }

    for (auto it = kImage.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        if (!isFree(m.capturedStart(0), m.capturedLength(0))) {
            continue;
        }
        if (renderImage(m.capturedStart(0), m.capturedLength(0), m.captured(2))) {
            continue;
        }
        syntax(m.capturedStart(0), 2); // ![
        apply(m.capturedStart(1), m.capturedLength(1), imageFailed(m.captured(2)) ? m_imageError : m_image);
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

    if (m_spellChecker && m_spellChecker->isEnabled()) {
        // Only this line's words. Code, math and link targets that start on another line of
        // the paragraph are not visible to wordsToCheck() here, but they are "taken" above.
        const int lineFrom = qMax(from, checkFrom);
        const int lineTo = checkTo < 0 ? int(text.size()) : checkTo;
        const QString line = text.mid(lineFrom, lineTo - lineFrom);
        const auto words = SpellChecker::wordsToCheck(line, 0);
        for (const auto &word : words) {
            const int start = lineFrom + word.start;
            if (taken[start]) {
                continue;
            }
            if (m_spellChecker->isMisspelled(line.mid(word.start, word.length))) {
                for (int i = start; i < start + word.length; ++i) {
                    formats[i].clearProperty(QTextFormat::FontUnderline); // a link's underline would hide the wave
                    formats[i].merge(m_misspelled);
                }
            }
        }
    }
}

void MarkdownHighlighter::setCodeFont(const QFont &font)
{
    m_codeFont = font;
}

void MarkdownHighlighter::setSpellChecker(SpellChecker *checker)
{
    m_spellChecker = checker;
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

void MarkdownHighlighter::setImageCache(ImageCache *cache)
{
    m_imageCache = cache;
}

bool MarkdownHighlighter::setContentWidth(qreal width)
{
    width = std::floor(width);
    if (qFuzzyCompare(width, m_contentWidth)) {
        return false;
    }
    m_contentWidth = width;
    return true;
}

qreal MarkdownHighlighter::contentWidth() const
{
    return m_contentWidth;
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
    const auto layout = m_tableLayout->compute(*table, m_contentWidth);
    if (row < layout.rowHeights.size()) {
        format.setFontPointSize(pointSizeForLineHeight(layout.rowHeights.at(row)));
    }
    return format;
}

QTextCharFormat MarkdownHighlighter::mathStrut(qreal ascent, qreal descent, qreal width, QChar character, bool fitTotalHeight) const
{
    const QFont base = document()->defaultFont();
    const QFontMetricsF metrics(base);
    // Inline formulas sit on the baseline: the line needs their ascent, and their descent
    // may use the extra line spacing below the text. Display formulas are centered, so
    // only the total height matters (width == 0).
    const qreal fontHeight = metrics.ascent() + metrics.descent();
    const qreal below = metrics.descent() + fontHeight * (LineHeightPercent - 100) / 100.0;
    // In total-height mode the object also covers the line spacing Qt adds below the line,
    // so the line plus its spacing is exactly the object's height.
    const qreal scale = (width > 0 && !fitTotalHeight)
        ? qMax<qreal>(1.0, qMax(ascent / metrics.ascent(), descent / below))
        : qMax<qreal>(1.0, (ascent + descent) / (fontHeight * LineHeightPercent / 100.0));
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
    // When this line changes what the other lines of its paragraph see (a ** added, the line
    // turned into a heading), re-style the paragraph around it once this edit is done.
    const QString text = currentBlock().text();
    const size_t key = paragraphKey(text, classify(currentBlock(), false));
    const auto *old = dynamic_cast<BlockRenderData *>(currentBlockUserData());
    if (old && old->paragraphKey != key && !m_restylingParagraphs) {
        // The lines above and below too: a line that stops being part of the paragraph
        // (say, becomes a heading) leaves its old neighbours to be re-styled without it.
        const QTextBlock block = currentBlock();
        queueParagraphRestyle(block.previous().isValid() ? block.previous() : block);
        queueParagraphRestyle(block);
        queueParagraphRestyle(block.next().isValid() ? block.next() : block);
    }

    auto *data = new BlockRenderData;
    data->math = m_mathSpans;
    data->images = m_imageSpans;
    data->codeLanguage = m_codeLanguage;
    data->codeState = m_codeState;
    data->paragraphKey = key;
    setCurrentBlockUserData(data);

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
