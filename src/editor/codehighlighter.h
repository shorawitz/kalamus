// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

#include <QHash>
#include <QList>
#include <QTextCharFormat>

/**
 * Syntax colours for fenced code blocks, using KDE's KSyntaxHighlighting (Kate's engine)
 * with the Breeze light or dark theme. One shared instance; it highlights a line at a time.
 */
class CodeHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    struct Span {
        int start = 0;
        int length = 0;
        QTextCharFormat format;
    };

    /// A span with its theme-independent format, for exports that style both themes.
    struct FormatSpan {
        int start = 0;
        int length = 0;
        KSyntaxHighlighting::Format format;
    };

    static CodeHighlighter &instance();

    /// The language for a fence's info string ("cpp", "Python", "sh", ...); invalid if unknown.
    KSyntaxHighlighting::Definition definitionFor(const QString &language);

    /// The language a script's "#!" line names ("#!/usr/bin/env python3" -> "python"); "" if none.
    static QString languageFromShebang(const QString &firstLine);

    /// Names to offer when inserting a code block: common ones first, then all languages.
    QStringList languageChoices();

    /// Pick the Breeze light or dark colours.
    void setDarkTheme(bool dark);

    /// Colours one line. @p state comes from the line above; the result goes to the line below.
    KSyntaxHighlighting::State
    highlight(const QString &text, const KSyntaxHighlighting::State &state, const KSyntaxHighlighting::Definition &definition, QList<Span> *spans);

    /// Like highlight(), but keeps the formats themselves (every span, default style included).
    KSyntaxHighlighting::State highlightFormats(const QString &text,
                                                const KSyntaxHighlighting::State &state,
                                                const KSyntaxHighlighting::Definition &definition,
                                                QList<FormatSpan> *spans);

    /// The Breeze light or dark theme (whichever the editor uses right now).
    KSyntaxHighlighting::Theme defaultTheme(bool dark) const;

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;

private:
    CodeHighlighter();

    KSyntaxHighlighting::Repository m_repository;
    QHash<QString, KSyntaxHighlighting::Definition> m_definitions;
    QList<Span> *m_spans = nullptr;
    QList<FormatSpan> *m_formatSpans = nullptr;
    bool m_dark = false;
};
