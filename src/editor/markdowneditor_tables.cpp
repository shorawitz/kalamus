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
    if (!table) {
        return {};
    }
    // Same engine and width as the highlighter, so the rows match the source lines.
    const auto sizes = m_tableLayout->compute(*table, m_highlighter->contentWidth());
    qreal x = 0;
    for (int c = 0; c < table->columnCount(); ++c) {
        grid.align.append(table->alignment(c));
        grid.columnX.append(x);
        grid.columnWidth.append(sizes.columnWidths.at(c));
        x += sizes.columnWidths.at(c);
    }
    grid.texts.reserve(table->rowCount());
    for (int r = 0; r < table->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < table->columnCount(); ++c) {
            row.append(table->cell(r, c));
        }
        grid.texts.append(row);
    }

    // A row reaches down to where the next row starts: Qt reports line heights without
    // the extra line spacing, which it adds when placing the next line.
    const qreal left = layout->blockBoundingRect(first).translated(offset).left();
    for (int r = 0; r < grid.rows.size(); ++r) {
        const qreal top = layout->blockBoundingRect(grid.rows.at(r)).translated(offset).top();
        const qreal bottom = r + 1 < grid.rows.size() ? layout->blockBoundingRect(grid.rows.at(r + 1)).translated(offset).top()
                                                      : top + sizes.rowHeights.value(r);
        grid.rowRects.append(QRectF(left, top, sizes.width, bottom - top));
    }
    if (!grid.rowRects.isEmpty()) {
        grid.rect = QRectF(left, grid.rowRects.first().top(), sizes.width, grid.rowRects.last().bottom() - grid.rowRects.first().top());
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

namespace
{

/// The parts of @p extras inside @p cell (of @p block), moved onto its rendered @p doc.
QList<QAbstractTextDocumentLayout::Selection>
cellSelections(QTextDocument *doc, const QTextBlock &block, const MarkdownTable::Cell &cell, const QList<QTextEdit::ExtraSelection> &extras)
{
    QList<QAbstractTextDocumentLayout::Selection> selections;
    const int base = block.position() + cell.start;
    QList<int> positions; // filled on first use
    for (const QTextEdit::ExtraSelection &extra : extras) {
        const int from = qMax(extra.cursor.selectionStart() - base, 0);
        const int to = qMin(extra.cursor.selectionEnd() - base, cell.end - cell.start);
        if (from >= to) {
            continue;
        }
        if (positions.isEmpty()) {
            positions = TableLayout::renderedPositions(block.text().mid(cell.start, cell.end - cell.start), doc->toPlainText());
        }
        int first = -1;
        int last = -1;
        for (int i = from; i < to; ++i) {
            if (const int p = positions.at(i); p >= 0) {
                first = first < 0 ? p : first;
                last = p;
            }
        }
        if (first < 0) {
            continue; // only markers matched: nothing drawn to highlight
        }
        QTextCursor cursor(doc);
        cursor.setPosition(first);
        cursor.setPosition(last + 1, QTextCursor::KeepAnchor);
        selections.append({cursor, extra.format});
    }
    return selections;
}

} // namespace

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

    // Cell contents, wrapped to the column width and top-aligned.
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = pal;
    const QList<QTextEdit::ExtraSelection> extras = extraSelections(); // e.g. find matches
    for (int r = 0; r < grid.rows.size() && r < grid.texts.size(); ++r) {
        const QRectF rowRect = grid.rowRects.at(r);
        const QTextBlock block = grid.rows.at(r);
        QList<QTextEdit::ExtraSelection> rowExtras;
        for (const QTextEdit::ExtraSelection &extra : extras) {
            if (extra.cursor.selectionEnd() > block.position() && extra.cursor.selectionStart() < block.position() + block.length()) {
                rowExtras.append(extra);
            }
        }
        for (int c = 0; c < grid.columnX.size(); ++c) {
            const QString text = grid.texts.at(r).value(c);
            if (text.isEmpty()) {
                continue;
            }
            const QRectF cell(grid.rect.left() + grid.columnX.at(c), rowRect.top(), grid.columnWidth.at(c), rowRect.height());
            QTextDocument *doc = m_tableLayout->cell(text, r == 0, cell.width() - 2 * TableLayout::Padding, toQt(grid.align.value(c)));
            painter.save();
            painter.setClipRect(cell, Qt::IntersectClip);
            painter.translate(cell.left() + TableLayout::Padding, cell.top() + TableLayout::VerticalPadding);
            context.selections = rowExtras.isEmpty() || c >= grid.cells.at(r).size()
                ? QList<QAbstractTextDocumentLayout::Selection>()
                : cellSelections(doc, block, grid.cells.at(r).at(c), rowExtras);
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
    {
        QPainter background(viewport());
        background.setClipRegion(event->region());
        paintCodeBackgrounds(background);
    }
    QTextEdit::paintEvent(event); // the text, on top of the code backgrounds
    QPainter painter(viewport());
    painter.setClipRegion(event->region());
    paintInlineObjects(painter);
    const auto grids = visibleTables();
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
    const int position = event->reason() == QContextMenuEvent::Mouse ? cursorForPosition(event->pos()).position() : textCursor().position();
    addSpellingActions(menu, position);
    if (isInTable() && !m_tableActions.isEmpty()) {
        menu->addSeparator();
        menu->addActions(m_tableActions);
    }
    menu->exec(event->globalPos());
    delete menu;
}
