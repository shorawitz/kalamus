// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "imagecache.h"
#include "markdowntable.h"
#include "spellchecker.h"
#include "tablelayout.h"

#include <QHash>
#include <QPixmap>
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
class QMenu;

class MarkdownEditor : public QTextEdit
{
    Q_OBJECT

public:
    /// Default width of the text column when Limit Line Width is on, in em.
    static constexpr int DefaultReadableWidth = 66;

    explicit MarkdownEditor(QWidget *parent = nullptr);

    /// Replace the whole document (clears undo history and the modified flag).
    void loadMarkdown(const QString &markdown);
    QString markdown() const;

    bool isInTable() const;
    /// True when the selection covers more than one line.
    bool selectionSpansLines() const;

    /// The document's folder, for relative image paths ("" for an unsaved document).
    void setBaseDirectory(const QString &directory);
    QString baseDirectory() const;

    /// Called when an image needs a saved document to live next to; returns true once
    /// the document has a folder (e.g. after asking the user to save it).
    void setSaveRequester(const std::function<bool()> &requester);

    /// Insert links to image files at the cursor. Files inside the document's folder are
    /// linked where they are; others are copied into its assets/ folder first.
    bool insertImageFiles(const QList<QUrl> &files);
    /// Save image data (e.g. a pasted screenshot) into assets/ and link it at the cursor.
    bool insertImageData(const QImage &image);

    static bool isImageFile(const QString &path);

    /// Keep text at a comfortable reading width (centered), or use the full window width.
    void setLimitLineWidth(bool limit);
    bool limitLineWidth() const;
    /// The reading width in em (about 860 px at the default font for 66, like Typora).
    void setReadableWidth(int em);
    int readableWidth() const;

    /// Font for code, math source and table source (only its family; sizes follow the text).
    void setCodeFont(const QFont &font);
    /// Fetch http(s) images, or show a note in their place.
    void setLoadRemoteImages(bool load);
    bool loadRemoteImages() const;
    /// Spell checking: on or off, and the dictionary ("" = the system's default).
    void setSpellCheckingEnabled(bool enabled);
    bool spellCheckingEnabled() const;
    void setSpellCheckingLanguage(const QString &language);
    QString spellCheckingLanguage() const;
    /// The misspelled word at @p position selected in a cursor; a null cursor if there is none.
    QTextCursor misspelledWordAt(int position) const;
    /// Stop flagging @p word: for this session, or for good (personal dictionary).
    void acceptWord(const QString &word, bool permanently);
    /// Put suggestions, Add to Dictionary and Ignore at the top of @p menu when the word
    /// at @p position is misspelled.
    void addSpellingActions(QMenu *menu, int position);

    /// Put the cursor at the start of line @p blockNumber and scroll it to the top of the view.
    void goToBlock(int blockNumber);

    /// Extra entries for the right-click menu while the cursor is in a table.
    void setTableActions(const QList<QAction *> &actions);

public Q_SLOTS:
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleCode();
    void insertLink();
    /// Wrap the selected lines in a ``` fenced code block (or insert an empty one).
    /// @p language goes after the opening fence and picks the syntax colours.
    void insertCodeBlock(const QString &language);
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
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    bool event(QEvent *event) override;
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
        QList<QList<MarkdownTable::Cell>> cells; ///< source positions, for clicks
        QList<QStringList> texts; ///< cell contents (header first)
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
    /// Re-style every table (after the width tables may use has changed).
    void relayoutTables();
    /// Qt lays large documents out in steps; re-styling blocks while that runs can make it
    /// stop early, leaving everything below unlaid (invisible, unreachable by scrolling).
    /// Lays out whatever was left.
    void ensureFullyLaidOut();

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
    void paintTable(QPainter &painter, const TableGrid &grid) const;
    bool tableHitTest(const QPoint &pos, QTextCursor *cursor) const;

    // Math and images: drawing
    void paintInlineObjects(QPainter &painter) const;
    /// Full-width backgrounds behind fenced code blocks (drawn before the text).
    void paintCodeBackgrounds(QPainter &painter) const;
    /// Re-style lines with images (after one finished loading).
    void restyleImageBlocks();
    /// Re-style every line (e.g. after a spelling change), then finish the layout.
    void restyleAll();

    MarkdownHighlighter *m_highlighter = nullptr;
    QList<QAction *> m_tableActions;
    bool m_inTable = false;
    std::unique_ptr<TableLayout> m_tableLayout;
    bool m_limitLineWidth = true;
    int m_readableWidth = DefaultReadableWidth;
    bool m_relayoutPending = false;
    bool m_imageRestylePending = false;
    bool m_layoutCheckPending = false;
    bool m_forwardingMouse = false; // a margin click on its way to the viewport
    ImageCache *m_imageCache = nullptr;
    SpellChecker m_spellChecker;
    std::function<bool()> m_saveRequester;
    mutable QHash<QString, QPixmap> m_scaledImages;
};
