// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

// Spell checking: the switches, and the right-click menu for a misspelled word.

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QMenu>
#include <QTextBlock>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
constexpr int MaxSuggestions = 8;
}

void MarkdownEditor::setSpellCheckingEnabled(bool enabled)
{
    if (enabled != m_spellChecker.isEnabled()) {
        m_spellChecker.setEnabled(enabled);
        restyleAll();
    }
}

bool MarkdownEditor::spellCheckingEnabled() const
{
    return m_spellChecker.isEnabled();
}

void MarkdownEditor::setSpellCheckingLanguage(const QString &language)
{
    m_spellChecker.setLanguage(language);
    if (m_spellChecker.isEnabled()) {
        restyleAll();
    }
}

QString MarkdownEditor::spellCheckingLanguage() const
{
    return m_spellChecker.language();
}

void MarkdownEditor::restyleAll()
{
    m_tableLayout->clear();
    m_highlighter->rehighlight();
    QMetaObject::invokeMethod(this, &MarkdownEditor::ensureFullyLaidOut, Qt::QueuedConnection);
}

QTextCursor MarkdownEditor::misspelledWordAt(int position) const
{
    if (!m_spellChecker.isEnabled()) {
        return {};
    }
    const QTextBlock block = document()->findBlock(position);
    // Only text the highlighter checked: a misspelled word carries the spelling underline.
    const int column = position - block.position();
    const auto words = SpellChecker::wordsToCheck(block.text());
    for (const auto &word : words) {
        if (column < word.start || column > word.start + word.length) {
            continue;
        }
        const auto formats = block.layout()->formats();
        const bool underlined = std::any_of(formats.cbegin(), formats.cend(), [&](const QTextLayout::FormatRange &range) {
            return range.start <= word.start && range.start + range.length > word.start
                && range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline;
        });
        if (!underlined) {
            return {};
        }
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + word.start);
        cursor.setPosition(block.position() + word.start + word.length, QTextCursor::KeepAnchor);
        return cursor;
    }
    return {};
}

void MarkdownEditor::acceptWord(const QString &word, bool permanently)
{
    if (permanently) {
        m_spellChecker.addToDictionary(word);
    } else {
        m_spellChecker.ignore(word);
    }
    m_tableLayout->clear();
    viewport()->update();
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (block.text().contains(word)) {
            m_highlighter->rehighlightBlock(block);
        }
    }
}

void MarkdownEditor::addSpellingActions(QMenu *menu, int position)
{
    const QTextCursor word = misspelledWordAt(position);
    if (word.isNull() || isReadOnly()) {
        return;
    }
    const QString text = word.selectedText();
    QAction *first = menu->actions().value(0);

    auto add = [&](QAction *action) {
        menu->insertAction(first, action);
    };

    const QStringList suggestions = m_spellChecker.suggestions(text).mid(0, MaxSuggestions);
    if (suggestions.isEmpty()) {
        auto *none = new QAction(i18n("No Suggestions"), menu);
        none->setEnabled(false);
        add(none);
    }
    for (const QString &suggestion : suggestions) {
        auto *action = new QAction(suggestion, menu);
        QFont bold = action->font();
        bold.setBold(true);
        action->setFont(bold);
        connect(action, &QAction::triggered, this, [this, word, suggestion] {
            QTextCursor replace = word;
            replace.insertText(suggestion);
        });
        add(action);
    }
    menu->insertSeparator(first);

    auto *addToDictionary = new QAction(QIcon::fromTheme(u"list-add"_s), i18n("Add to Dictionary"), menu);
    connect(addToDictionary, &QAction::triggered, this, [this, text] {
        acceptWord(text, true);
    });
    add(addToDictionary);
    auto *ignore = new QAction(QIcon::fromTheme(u"tools-check-spelling"_s), i18n("Ignore"), menu);
    connect(ignore, &QAction::triggered, this, [this, text] {
        acceptWord(text, false);
    });
    add(ignore);
    menu->insertSeparator(first);
}
