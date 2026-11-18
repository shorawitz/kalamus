// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowntable.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace
{
const QRegularExpression kDelimiterCell(uR"(^:?-+:?$)"_s);

MarkdownTable::Align alignmentOf(const QString &cell)
{
    const bool left = cell.startsWith(u':');
    const bool right = cell.endsWith(u':');
    if (left && right) {
        return MarkdownTable::Align::Center;
    }
    if (right) {
        return MarkdownTable::Align::Right;
    }
    return left ? MarkdownTable::Align::Left : MarkdownTable::Align::None;
}

QString delimiterCell(MarkdownTable::Align align, int width)
{
    switch (align) {
    case MarkdownTable::Align::Left:
        return u':' + QString(width - 1, u'-');
    case MarkdownTable::Align::Right:
        return QString(width - 1, u'-') + u':';
    case MarkdownTable::Align::Center:
        return u':' + QString(width - 2, u'-') + u':';
    case MarkdownTable::Align::None:
        break;
    }
    return QString(width, u'-');
}
} // namespace

QList<MarkdownTable::Cell> MarkdownTable::splitRow(const QString &line)
{
    QList<Cell> cells;
    const int n = line.size();

    auto addSegment = [&](int from, int to) {
        int start = from;
        int end = to;
        while (start < end && line.at(start).isSpace()) {
            ++start;
        }
        while (end > start && line.at(end - 1).isSpace()) {
            --end;
        }
        if (start == end) {
            start = end = qMin(from + 1, to); // empty cell: sit after "| "
        }
        cells.append({line.mid(start, end - start), start, end, from, to});
    };

    int i = 0;
    while (i < n && line.at(i).isSpace()) {
        ++i;
    }
    if (i < n && line.at(i) == u'|') {
        ++i;
    }
    int segmentStart = i;
    for (; i < n; ++i) {
        if (line.at(i) == u'\\') {
            ++i; // escaped character, e.g. \|
            continue;
        }
        if (line.at(i) == u'|') {
            addSegment(segmentStart, i);
            segmentStart = i + 1;
        }
    }
    if (segmentStart <= n && (!QStringView(line).mid(segmentStart).trimmed().isEmpty() || cells.isEmpty())) {
        addSegment(segmentStart, n);
    }
    return cells;
}

bool MarkdownTable::isDelimiterRow(const QString &line)
{
    if (!line.contains(u'|')) {
        return false;
    }
    const QList<Cell> cells = splitRow(line);
    if (cells.isEmpty()) {
        return false;
    }
    for (const Cell &cell : cells) {
        if (!kDelimiterCell.match(cell.text).hasMatch()) {
            return false;
        }
    }
    return true;
}

std::optional<MarkdownTable> MarkdownTable::parse(const QStringList &lines)
{
    if (lines.size() < 2 || !isDelimiterRow(lines.at(1))) {
        return std::nullopt;
    }

    MarkdownTable table;
    int columns = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QList<Cell> cells = splitRow(lines.at(i));
        if (i == 1) {
            for (const Cell &cell : cells) {
                table.m_align.append(alignmentOf(cell.text));
            }
            continue;
        }
        QStringList row;
        for (const Cell &cell : cells) {
            row.append(cell.text);
        }
        columns = qMax(columns, int(row.size()));
        table.m_rows.append(row);
    }

    // Normalise: every row and the alignment list get the same number of columns.
    for (QStringList &row : table.m_rows) {
        while (row.size() < columns) {
            row.append(QString());
        }
    }
    table.m_align.resize(columns, Align::None);
    return table;
}

MarkdownTable MarkdownTable::create(int columns, int bodyRows)
{
    MarkdownTable table;
    QStringList header;
    for (int c = 0; c < columns; ++c) {
        header.append(u"Column %1"_s.arg(c + 1));
    }
    table.m_rows.append(header);
    for (int r = 0; r < bodyRows; ++r) {
        table.m_rows.append(QStringList(columns, QString()));
    }
    table.m_align = QList<Align>(columns, Align::None);
    return table;
}

int MarkdownTable::rowCount() const
{
    return m_rows.size();
}

int MarkdownTable::columnCount() const
{
    return m_align.size();
}

QString MarkdownTable::cell(int row, int column) const
{
    if (row < 0 || row >= m_rows.size() || column < 0 || column >= m_rows.at(row).size()) {
        return {};
    }
    return m_rows.at(row).at(column);
}

bool MarkdownTable::isRowEmpty(int row) const
{
    for (const QString &text : m_rows.value(row)) {
        if (!text.isEmpty()) {
            return false;
        }
    }
    return true;
}

MarkdownTable::Align MarkdownTable::alignment(int column) const
{
    return m_align.value(column, Align::None);
}

void MarkdownTable::setAlignment(int column, Align align)
{
    if (column >= 0 && column < m_align.size()) {
        m_align[column] = align;
    }
}

void MarkdownTable::insertRow(int at)
{
    at = qBound(1, at, int(m_rows.size())); // never above the header
    m_rows.insert(at, QStringList(columnCount(), QString()));
}

void MarkdownTable::removeRow(int row)
{
    if (row >= 1 && row < m_rows.size()) {
        m_rows.removeAt(row);
    }
}

void MarkdownTable::insertColumn(int at)
{
    at = qBound(0, at, columnCount());
    for (QStringList &row : m_rows) {
        row.insert(at, QString());
    }
    m_align.insert(at, Align::None);
}

void MarkdownTable::removeColumn(int column)
{
    if (columnCount() <= 1 || column < 0 || column >= columnCount()) {
        return;
    }
    for (QStringList &row : m_rows) {
        row.removeAt(column);
    }
    m_align.removeAt(column);
}

QList<int> MarkdownTable::columnWidths() const
{
    QList<int> widths(columnCount(), 3);
    for (const QStringList &row : m_rows) {
        for (int c = 0; c < row.size(); ++c) {
            widths[c] = qMax(widths[c], int(row.at(c).size()));
        }
    }
    return widths;
}

QString MarkdownTable::toMarkdown() const
{
    const QList<int> widths = columnWidths();
    auto line = [&](const QStringList &cells) {
        QString out = u"|"_s;
        for (int c = 0; c < widths.size(); ++c) {
            out += u' ' + cells.value(c).leftJustified(widths[c], u' ') + u" |"_s;
        }
        return out;
    };

    QStringList lines;
    lines.append(line(m_rows.value(0)));
    QStringList delimiter;
    for (int c = 0; c < widths.size(); ++c) {
        delimiter.append(delimiterCell(m_align[c], widths[c]));
    }
    lines.append(line(delimiter));
    for (int r = 1; r < m_rows.size(); ++r) {
        lines.append(line(m_rows.at(r)));
    }
    return lines.join(u'\n');
}

int MarkdownTable::cellOffset(int row, int column) const
{
    const QList<int> widths = columnWidths();
    int lineLength = 1; // leading "|"
    for (int w : widths) {
        lineLength += w + 3; // " cell |"
    }
    const int lineIndex = row == 0 ? 0 : row + 1; // skip the delimiter line
    int offset = lineIndex * (lineLength + 1) + 2;
    for (int c = 0; c < column && c < widths.size(); ++c) {
        offset += widths[c] + 3;
    }
    return offset;
}
