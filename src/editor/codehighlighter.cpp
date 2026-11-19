// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "codehighlighter.h"

#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Theme>

using namespace Qt::StringLiterals;

CodeHighlighter &CodeHighlighter::instance()
{
    static CodeHighlighter highlighter;
    return highlighter;
}

CodeHighlighter::CodeHighlighter()
{
    setTheme(m_repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme));
}

void CodeHighlighter::setDarkTheme(bool dark)
{
    if (dark != m_dark || !theme().isValid()) {
        m_dark = dark;
        setTheme(m_repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme : KSyntaxHighlighting::Repository::LightTheme));
    }
}

KSyntaxHighlighting::Definition CodeHighlighter::definitionFor(const QString &language)
{
    const QString key = language.trimmed().toLower();
    if (key.isEmpty()) {
        return {};
    }
    if (const auto it = m_definitions.constFind(key); it != m_definitions.cend()) {
        return *it;
    }

    // Names people write after ``` that aren't a syntax name or a file extension.
    static const QHash<QString, QString> aliases{
        {u"sh"_s, u"Bash"_s},         {u"shell"_s, u"Bash"_s},     {u"console"_s, u"Bash"_s},   {u"zsh"_s, u"Zsh"_s},
        {u"c++"_s, u"C++"_s},         {u"golang"_s, u"Go"_s},      {u"docker"_s, u"Dockerfile"_s},
        {u"patch"_s, u"Diff"_s},      {u"text"_s, QString()},      {u"plaintext"_s, QString()}, {u"txt"_s, QString()},
        {u"jsonc"_s, u"JSON"_s},      {u"vue"_s, u"HTML"_s},       {u"tex"_s, u"LaTeX"_s},      {u"ps1"_s, u"PowerShell"_s},
        {u"powershell"_s, u"PowerShell"_s},
    };

    KSyntaxHighlighting::Definition definition;
    if (const auto alias = aliases.constFind(key); alias != aliases.cend()) {
        if (!alias->isEmpty()) {
            definition = m_repository.definitionForName(*alias);
        }
    } else {
        // A syntax name, any case ("python", "YAML"), then a file extension ("py", "yml").
        const auto all = m_repository.definitions();
        for (const auto &candidate : all) {
            if (candidate.name().compare(key, Qt::CaseInsensitive) == 0
                || candidate.name().compare(QString(key).replace(u'-', u' '), Qt::CaseInsensitive) == 0) {
                definition = candidate;
                break;
            }
        }
        if (!definition.isValid()) {
            definition = m_repository.definitionForFileName(u"file."_s + key);
        }
        if (!definition.isValid() && key == u"dockerfile"_s) {
            definition = m_repository.definitionForFileName(u"Dockerfile"_s);
        }
    }
    m_definitions.insert(key, definition);
    return definition;
}

KSyntaxHighlighting::State CodeHighlighter::highlight(const QString &text,
                                                      const KSyntaxHighlighting::State &state,
                                                      const KSyntaxHighlighting::Definition &definition,
                                                      QList<Span> *spans)
{
    if (definition != this->definition()) {
        setDefinition(definition);
    }
    m_spans = spans;
    const auto next = highlightLine(text, state);
    m_spans = nullptr;
    return next;
}

void CodeHighlighter::applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format)
{
    if (!m_spans || length <= 0 || format.isDefaultTextStyle(theme())) {
        return;
    }
    QTextCharFormat f;
    if (format.hasTextColor(theme())) {
        f.setForeground(format.textColor(theme()));
    }
    if (format.hasBackgroundColor(theme())) {
        f.setBackground(format.backgroundColor(theme()));
    }
    if (format.isBold(theme())) {
        f.setFontWeight(QFont::Bold);
    }
    if (format.isItalic(theme())) {
        f.setFontItalic(true);
    }
    if (format.isUnderline(theme())) {
        f.setFontUnderline(true);
    }
    if (format.isStrikeThrough(theme())) {
        f.setFontStrikeOut(true);
    }
    m_spans->append({offset, length, f});
}

QString CodeHighlighter::languageFromShebang(const QString &firstLine)
{
    if (!firstLine.startsWith(u"#!"_s)) {
        return {};
    }
    // "#!/usr/bin/env python3 -u" -> "python3", "#!/bin/bash" -> "bash"
    QStringList words = firstLine.mid(2).trimmed().split(u' ', Qt::SkipEmptyParts);
    if (!words.isEmpty() && words.first().endsWith(u"/env"_s)) {
        words.removeFirst();
        while (!words.isEmpty() && words.first().startsWith(u'-')) {
            words.removeFirst(); // env -S ...
        }
    }
    if (words.isEmpty()) {
        return {};
    }
    QString interpreter = words.first().section(u'/', -1);
    while (!interpreter.isEmpty() && (interpreter.back().isDigit() || interpreter.back() == u'.')) {
        interpreter.chop(1); // python3.12 -> python
    }
    static const QHash<QString, QString> names{{u"node"_s, u"javascript"_s}, {u"nodejs"_s, u"javascript"_s}, {u"deno"_s, u"typescript"_s},
                                               {u"pwsh"_s, u"powershell"_s}, {u"dash"_s, u"bash"_s},          {u"ksh"_s, u"bash"_s}};
    return names.value(interpreter, interpreter);
}

QStringList CodeHighlighter::languageChoices()
{
    // Common ones first (as people write them after ```), then every other language by name.
    QStringList choices{u"python"_s, u"bash"_s, u"javascript"_s, u"typescript"_s, u"json"_s, u"yaml"_s, u"cpp"_s, u"c"_s,
                        u"java"_s,   u"go"_s,   u"rust"_s,       u"sql"_s,        u"html"_s, u"css"_s,  u"xml"_s, u"markdown"_s,
                        u"diff"_s,   u"dockerfile"_s, u"ini"_s,  u"powershell"_s};
    QStringList others;
    const auto all = m_repository.definitions();
    for (const auto &definition : all) {
        if (definition.isHidden()) {
            continue;
        }
        const QString name = definition.name().toLower().replace(u' ', u'-');
        if (!choices.contains(name)) {
            others.append(name);
        }
    }
    others.sort(Qt::CaseInsensitive);
    others.removeDuplicates();
    return choices + others;
}
