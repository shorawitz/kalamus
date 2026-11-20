// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outline.h"
#include "markdownhighlighter.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

using namespace Qt::StringLiterals;

namespace
{
const QRegularExpression kHeading(uR"(^ {0,3}(#{1,6})(?:[ \t]+(.*?))?[ \t]*$)"_s);
const QRegularExpression kClosingHashes(uR"((?:^|[ \t]+)#+[ \t]*$)"_s);
const QRegularExpression kImage(uR"(!\[([^\]]*)\]\([^)]*\))"_s);
const QRegularExpression kLink(uR"(\[([^\]]+)\]\([^)]*\))"_s);
const QRegularExpression kStrong(uR"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)"_s);
const QRegularExpression kEmphasis(uR"((?<![*\w])([*_])(?=[^\s*_])(.+?)(?<=[^\s*_])\1(?![*\w]))"_s);
const QRegularExpression kStrike(uR"(~~(?=\S)(.+?)(?<=\S)~~)"_s);
const QRegularExpression kCode(uR"((`+)(.+?)\1)"_s);
const QRegularExpression kEscape(uR"(\\([\\`*_{}\[\]()#+\-.!|~$<>]))"_s);
} // namespace

QString Outline::plainTitle(const QString &headingText)
{
    QString title = headingText;
    title.remove(kClosingHashes);
    title.replace(kImage, u"\\1"_s);
    title.replace(kLink, u"\\1"_s);
    title.replace(kCode, u"\\2"_s);
    title.replace(kStrong, u"\\2"_s);
    title.replace(kEmphasis, u"\\2"_s);
    title.replace(kStrike, u"\\1"_s);
    title.replace(kEscape, u"\\1"_s);
    return title.simplified();
}

QList<OutlineHeading> Outline::headings(const QTextDocument *document)
{
    QList<OutlineHeading> result;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        // The highlighter gives lines inside code, math and front matter their own state;
        // a heading line is a plain one (or not styled yet).
        const int state = block.userState();
        if (state == MarkdownHighlighter::SetextHeading1 || state == MarkdownHighlighter::SetextHeading2) {
            // Heading text with === or --- under it; it may run over several lines.
            const int first = block.blockNumber();
            QString text = block.text();
            while (block.next().isValid() && block.next().userState() == state) {
                block = block.next();
                text += u' ' + block.text();
            }
            const QString title = plainTitle(text);
            if (!title.isEmpty()) {
                result.append({state == MarkdownHighlighter::SetextHeading1 ? 1 : 2, title, first});
            }
            continue;
        }
        if (state > 0) {
            continue;
        }
        const auto m = kHeading.match(block.text());
        if (!m.hasMatch()) {
            continue;
        }
        const QString title = plainTitle(m.captured(2));
        if (title.isEmpty()) {
            continue;
        }
        result.append({int(m.capturedLength(1)), title, block.blockNumber()});
    }
    return result;
}

int Outline::sectionAt(const QList<OutlineHeading> &headings, int blockNumber)
{
    int section = -1;
    for (int i = 0; i < headings.size() && headings.at(i).blockNumber <= blockNumber; ++i) {
        section = i;
    }
    return section;
}
