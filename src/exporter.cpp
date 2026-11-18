// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporter.h"

#include <QFile>
#include <QPrinter>
#include <QProcess>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>

#include <KLocalizedString>

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace
{
const char *const kExtensions[] = {"table", "strikethrough", "autolink", "tagfilter", "tasklist"};

const auto kPageCss = uR"(
body { max-width: 46em; margin: 2em auto; padding: 0 1em; font-family: system-ui, sans-serif;
       line-height: 1.6; color: #1f2328; background: #ffffff; }
@media (prefers-color-scheme: dark) { body { color: #e6edf3; background: #0d1117; } }
h1, h2 { border-bottom: 1px solid #8884; padding-bottom: .3em; }
code, pre { font-family: ui-monospace, monospace; background: #8882; border-radius: 4px; }
code { padding: .1em .3em; }
pre { padding: 1em; overflow-x: auto; }
pre code { background: none; padding: 0; }
blockquote { margin: 0; padding: 0 1em; border-left: 4px solid #8886; color: #8b949e; }
table { border-collapse: collapse; }
th, td { border: 1px solid #8886; padding: .4em .8em; }
img { max-width: 100%; }
)"_s;

// Qt's rich-text engine understands a subset of CSS; keep the PDF stylesheet simple.
const auto kPrintCss = uR"(
h1, h2, h3, h4, h5, h6 { font-weight: bold; }
code, pre { font-family: monospace; background-color: #f0f0f0; }
blockquote { color: #555555; margin-left: 16px; }
table { border-collapse: collapse; }
th, td { border: 1px solid #999999; padding: 4px 8px; }
)"_s;
} // namespace

QString Exporter::toHtmlFragment(const QString &markdown)
{
    cmark_gfm_core_extensions_ensure_registered();

    const int options = CMARK_OPT_DEFAULT | CMARK_OPT_FOOTNOTES | CMARK_OPT_GITHUB_PRE_LANG;
    cmark_parser *parser = cmark_parser_new(options);
    for (const char *name : kExtensions) {
        if (cmark_syntax_extension *ext = cmark_find_syntax_extension(name)) {
            cmark_parser_attach_syntax_extension(parser, ext);
        }
    }

    const QByteArray utf8 = markdown.toUtf8();
    cmark_parser_feed(parser, utf8.constData(), utf8.size());
    cmark_node *root = cmark_parser_finish(parser);

    char *html = cmark_render_html(root, options, cmark_parser_get_syntax_extensions(parser));
    const QString result = QString::fromUtf8(html);

    std::free(html);
    cmark_node_free(root);
    cmark_parser_free(parser);
    return result;
}

QString Exporter::toHtmlDocument(const QString &markdown, const QString &title)
{
    QString page;
    page += u"<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"_s;
    page += u"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"_s;
    page += u"<title>"_s + title.toHtmlEscaped() + u"</title>\n"_s;
    page += u"<style>"_s + kPageCss + u"</style>\n"_s;
    page += uR"(<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css">
<script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js"></script>
<script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js"
  onload="renderMathInElement(document.body, {delimiters: [{left: '$$', right: '$$', display: true}, {left: '$', right: '$', display: false}]});"></script>
)"_s;
    page += u"</head>\n<body>\n"_s + toHtmlFragment(markdown) + u"</body>\n</html>\n"_s;
    return page;
}

bool Exporter::exportHtml(const QString &markdown, const QString &title, const QString &outputPath, QString *error)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }
    file.write(toHtmlDocument(markdown, title).toUtf8());
    return true;
}

bool Exporter::exportPdf(const QString &markdown, const QString &baseDir, const QString &outputPath, QString *error)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outputPath);
    printer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setBaseUrl(QUrl::fromLocalFile(baseDir + u'/'));
    doc.setDefaultStyleSheet(kPrintCss);
    doc.setHtml(toHtmlFragment(markdown));
    doc.print(&printer);

    if (!QFile::exists(outputPath)) {
        *error = i18n("The PDF could not be written.");
        return false;
    }
    return true;
}

bool Exporter::pandocAvailable()
{
    return !QStandardPaths::findExecutable(u"pandoc"_s).isEmpty();
}

bool Exporter::exportWithPandoc(const QString &markdown, const QString &format, const QString &baseDir, const QString &outputPath, QString *error)
{
    QProcess pandoc;
    pandoc.setWorkingDirectory(baseDir);
    pandoc.start(u"pandoc"_s,
                 {u"--from=gfm+tex_math_dollars"_s,
                  u"--to="_s + format,
                  u"--resource-path="_s + baseDir,
                  u"--output="_s + outputPath});
    if (!pandoc.waitForStarted()) {
        *error = i18n("Could not start pandoc. Install it with: sudo apt install pandoc");
        return false;
    }
    pandoc.write(markdown.toUtf8());
    pandoc.closeWriteChannel();
    if (!pandoc.waitForFinished(120000) || pandoc.exitStatus() != QProcess::NormalExit || pandoc.exitCode() != 0) {
        *error = QString::fromUtf8(pandoc.readAllStandardError()).trimmed();
        if (error->isEmpty()) {
            *error = i18n("pandoc failed.");
        }
        return false;
    }
    return true;
}
