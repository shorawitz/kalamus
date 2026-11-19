// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

namespace Sonnet
{
class Speller;
}

/**
 * Spell checking for Markdown prose, on top of KDE's Sonnet (Hunspell, Aspell ...).
 *
 * Only words a reader sees are checked: code, math, link targets, URLs, HTML tags
 * and identifier-like words (camelCase, ALLCAPS, snake_case, words with digits) are skipped.
 */
class SpellChecker
{
public:
    /// A word in a line: where it starts and how long it is.
    struct Word {
        int start = 0;
        int length = 0;
        bool operator==(const Word &other) const
        {
            return start == other.start && length == other.length;
        }
    };

    SpellChecker();
    ~SpellChecker();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    /// A dictionary code like "en_US"; empty means Sonnet's (the system's) default.
    QString language() const;
    void setLanguage(const QString &language);
    /// Installed dictionaries: display name -> code.
    static QMap<QString, QString> availableLanguages();
    /// False when no dictionary is installed for the language.
    bool isAvailable() const;

    bool isMisspelled(const QString &word) const;
    QStringList suggestions(const QString &word) const;
    /// Remember the word in the personal dictionary (kept across sessions).
    void addToDictionary(const QString &word);
    /// Accept the word until Kalamus quits.
    void ignore(const QString &word);

    /// The words to check in @p text, from @p from on.
    static QList<Word> wordsToCheck(const QString &text, int from = 0);

private:
    std::unique_ptr<Sonnet::Speller> m_speller;
    bool m_enabled = true;
    mutable QHash<QString, bool> m_cache;
};
