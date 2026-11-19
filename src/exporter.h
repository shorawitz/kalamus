// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QPrinter;
class QTextDocument;

/// Turns Markdown into other formats. HTML and PDF are built in; Word/ODT use pandoc.
namespace Exporter
{
/// GitHub-flavoured Markdown (tables, task lists, strikethrough, autolinks) to an HTML fragment.
QString toHtmlFragment(const QString &markdown);

/// A complete, styled HTML page. Math is rendered in the browser with KaTeX.
QString toHtmlDocument(const QString &markdown, const QString &title);

bool exportHtml(const QString &markdown, const QString &title, const QString &outputPath, QString *error);

/// The document exportPdf() prints: styled HTML with formulas and local images loaded, and
/// images sized to fit @p printer's page (inside a table, their column's share of it).
void buildPrintDocument(const QString &markdown, const QString &baseDir, const QPrinter &printer, QTextDocument &doc);

/// @p baseDir resolves relative image paths.
bool exportPdf(const QString &markdown, const QString &baseDir, const QString &outputPath, QString *error);

bool pandocAvailable();

/// @p format is a pandoc output format such as "docx" or "odt".
bool exportWithPandoc(const QString &markdown, const QString &format, const QString &baseDir, const QString &outputPath, QString *error);
}
