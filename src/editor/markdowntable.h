// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * A GitHub-flavoured Markdown table: parse it from source lines, edit it as a
 * grid, and write it back neatly aligned.
 */
class MarkdownTable
{
public:
    enum class Align {
        None,
        Left,
        Center,
        Right,
    };

    /// One cell of a source line. Positions are offsets into that line.
    struct Cell {
        QString text; ///< trimmed cell content, escapes kept as written
        int start = 0; ///< where the content starts
        int end = 0; ///< where the content ends
        int segmentStart = 0; ///< just after the opening pipe
        int segmentEnd = 0; ///< at the closing pipe (or end of line)
    };

    static QList<Cell> splitRow(const QString &line);
    /// A `| --- | :-: |` line (it must contain a pipe, so `---` stays a rule).
    static bool isDelimiterRow(const QString &line);

    /// @p lines: header, delimiter, then body rows.
    static std::optional<MarkdownTable> parse(const QStringList &lines);
    /// A new table with "Column N" headers and empty body rows.
    static MarkdownTable create(int columns, int bodyRows);

    int rowCount() const; ///< header included
    int columnCount() const;
    QString cell(int row, int column) const;
    bool isRowEmpty(int row) const;
    Align alignment(int column) const;

    void setAlignment(int column, Align align);
    void insertRow(int at);
    void removeRow(int row);
    void insertColumn(int at);
    void removeColumn(int column);

    /// Aligned Markdown, no trailing newline.
    QString toMarkdown() const;
    /// Where the content of a cell starts inside toMarkdown().
    int cellOffset(int row, int column) const;

private:
    QList<int> columnWidths() const;

    QList<QStringList> m_rows; // m_rows[0] is the header
    QList<Align> m_align;
};
