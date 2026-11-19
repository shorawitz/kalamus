// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

class QTextDocument;

/// A heading in the document, for the outline panel.
struct OutlineHeading {
    int level = 1; ///< 1 to 6
    QString title; ///< plain text, Markdown markup removed
    int blockNumber = 0; ///< the heading's line
    bool operator==(const OutlineHeading &other) const
    {
        return level == other.level && title == other.title && blockNumber == other.blockNumber;
    }
};

namespace Outline
{
/// The # headings of a styled document, skipping lines inside code blocks, math and front matter.
QList<OutlineHeading> headings(const QTextDocument *document);
/// A heading line's text without its markup: "## **Bold** [link](url) ##" -> "Bold link".
QString plainTitle(const QString &headingText);
/// Index of the heading whose section contains @p blockNumber, or -1 above the first heading.
int sectionAt(const QList<OutlineHeading> &headings, int blockNumber);
}
