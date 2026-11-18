// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

// Table support for MarkdownEditor: drawing tables as grids when the cursor is
// elsewhere, and spreadsheet-style editing when it is inside one.

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextDocument>

using namespace Qt::StringLiterals;

namespace
{
constexpr qreal kCellPadding = 10;
constexpr qreal kMinColumnWidth = 48;

bool isTableBody(const QTextBlock &block)
{
    const int state = block.userState();
    return state == MarkdownHighlighter::TableDelimiter || state == MarkdownHighlighter::TableRow;
}

QTextBlock tableHeaderOf(QTextBlock block)
{
    if (!MarkdownHighlighter::isTableState(block.userState())) {
        return {};
    }
    while (block.userState() != MarkdownHighlighter::TableHeader && block.previous().isValid()
           && MarkdownHighlighter::isTableState(block.previous().userState())) {
        block = block.previous();
    }
    return block.userState() == MarkdownHighlighter::TableHeader ? block : QTextBlock();
}

QTextBlock tableLastOf(QTextBlock header)
{
    while (header.next().isValid() && isTableBody(header.next())) {
        header = header.next();
    }
    return header;
}

Qt::Alignment toQt(MarkdownTable::Align align)
{
    switch (align) {
    case MarkdownTable::Align::Center:
        return Qt::AlignHCenter;
    case MarkdownTable::Align::Right:
        return Qt::AlignRight;
    default:
        return Qt::AlignLeft;
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Editing

bool MarkdownEditor::isInTable() const
{
    return MarkdownHighlighter::isTableState(textCursor().block().userState());
}

void MarkdownEditor::setTableActions(const QList<QAction *> &actions)
{
    m_tableActions = actions;
}

std::optional<MarkdownEditor::TableContext> MarkdownEditor::tableAtCursor() const
{
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const QTextBlock first = tableHeaderOf(block);
    if (!first.isValid()) {
        return std::nullopt;
    }
    const QTextBlock last = tableLastOf(first);

    QStringList lines;
    int row = 0;
    int index = 0;
    for (QTextBlock b = first; b.isValid(); b = b.next(), ++index) {
        lines.append(b.text());
        if (b == block) {
            row = index <= 1 ? 0 : index - 1; // the delimiter line counts as the header
        }
        if (b == last) {
            break;
        }
    }
    const auto table = MarkdownTable::parse(lines);
    if (!table) {
        return std::nullopt;
    }

    const int position = cursor.positionInBlock();
    const auto cells = MarkdownTable::splitRow(block.text());
    int column = 0;
    for (int c = 0; c < cells.size(); ++c) {
        if (position >= cells[c].segmentStart) {
            column = c;
        }
    }
    column = qBound(0, column, table->columnCount() - 1);
    return TableContext{first, last, *table, row, column};
}

void MarkdownEditor::writeTable(const TableContext &context, const MarkdownTable &table, int row, int column, bool selectCell)
{
    const int start = context.first.position();
    const int end = context.last.position() + context.last.length() - 1;
    const QString text = table.toMarkdown();

    QTextCursor c(document());
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    if (c.selectedText().replace(QChar::ParagraphSeparator, u'\n') != text) {
        c.insertText(text);
    }

    row = qBound(0, row, table.rowCount() - 1);
    column = qBound(0, column, table.columnCount() - 1);
    const int cellStart = start + table.cellOffset(row, column);
    const int cellEnd = cellStart + table.cell(row, column).size();
    c.setPosition(selectCell ? cellStart : cellEnd);
    c.setPosition(cellEnd, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

bool MarkdownEditor::editTable(const TableEdit &edit, bool selectCell)
{
    const auto context = tableAtCursor();
    if (!context) {
        return false;
    }
    MarkdownTable table = context->table;
    int row = context->row;
    int column = context->column;
    edit(table, row, column);

    QTextCursor group(document());
    group.beginEditBlock();
    writeTable(*context, table, row, column, selectCell);
    group.endEditBlock();
    return true;
}

bool MarkdownEditor::handleTableKey(QKeyEvent *event)
{
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    const bool tab = key == Qt::Key_Tab && mods == Qt::NoModifier;
    const bool backtab = key == Qt::Key_Backtab;
    const bool enter = (key == Qt::Key_Return || key == Qt::Key_Enter) && mods == Qt::NoModifier;
    if ((!tab && !backtab && !enter) || !isInTable()) {
        return false;
    }

    if (tab) {
        // Next cell; past the last cell, a new row.
        return editTable([](MarkdownTable &t, int &row, int &column) {
            if (column + 1 < t.columnCount()) {
                ++column;
                return;
            }
            column = 0;
            ++row;
            if (row >= t.rowCount()) {
                t.insertRow(t.rowCount());
            }
        });
    }
    if (backtab) {
        return editTable([](MarkdownTable &t, int &row, int &column) {
            if (column > 0) {
                --column;
            } else if (row > 0) {
                --row;
                column = t.columnCount() - 1;
            }
        });
    }

    const auto context = tableAtCursor();
    if (!context) {
        return false;
    }
    const bool lastRowEmpty = context->row >= 1 && context->row == context->table.rowCount() - 1 && context->table.isRowEmpty(context->row);
    if (!lastRowEmpty) {
        // Enter adds a row below and moves to its first cell.
        return editTable([](MarkdownTable &t, int &row, int &column) {
            row = qMax(row, 0) + 1;
            t.insertRow(row);
            column = 0;
        });
    }

    // Enter on an empty last row leaves the table, like Enter on an empty list item.
    MarkdownTable table = context->table;
    table.removeRow(context->row);
    const int start = context->first.position();
    QTextCursor group(document());
    group.beginEditBlock();
    writeTable(*context, table, 0, 0, false);
    QTextCursor c(document());
    c.setPosition(start + table.toMarkdown().size());
    c.insertText(u"\n\n"_s); // a blank line ends the table
    group.endEditBlock();
    setTextCursor(c);
    return true;
}

void MarkdownEditor::insertTable(int columns, int bodyRows)
{
    const MarkdownTable table = MarkdownTable::create(columns, bodyRows);
    QTextCursor c = textCursor();
    c.clearSelection();
    c.beginEditBlock();
    // Tables need a blank line before and after them.
    if (!c.block().text().trimmed().isEmpty()) {
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertText(u"\n\n"_s);
    } else if (c.block().previous().isValid() && !c.block().previous().text().trimmed().isEmpty()) {
        c.insertText(u"\n"_s);
    }
    const int start = c.position();
    c.insertText(table.toMarkdown());
    if (c.block().next().isValid() && !c.block().next().text().trimmed().isEmpty()) {
        c.insertText(u"\n"_s);
    }
    c.endEditBlock();

    const int cellStart = start + table.cellOffset(0, 0);
    c.setPosition(cellStart);
    c.setPosition(cellStart + table.cell(0, 0).size(), QTextCursor::KeepAnchor);
    setTextCursor(c);
    setFocus();
}

void MarkdownEditor::insertTableRowBelow()
{
    editTable([](MarkdownTable &t, int &row, int &) {
        row = qMax(row, 0) + 1;
        t.insertRow(row);
    });
}

void MarkdownEditor::insertTableColumnAfter()
{
    editTable([](MarkdownTable &t, int &, int &column) {
        ++column;
        t.insertColumn(column);
    });
}

void MarkdownEditor::deleteTableRow()
{
    editTable([](MarkdownTable &t, int &row, int &) {
        if (row >= 1) {
            t.removeRow(row);
            row = qMin(row, t.rowCount() - 1);
        }
    });
}

void MarkdownEditor::deleteTableColumn()
{
    editTable([](MarkdownTable &t, int &, int &column) {
        t.removeColumn(column);
        column = qMin(column, t.columnCount() - 1);
    });
}

void MarkdownEditor::setTableColumnAlignment(MarkdownTable::Align align)
{
    editTable(
        [align](MarkdownTable &t, int &, int &column) {
            t.setAlignment(column, align);
        },
        false);
}

void MarkdownEditor::formatTable()
{
    editTable([](MarkdownTable &, int &, int &) { }, false);
}

// ---------------------------------------------------------------------------
// Drawing

QTextDocument *MarkdownEditor::cellDocument(const QString &text, bool header) const
{
    const QString key = (header ? u"h:"_s : u"b:"_s) + text;
    if (const auto it = m_cellCache.constFind(key); it != m_cellCache.cend()) {
        return it->get();
    }
    if (m_cellCache.size() > 2000) {
        m_cellCache.clear();
    }
    auto doc = std::make_shared<QTextDocument>();
    QFont f = font();
    f.setBold(header);
    doc->setDefaultFont(f);
    doc->setDocumentMargin(0);
    doc->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    m_cellCache.insert(key, doc);
    return doc.get();
}

MarkdownEditor::TableGrid MarkdownEditor::layoutTable(const QTextBlock &first) const
{
    TableGrid grid;
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());

    QStringList lines;
    for (QTextBlock b = first; b.isValid() && (b == first || isTableBody(b)); b = b.next()) {
        lines.append(b.text());
        if (b.userState() != MarkdownHighlighter::TableDelimiter) {
            grid.rows.append(b);
            grid.cells.append(MarkdownTable::splitRow(b.text()));
        }
    }
    const auto table = MarkdownTable::parse(lines);
    int columns = table ? table->columnCount() : 0;
    for (const auto &row : std::as_const(grid.cells)) {
        columns = qMax(columns, int(row.size()));
    }
    for (int c = 0; c < columns; ++c) {
        grid.align.append(table ? table->alignment(c) : MarkdownTable::Align::None);
    }

    // Column widths from the rendered cell contents.
    QList<qreal> widths(columns, kMinColumnWidth);
    for (int r = 0; r < grid.cells.size(); ++r) {
        for (int c = 0; c < grid.cells[r].size(); ++c) {
            QTextDocument *doc = cellDocument(grid.cells[r][c].text, r == 0);
            doc->setTextWidth(-1);
            widths[c] = qMax(widths[c], doc->idealWidth() + 2 * kCellPadding);
        }
    }

    const QRectF firstRect = layout->blockBoundingRect(first).translated(offset);
    const qreal left = firstRect.left();
    const qreal available = viewport()->width() - document()->documentMargin() - left;
    qreal total = 0;
    for (qreal w : std::as_const(widths)) {
        total += w;
    }
    if (total > available && total > 0 && available > 0) {
        const qreal scale = available / total;
        for (qreal &w : widths) {
            w *= scale;
        }
        total = available;
    }
    qreal x = 0;
    for (qreal w : std::as_const(widths)) {
        grid.columnX.append(x);
        grid.columnWidth.append(w);
        x += w;
    }

    for (const QTextBlock &row : std::as_const(grid.rows)) {
        const QRectF r = layout->blockBoundingRect(row).translated(offset);
        grid.rowRects.append(QRectF(left, r.top(), total, r.height()));
    }
    if (!grid.rowRects.isEmpty()) {
        grid.rect = QRectF(left, grid.rowRects.first().top(), total, grid.rowRects.last().bottom() - grid.rowRects.first().top());
    }
    return grid;
}

QList<MarkdownEditor::TableGrid> MarkdownEditor::visibleTables() const
{
    QList<TableGrid> grids;
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    const qreal dy = verticalScrollBar()->value();
    const qreal bottom = viewport()->height();

    QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
    if (const QTextBlock header = tableHeaderOf(block); header.isValid()) {
        block = header;
    }
    while (block.isValid()) {
        if (layout->blockBoundingRect(block).top() - dy > bottom) {
            break;
        }
        if (block.userState() == MarkdownHighlighter::TableHeader) {
            if (!m_highlighter->isActive(block)) {
                TableGrid grid = layoutTable(block);
                if (!grid.rows.isEmpty()) {
                    grids.append(grid);
                }
            }
            block = tableLastOf(block).next();
            continue;
        }
        block = block.next();
    }
    return grids;
}

void MarkdownEditor::paintTable(QPainter &painter, const TableGrid &grid) const
{
    const QPalette pal = palette();
    QColor border = pal.color(QPalette::Text);
    border.setAlphaF(0.25);
    constexpr qreal radius = 4;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath outline;
    outline.addRoundedRect(grid.rect, radius, radius);
    painter.setClipPath(outline, Qt::IntersectClip);

    // Header background.
    QRectF header = grid.rowRects.first();
    if (grid.rowRects.size() > 1) {
        header.setBottom(grid.rowRects.at(1).top());
    }
    painter.fillRect(header, pal.color(QPalette::AlternateBase));

    // Cell contents.
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = pal;
    for (int r = 0; r < grid.rows.size(); ++r) {
        const QRectF rowRect = grid.rowRects.at(r);
        for (int c = 0; c < grid.columnX.size() && c < grid.cells.at(r).size(); ++c) {
            const QString text = grid.cells.at(r).at(c).text;
            if (text.isEmpty()) {
                continue;
            }
            const QRectF cell(grid.rect.left() + grid.columnX.at(c), rowRect.top(), grid.columnWidth.at(c), rowRect.height());
            QTextDocument *doc = cellDocument(text, r == 0);
            QTextOption option = doc->defaultTextOption();
            option.setAlignment(toQt(grid.align.value(c)));
            option.setWrapMode(QTextOption::NoWrap);
            doc->setDefaultTextOption(option);
            doc->setTextWidth(qMax<qreal>(1, cell.width() - 2 * kCellPadding));

            painter.save();
            painter.setClipRect(cell.adjusted(kCellPadding / 2, 0, -kCellPadding / 2, 0), Qt::IntersectClip);
            painter.translate(cell.left() + kCellPadding, cell.top() + (cell.height() - doc->size().height()) / 2);
            doc->documentLayout()->draw(&painter, context);
            painter.restore();
        }
    }

    // Grid lines.
    for (int r = 1; r < grid.rowRects.size(); ++r) {
        painter.setPen(QPen(border, r == 1 ? 2 : 1));
        const qreal y = grid.rowRects.at(r).top();
        painter.drawLine(QPointF(grid.rect.left(), y), QPointF(grid.rect.right(), y));
    }
    painter.setPen(QPen(border, 1));
    for (int c = 1; c < grid.columnX.size(); ++c) {
        const qreal x = grid.rect.left() + grid.columnX.at(c);
        painter.drawLine(QPointF(x, grid.rect.top()), QPointF(x, grid.rect.bottom()));
    }
    painter.restore();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(grid.rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.restore();
}

bool MarkdownEditor::tableHitTest(const QPoint &pos, QTextCursor *cursor) const
{
    const auto grids = visibleTables();
    for (const TableGrid &grid : grids) {
        if (!grid.rect.adjusted(0, -2, 0, 2).contains(pos)) {
            continue;
        }
        int row = 0;
        for (int r = 0; r < grid.rowRects.size(); ++r) {
            if (pos.y() >= grid.rowRects.at(r).top()) {
                row = r;
            }
        }
        int column = 0;
        for (int c = 0; c < grid.columnX.size(); ++c) {
            if (pos.x() >= grid.rect.left() + grid.columnX.at(c)) {
                column = c;
            }
        }
        const QTextBlock block = grid.rows.at(row);
        const auto &cells = grid.cells.at(row);
        const int inBlock = column < cells.size() ? cells.at(column).end : block.length() - 1;
        QTextCursor c(document());
        c.setPosition(block.position() + inBlock);
        *cursor = c;
        return true;
    }
    return false;
}

void MarkdownEditor::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);
    const auto grids = visibleTables();
    if (grids.isEmpty()) {
        return;
    }
    QPainter painter(viewport());
    painter.setClipRegion(event->region());
    for (const TableGrid &grid : grids) {
        paintTable(painter, grid);
    }
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event)
{
    // A click on a drawn table puts the cursor in that cell, which shows the table as source.
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier)) {
        QTextCursor cursor;
        if (tableHitTest(event->position().toPoint(), &cursor)) {
            setTextCursor(cursor);
            setFocus(Qt::MouseFocusReason);
            event->accept();
            return;
        }
    }
    QTextEdit::mousePressEvent(event);
}

void MarkdownEditor::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->reason() == QContextMenuEvent::Mouse) {
        QTextCursor cursor;
        if (tableHitTest(event->pos(), &cursor)) {
            setTextCursor(cursor);
        }
    }
    QMenu *menu = createStandardContextMenu();
    if (isInTable() && !m_tableActions.isEmpty()) {
        menu->addSeparator();
        menu->addActions(m_tableActions);
    }
    menu->exec(event->globalPos());
    delete menu;
}
