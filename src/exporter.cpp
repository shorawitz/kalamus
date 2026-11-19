// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporter.h"
#include "codehighlighter.h"
#include "mathrenderer.h"

#include <QDir>
#include <QMap>
#include <QFile>
#include <QImageReader>
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

/**
 * Loads the document's local images into @p doc (Qt's own loader trips over encoded
 * paths) and gives large ones a width that fits the page. Images that can't be loaded,
 * such as web images, print as their alt text instead of a broken-image icon.
 */
QString embedImages(const QString &html, const QString &baseDir, const QPrinter &printer, QTextDocument &doc)
{
    // QTextDocument::print() lays out in screen pixels with a 2 cm margin on each side.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchX() : 96.0;
    const qreal maxWidth = (printer.pageLayout().fullRect(QPageLayout::Inch).width() - 2 * 2.0 / 2.54) * dpi;

    static const QRegularExpression img(uR"re(<img src="([^"]+)"([^>]*)>)re"_s);
    static const QRegularExpression alt(uR"re(alt="([^"]*)")re"_s);
    QString out;
    qsizetype last = 0;
    int index = 0;
    for (auto it = img.globalMatch(html); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(html).mid(last, m.capturedStart(0) - last);
        last = m.capturedEnd(0);
        const QString src = m.captured(1);
        if (src.startsWith(u"kalamus-math:"_s)) {
            out += m.captured(0);
            continue;
        }
        QImage image;
        if (!src.contains(u"://"_s) && !src.startsWith(u"data:"_s)) {
            QImageReader reader(QDir(baseDir).filePath(QUrl::fromPercentEncoding(src.toUtf8())));
            reader.setAutoTransform(true);
            image = reader.read();
        }
        if (image.isNull()) {
            out += u"<i>"_s + alt.match(m.captured(2)).captured(1) + u"</i>"_s;
            continue;
        }
        const QUrl url(u"kalamus-image:%1"_s.arg(index++));
        doc.addResource(QTextDocument::ImageResource, url, image);
        const qreal width = qMin<qreal>(image.width(), maxWidth);
        out += u"<img src=\""_s + url.toString() + u"\" width=\""_s + QString::number(qFloor(width)) + u"\">"_s;
    }
    out += QStringView(html).mid(last);
    return out;
}

qreal screenPixelsPerPoint()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    return (screen ? screen->logicalDotsPerInchY() : 96.0) / 72.0;
}

// <pre lang="cpp"><code>...</code></pre>, as cmark writes a fenced code block.
const QRegularExpression kCodeBlock(uR"re(<pre(?: lang="([^"]*)")?><code>([\s\S]*?)</code></pre>)re"_s);

QString unescapeHtml(QString text)
{
    // cmark escapes exactly these four.
    return text.replace(u"&lt;"_s, u"<"_s).replace(u"&gt;"_s, u">"_s).replace(u"&quot;"_s, u"\""_s).replace(u"&amp;"_s, u"&"_s);
}

QString cssFor(const KSyntaxHighlighting::Format &format, const KSyntaxHighlighting::Theme &theme)
{
    QString css;
    if (format.hasTextColor(theme)) {
        css += u"color:"_s + format.textColor(theme).name() + u';';
    }
    if (format.hasBackgroundColor(theme)) {
        css += u"background-color:"_s + format.backgroundColor(theme).name() + u';';
    }
    if (format.isBold(theme)) {
        css += u"font-weight:bold;"_s;
    }
    if (format.isItalic(theme)) {
        css += u"font-style:italic;"_s;
    }
    if (format.isUnderline(theme) || format.isStrikeThrough(theme)) {
        css += u"text-decoration:"_s + (format.isUnderline(theme) ? u"underline "_s : QString())
            + (format.isStrikeThrough(theme) ? u"line-through"_s : QString()) + u';';
    }
    return css;
}

/**
 * Colours the fenced code blocks in cmark's HTML with Kate's syntax definitions.
 * With @p styleSheet, spans get classes and the style sheet gets Breeze light colours plus
 * Breeze dark ones for readers in dark mode. Without it (PDF, where Qt's rich text has
 * no media queries), the light colours go inline.
 */
QString colorCodeBlocks(const QString &html, QString *styleSheet)
{
    auto &highlighter = CodeHighlighter::instance();
    const auto light = highlighter.defaultTheme(false);
    const auto dark = highlighter.defaultTheme(true);
    QMap<int, KSyntaxHighlighting::Format> used;

    QString out;
    qsizetype last = 0;
    for (auto it = kCodeBlock.globalMatch(html); it.hasNext();) {
        const auto m = it.next();
        const QString code = unescapeHtml(m.captured(2));
        QString language = m.captured(1);
        if (language.isEmpty()) {
            language = CodeHighlighter::languageFromShebang(code.section(u'\n', 0, 0));
        }
        const auto definition = highlighter.definitionFor(language);
        if (!definition.isValid()) {
            continue; // left as cmark wrote it
        }

        QString colored;
        KSyntaxHighlighting::State state;
        const QStringList lines = code.split(u'\n');
        for (qsizetype n = 0; n < lines.size(); ++n) {
            const QString &line = lines.at(n);
            QList<CodeHighlighter::FormatSpan> spans;
            state = highlighter.highlightFormats(line, state, definition, &spans);
            int pos = 0;
            for (const auto &span : std::as_const(spans)) {
                if (span.start < pos) {
                    continue;
                }
                colored += line.mid(pos, span.start - pos).toHtmlEscaped();
                const QString text = line.mid(span.start, span.length).toHtmlEscaped();
                pos = span.start + span.length;
                if (styleSheet) {
                    if (span.format.isDefaultTextStyle(light) && span.format.isDefaultTextStyle(dark)) {
                        colored += text;
                        continue;
                    }
                    used.insert(span.format.id(), span.format);
                    colored += u"<span class=\"hl%1\">"_s.arg(span.format.id()) + text + u"</span>"_s;
                } else {
                    const QString css = span.format.isDefaultTextStyle(light) ? QString() : cssFor(span.format, light);
                    colored += css.isEmpty() ? text : u"<span style=\""_s + css + u"\">"_s + text + u"</span>"_s;
                }
            }
            colored += line.mid(pos).toHtmlEscaped();
            if (n + 1 < lines.size()) {
                colored += u'\n';
            }
        }

        out += QStringView(html).mid(last, m.capturedStart(2) - last);
        out += colored;
        last = m.capturedEnd(2);
    }
    out += QStringView(html).mid(last);

    if (styleSheet) {
        QString lightCss;
        QString darkCss;
        for (auto it = used.cbegin(); it != used.cend(); ++it) {
            const QString selector = u".hl%1{"_s.arg(it.key());
            lightCss += selector + cssFor(*it, light) + u"}\n"_s;
            darkCss += selector + cssFor(*it, dark) + u"}\n"_s;
        }
        *styleSheet = used.isEmpty() ? QString() : lightCss + u"@media (prefers-color-scheme: dark) {\n"_s + darkCss + u"}\n"_s;
    }
    return out;
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
    QString codeCss;
    const QString body = restoreMath(colorCodeBlocks(toHtmlFragment(protectMath(markdown, &math)), &codeCss), math, [&](int i, bool block) -> QString {
        const MathItem &item = math.at(i);
        const QString tex = item.latex.toHtmlEscaped();
        if (block) {
            return u"<div class=\"math\">$$"_s + tex + u"$$</div>"_s;
        }
        return item.display ? u"<span class=\"math\">$$"_s + tex + u"$$</span>"_s : u"<span class=\"math\">$"_s + tex + u"$</span>"_s;
    });
    if (!codeCss.isEmpty()) {
        page += u"<style>\n"_s + codeCss + u"</style>\n"_s;
    }
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
    const qreal pixelSize = MathRenderer::instance().matchingPixelSize(doc.defaultFont(), doc.defaultFont().pointSizeF() * screenPixelsPerPoint());
    const QString html = restoreMath(colorCodeBlocks(toHtmlFragment(protectMath(markdown, &math)), nullptr), math, [&](int i, bool block) -> QString {
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
    doc.setHtml(embedImages(html, baseDir, printer, doc));
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
