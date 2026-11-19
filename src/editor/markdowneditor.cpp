// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>

using namespace Qt::StringLiterals;

namespace
{
const QRegularExpression kHeadingPrefix(uR"(^ {0,3}(#{1,6})(?:[ \t]+|$))"_s);
const QRegularExpression kListPrefix(uR"(^([ \t]*)([-*+]|\d{1,9}[.)])([ \t]+)(\[[ xX]\][ \t]+)?)"_s);
const QRegularExpression kQuotePrefix(uR"(^ {0,3}(?:>[ \t]?)+)"_s);

QString textBetween(QTextDocument *doc, int from, int to)
{
    if (from < 0 || to > doc->characterCount() - 1 || from >= to) {
        return {};
    }
    QTextCursor c(doc);
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    return c.selectedText();
}

void removeRange(QTextDocument *doc, int from, int to)
{
    QTextCursor c(doc);
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    c.removeSelectedText();
}

void insertAt(QTextDocument *doc, int pos, const QString &text)
{
    QTextCursor c(doc);
    c.setPosition(pos);
    c.insertText(text);
}

bool insideVerbatimBlock(const QTextBlock &block)
{
    const int state = block.previous().isValid() ? block.previous().userState() : -1;
    return state == MarkdownHighlighter::FencedBacktick || state == MarkdownHighlighter::FencedTilde
        || state == MarkdownHighlighter::MathBlock || state == MarkdownHighlighter::FrontMatter;
}
} // namespace

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setCursorWidth(2);
    // Text wraps and tables/images fit the width, so there is nothing to scroll to sideways.
    // Keep it that way even if something overflows by a rounding error.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value != 0) {
            horizontalScrollBar()->setValue(0);
        }
    });

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    setFont(font);
    document()->setDocumentMargin(32);

    m_tableLayout = std::make_unique<TableLayout>();
    m_tableLayout->setFont(font);
    m_highlighter = new MarkdownHighlighter(document());
    m_highlighter->setTableLayout(m_tableLayout.get());
    m_imageCache = new ImageCache(this);
    m_highlighter->setImageCache(m_imageCache);
    connect(m_imageCache, &ImageCache::imageReady, this, &MarkdownEditor::restyleImageBlocks);
    connect(m_highlighter, &MarkdownHighlighter::restyled, this, [this] {
        if (!m_layoutCheckPending) {
            m_layoutCheckPending = true;
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_layoutCheckPending = false;
                    ensureFullyLaidOut();
                },
                Qt::QueuedConnection);
        }
    });

    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        m_highlighter->setActiveBlock(textCursor().block());
        const bool inTable = isInTable();
        if (inTable != m_inTable) {
            m_inTable = inTable;
            Q_EMIT tableContextChanged(inTable);
        }
    });

    loadMarkdown(QString());
}

void MarkdownEditor::loadMarkdown(const QString &markdown)
{
    setPlainText(markdown);
    applyLineSpacing();
    document()->clearUndoRedoStacks();
    document()->setModified(false);
    moveCursor(QTextCursor::Start);
    m_highlighter->setActiveBlock(textCursor().block());
}

QString MarkdownEditor::markdown() const
{
    return toPlainText();
}

void MarkdownEditor::applyLineSpacing()
{
    QTextCursor c(document());
    c.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(MarkdownHighlighter::LineHeightPercent, QTextBlockFormat::ProportionalHeight);
    c.mergeBlockFormat(format);
}

void MarkdownEditor::toggleBold()
{
    toggleWrap(u"**"_s);
}

void MarkdownEditor::toggleItalic()
{
    toggleWrap(u"*"_s);
}

void MarkdownEditor::toggleStrikethrough()
{
    toggleWrap(u"~~"_s);
}

void MarkdownEditor::toggleCode()
{
    toggleWrap(u"`"_s);
}

void MarkdownEditor::toggleWrap(const QString &marker)
{
    QTextCursor c = textCursor();
    const int len = marker.size();

    if (!c.hasSelection()) {
        c.insertText(marker + marker);
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, len);
        setTextCursor(c);
        return;
    }

    QTextDocument *doc = document();
    int start = c.selectionStart();
    int end = c.selectionEnd();
    const QString selected = c.selectedText();

    c.beginEditBlock();
    if (textBetween(doc, start - len, start) == marker && textBetween(doc, end, end + len) == marker) {
        // **|text|**  -> remove the markers around the selection
        removeRange(doc, end, end + len);
        removeRange(doc, start - len, start);
        start -= len;
        end -= len;
    } else if (selected.size() >= 2 * len && selected.startsWith(marker) && selected.endsWith(marker)) {
        // |**text**|  -> remove the markers inside the selection
        removeRange(doc, end - len, end);
        removeRange(doc, start, start + len);
        end -= 2 * len;
    } else {
        insertAt(doc, end, marker);
        insertAt(doc, start, marker);
        start += len;
        end += len;
    }
    c.endEditBlock();

    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void MarkdownEditor::insertLink()
{
    QTextCursor c = textCursor();
    const QString selected = c.selectedText();
    const int start = c.selectionStart();
    const bool selectionIsUrl = selected.startsWith(u"http://"_s) || selected.startsWith(u"https://"_s);
    const QString placeholder = u"https://"_s;

    c.beginEditBlock();
    if (selected.isEmpty() || selectionIsUrl) {
        c.insertText(u"[]("_s + (selectionIsUrl ? selected : placeholder) + u')');
        c.setPosition(start + 1); // inside [], ready for the link text
    } else {
        c.insertText(u'[' + selected + u"]("_s + placeholder + u')');
        const int urlStart = start + 1 + selected.size() + 2;
        c.setPosition(urlStart);
        c.setPosition(urlStart + placeholder.size(), QTextCursor::KeepAnchor);
    }
    c.endEditBlock();
    setTextCursor(c);
}

bool MarkdownEditor::selectionSpansLines() const
{
    const QTextCursor c = textCursor();
    return c.hasSelection() && document()->findBlock(c.selectionStart()) != document()->findBlock(c.selectionEnd());
}

void MarkdownEditor::insertCodeBlock(const QString &language)
{
    const QString fence = u"```"_s;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection()) {
        // Whole lines: from the start of the first selected line to the end of the last.
        QTextCursor first(document());
        first.setPosition(c.selectionStart());
        QTextCursor last(document());
        last.setPosition(c.selectionEnd());
        if (last.positionInBlock() == 0 && c.selectionEnd() > c.selectionStart()) {
            last.movePosition(QTextCursor::PreviousCharacter); // selection ended at a line start
        }
        last.movePosition(QTextCursor::EndOfBlock);
        last.insertText(u'\n' + fence); // closing fence first, so the first position stays valid
        first.movePosition(QTextCursor::StartOfBlock);
        first.insertText(fence + language + u'\n');
        c = last;
    } else {
        if (!c.block().text().trimmed().isEmpty()) {
            c.movePosition(QTextCursor::EndOfBlock);
            c.insertText(u"\n"_s);
        }
        c.insertText(fence + language + u"\n\n"_s + fence);
        c.movePosition(QTextCursor::PreviousBlock); // into the empty line, ready to type
    }
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::setHeading(int level)
{
    const QTextBlock block = textCursor().block();
    const auto m = kHeadingPrefix.match(block.text());
    const int current = m.hasMatch() ? m.capturedLength(1) : 0;
    if (level == current) {
        level = 0;
    }

    QTextCursor c(block);
    c.beginEditBlock();
    if (m.hasMatch()) {
        c.setPosition(block.position() + m.capturedLength(0), QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    if (level > 0) {
        c.insertText(QString(level, u'#') + u' ');
    }
    c.endEditBlock();
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event)
{
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    if (handleTableKey(event)) {
        return;
    }
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (mods == Qt::ShiftModifier) {
            // A plain new line (QTextEdit would insert a Unicode line separator).
            textCursor().insertText(u"\n"_s);
            ensureCursorVisible();
            return;
        }
        if (mods == Qt::NoModifier && continueBlock()) {
            return;
        }
        break;
    case Qt::Key_Tab:
        if (mods == Qt::NoModifier && indentListItem(false)) {
            return;
        }
        break;
    case Qt::Key_Backtab:
        if (indentListItem(true)) {
            return;
        }
        break;
    default:
        break;
    }
    QTextEdit::keyPressEvent(event);
}

bool MarkdownEditor::continueBlock()
{
    QTextCursor c = textCursor();
    if (c.hasSelection() || insideVerbatimBlock(c.block())) {
        return false;
    }
    const QTextBlock block = c.block();
    const QString text = block.text();
    const int column = c.positionInBlock();

    QString prefix;
    int prefixEnd = 0;
    if (const auto m = kListPrefix.match(text); m.hasMatch() && column >= m.capturedEnd(0)) {
        QString marker = m.captured(2);
        if (marker.at(0).isDigit()) {
            const int number = QStringView(marker).chopped(1).toInt() + 1;
            marker = QString::number(number) + marker.back();
        }
        prefix = m.captured(1) + marker + m.captured(3) + (m.capturedLength(4) > 0 ? u"[ ] "_s : QString());
        prefixEnd = m.capturedEnd(0);
    } else if (const auto q = kQuotePrefix.match(text); q.hasMatch() && column >= q.capturedEnd(0)) {
        prefix = q.captured(0);
        prefixEnd = q.capturedEnd(0);
    } else {
        return false;
    }

    if (text.mid(prefixEnd).trimmed().isEmpty()) {
        // Enter on an empty item ends the list / quote, like in a word processor.
        QTextCursor line(block);
        line.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        line.removeSelectedText();
        return true;
    }

    c.insertText(u"\n"_s + prefix);
    setTextCursor(c);
    ensureCursorVisible();
    return true;
}

bool MarkdownEditor::indentListItem(bool outdent)
{
    const QTextCursor cursor = textCursor();
    if (cursor.hasSelection() || insideVerbatimBlock(cursor.block())) {
        return false;
    }
    const QTextBlock block = cursor.block();
    const auto m = kListPrefix.match(block.text());
    if (!m.hasMatch()) {
        return false;
    }
    const int width = m.capturedLength(2) + m.capturedLength(3);

    QTextCursor c(block);
    if (outdent) {
        const int remove = qMin(width, m.capturedLength(1));
        if (remove == 0) {
            return true;
        }
        c.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else {
        c.insertText(QString(width, u' '));
    }
    return true;
}

void MarkdownEditor::resizeEvent(QResizeEvent *event)
{
    QTextEdit::resizeEvent(event);
    updateMargins();
}

void MarkdownEditor::changeEvent(QEvent *event)
{
    QTextEdit::changeEvent(event);
    // Palette/font changes also arrive from the constructor, before the highlighter exists.
    if (m_highlighter && (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange)) {
        m_tableLayout->setFont(font());
        m_highlighter->refreshStyles();
        updateMargins();
    }
}

void MarkdownEditor::setBaseDirectory(const QString &directory)
{
    if (directory == m_imageCache->baseDirectory()) {
        return;
    }
    m_imageCache->setBaseDirectory(directory);
    m_imageCache->clear();
    m_scaledImages.clear();
    m_highlighter->rehighlight();
}

QString MarkdownEditor::baseDirectory() const
{
    return m_imageCache->baseDirectory();
}

void MarkdownEditor::restyleImageBlocks()
{
    if (m_imageRestylePending) {
        return;
    }
    // Several images often arrive together; re-style once.
    m_imageRestylePending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_imageRestylePending = false;
            for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
                if (block.text().contains(u"!["_s)) {
                    m_highlighter->rehighlightBlock(block);
                }
            }
            ensureFullyLaidOut();
            viewport()->update();
        },
        Qt::QueuedConnection);
}

void MarkdownEditor::ensureFullyLaidOut()
{
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (block.isVisible() && block.layout() && block.layout()->lineCount() == 0) {
            const int from = block.position();
            document()->markContentsDirty(from, document()->characterCount() - from);
            return;
        }
    }
}

void MarkdownEditor::setLimitLineWidth(bool limit)
{
    if (limit != m_limitLineWidth) {
        m_limitLineWidth = limit;
        updateMargins();
    }
}

bool MarkdownEditor::limitLineWidth() const
{
    return m_limitLineWidth;
}

void MarkdownEditor::updateMargins()
{
    // Readable width: about 52em of text (Typora's default is similar), centered like a page.
    // Otherwise the text uses the whole window.
    int side = 0;
    if (m_limitLineWidth) {
        const qreal em = QFontInfo(font()).pixelSize();
        const int maxWidth = qRound(em * 52 + 2 * document()->documentMargin());
        side = qMax(0, (width() - maxWidth) / 2);
    }
    setViewportMargins(side, 0, side, 0);
    relayoutTables();
}

bool MarkdownEditor::viewportEvent(QEvent *event)
{
    // The viewport also changes width when the vertical scroll bar appears or goes away.
    if (event->type() == QEvent::Resize) {
        relayoutTables();
    }
    return QTextEdit::viewportEvent(event);
}

void MarkdownEditor::relayoutTables()
{
    if (!m_highlighter || m_relayoutPending) {
        return;
    }
    // Coalesce the many resize events of a window drag into one re-layout.
    m_relayoutPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_relayoutPending = false;
            const qreal available = viewport()->width() - 2 * document()->documentMargin();
            if (!m_highlighter->setContentWidth(available)) {
                return;
            }
            for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
                const auto *data = dynamic_cast<BlockRenderData *>(block.userData());
                if (MarkdownHighlighter::isTableState(block.userState()) || (data && !data->images.isEmpty())) {
                    m_highlighter->rehighlightBlock(block);
                }
            }
            ensureFullyLaidOut();
            viewport()->update();
        },
        Qt::QueuedConnection);
}
