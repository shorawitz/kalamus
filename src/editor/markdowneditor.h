// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "markdowntable.h"

#include <QHash>
#include <QTextBlock>
#include <QTextEdit>

#include <functional>
#include <memory>
#include <optional>

class MarkdownHighlighter;
class QTextDocument;

/**
 * The writing surface. Holds the Markdown source as plain text and lets
 * MarkdownHighlighter make it look rendered. Adds the editing helpers people
 * expect from a word processor: Ctrl+B/I/K, headings, list continuation, and
 * tables that show as a grid and edit like a spreadsheet.
 */
class MarkdownEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    /// Replace the whole document (clears undo history and the modified flag).
    void loadMarkdown(const QString &markdown);
    QString markdown() const;

    bool isInTable() const;
    /// Extra entries for the right-click menu while the cursor is in a table.
    void setTableActions(const QList<QAction *> &actions);

public Q_SLOTS:
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleCode();
    void insertLink();
    /// 0 = paragraph, 1..6 = heading level. Applying the current level again turns it off.
    void setHeading(int level);

    void insertTable(int columns = 3, int bodyRows = 2);
    void insertTableRowBelow();
    void insertTableColumnAfter();
    void deleteTableRow();
    void deleteTableColumn();
    void setTableColumnAlignment(MarkdownTable::Align align);
    void formatTable();

Q_SIGNALS:
    void tableContextChanged(bool inTable);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct TableContext {
        QTextBlock first;
        QTextBlock last;
        MarkdownTable table;
        int row = 0; ///< 0 = header
        int column = 0;
    };

    /// A table drawn as a grid, in viewport coordinates.
    struct TableGrid {
        QList<QTextBlock> rows; ///< header and body rows (no delimiter)
        QList<QList<MarkdownTable::Cell>> cells;
        QList<MarkdownTable::Align> align;
        QList<qreal> columnX; ///< left edge of each column
        QList<qreal> columnWidth;
        QList<QRectF> rowRects;
        QRectF rect;
    };

    void toggleWrap(const QString &marker);
    bool continueBlock();
    bool indentListItem(bool outdent);
    void applyLineSpacing();
    void updateMargins();

    // Tables: editing
    std::optional<TableContext> tableAtCursor() const;
    using TableEdit = std::function<void(MarkdownTable &table, int &row, int &column)>;
    /// Edits the table at the cursor, rewrites it aligned, and moves to (row, column).
    bool editTable(const TableEdit &edit, bool selectCell = true);
    void writeTable(const TableContext &context, const MarkdownTable &table, int row, int column, bool selectCell);
    bool handleTableKey(QKeyEvent *event);

    // Tables: drawing
    QList<TableGrid> visibleTables() const;
    TableGrid layoutTable(const QTextBlock &first) const;
    QTextDocument *cellDocument(const QString &text, bool header) const;
    void paintTable(QPainter &painter, const TableGrid &grid) const;
    bool tableHitTest(const QPoint &pos, QTextCursor *cursor) const;

    MarkdownHighlighter *m_highlighter = nullptr;
    QList<QAction *> m_tableActions;
    bool m_inTable = false;
    mutable QHash<QString, std::shared_ptr<QTextDocument>> m_cellCache;
};
