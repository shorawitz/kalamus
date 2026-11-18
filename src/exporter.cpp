// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporter.h"
#include "mathrenderer.h"

#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QPrinter>
#include <QProcess>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>

#include <KLocalizedString>

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <algorithm>
#include <cstdlib>
#include <functional>

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
struct MathItem {
    QString latex;
    bool display = false;
};

const QRegularExpression kFence(uR"(^ {0,3}(`{3,}|~{3,}))"_s);
const QRegularExpression kCodeSpan(uR"((`+)(.+?)(?<!`)\1(?!`))"_s);
const QRegularExpression kInlineDisplayMath(uR"((?<![\\$])\$\$(?=\S)([^$\n]+?)(?<=\S)\$\$)"_s);
const QRegularExpression kInlineMath(uR"((?<![\\$])\$(?=\S)([^$\n]+?)(?<=\S)\$(?![\d$]))"_s);

QString placeholder(int index)
{
    return u"KALAMUSMATH%1X"_s.arg(index);
}

/**
 * Swaps every formula for a placeholder, so the Markdown converter cannot touch the
 * LaTeX (it would treat backslashes as escapes). Code blocks and code spans are skipped,
 * using the same rules as the editor.
 */
QString protectMath(const QString &markdown, QList<MathItem> *items)
{
    const QStringList lines = markdown.split(u'\n');
    QStringList out;
    QChar fence;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (!fence.isNull()) {
            if (trimmed.size() >= 3 && trimmed == QString(trimmed.size(), fence)) {
                fence = QChar();
            }
            out.append(line);
            continue;
        }
        if (const auto m = kFence.match(line); m.hasMatch()) {
            fence = m.captured(1).at(0);
            out.append(line);
            continue;
        }

        if (trimmed.startsWith(u"$$"_s)) {
            if (trimmed.size() > 4 && trimmed.endsWith(u"$$"_s)) {
                items->append({trimmed.mid(2, trimmed.size() - 4).trimmed(), true});
                out.append(placeholder(items->size() - 1));
                continue;
            }
            QString latex = trimmed.mid(2);
            int closing = -1;
            for (int j = i + 1; j < lines.size() && closing < 0; ++j) {
                const QString t = lines.at(j).trimmed();
                if (t.endsWith(u"$$"_s)) {
                    latex += u'\n' + t.chopped(2);
                    closing = j;
                } else {
                    latex += u'\n' + lines.at(j);
                }
            }
            if (closing >= 0) {
                items->append({latex.trimmed(), true});
                out.append(placeholder(items->size() - 1));
                i = closing;
                continue;
            }
            // Never closed: treat the line as text.
        }

        // Inline formulas outside code spans, replaced from the end so offsets stay valid.
        QList<bool> taken(line.size(), false);
        for (auto it = kCodeSpan.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            for (int k = m.capturedStart(0); k < m.capturedEnd(0); ++k) {
                taken[k] = true;
            }
        }
        struct Hit {
            int start;
            int length;
            MathItem item;
        };
        QList<Hit> hits;
        for (const auto *pattern : {&kInlineDisplayMath, &kInlineMath}) {
            for (auto it = pattern->globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                bool free = true;
                for (int k = m.capturedStart(0); k < m.capturedEnd(0) && free; ++k) {
                    free = !taken[k];
                }
                if (!free) {
                    continue;
                }
                for (int k = m.capturedStart(0); k < m.capturedEnd(0); ++k) {
                    taken[k] = true;
                }
                hits.append({int(m.capturedStart(0)), int(m.capturedLength(0)), {m.captured(1), pattern == &kInlineDisplayMath}});
            }
        }
        std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
            return a.start > b.start;
        });
        QString result = line;
        for (const Hit &hit : std::as_const(hits)) {
            items->append(hit.item);
            result.replace(hit.start, hit.length, placeholder(items->size() - 1));
        }
        out.append(result);
    }
    return out.join(u'\n');
}

/// Puts formulas back into converted HTML. @p replace gives the HTML for one formula.
QString restoreMath(QString html, const QList<MathItem> &items, const std::function<QString(int, bool block)> &replace)
{
    // Highest index first, so KALAMUSMATH1X never matches inside KALAMUSMATH12X.
    for (int i = items.size() - 1; i >= 0; --i) {
        html.replace(u"<p>"_s + placeholder(i) + u"</p>"_s, replace(i, true));
        html.replace(placeholder(i), replace(i, false));
    }
    return html;
}

qreal screenPixelsPerPoint()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    return (screen ? screen->logicalDotsPerInchY() : 96.0) / 72.0;
}
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
    QList<MathItem> math;
    const QString body = restoreMath(toHtmlFragment(protectMath(markdown, &math)), math, [&](int i, bool block) -> QString {
        const MathItem &item = math.at(i);
        const QString tex = item.latex.toHtmlEscaped();
        if (block) {
            return u"<div class=\"math\">$$"_s + tex + u"$$</div>"_s;
        }
        return item.display ? u"<span class=\"math\">$$"_s + tex + u"$$</span>"_s : u"<span class=\"math\">$"_s + tex + u"$</span>"_s;
    });
    page += u"</head>\n<body>\n"_s + body + u"</body>\n</html>\n"_s;
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
    // QTextDocument::print() adds its own 2 cm margin; printer margins would double it.
    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setBaseUrl(QUrl::fromLocalFile(baseDir + u'/'));
    doc.setDefaultStyleSheet(kPrintCss);
    // Formulas become images, rendered at 4x for crisp print.
    QList<MathItem> math;
    const qreal pixelSize = doc.defaultFont().pointSizeF() * screenPixelsPerPoint();
    const QString html = restoreMath(toHtmlFragment(protectMath(markdown, &math)), math, [&](int i, bool block) -> QString {
        const MathItem &item = math.at(i);
        const auto metrics = MathRenderer::instance().measure(item.latex, item.display, pixelSize);
        if (!metrics.ok) {
            const QString source = (item.display ? u"$$"_s : u"$"_s) + item.latex + (item.display ? u"$$"_s : u"$"_s);
            return u"<code>"_s + source.toHtmlEscaped() + u"</code>"_s;
        }
        const QUrl url(u"kalamus-math:%1"_s.arg(i));
        doc.addResource(QTextDocument::ImageResource, url, MathRenderer::instance().render(item.latex, item.display, pixelSize, Qt::black, 4.0));
        const QString img = u"<img src=\"%1\" width=\"%2\" height=\"%3\" style=\"vertical-align: middle\">"_s.arg(url.toString())
                                .arg(qRound(metrics.width))
                                .arg(qRound(metrics.ascent + metrics.descent));
        return block ? u"<p align=\"center\">"_s + img + u"</p>"_s : img;
    });
    doc.setHtml(html);
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
