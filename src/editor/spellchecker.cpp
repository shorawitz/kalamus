// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "spellchecker.h"

#include <QRegularExpression>

#include <Sonnet/Speller>

using namespace Qt::StringLiterals;

namespace
{
// Parts of a line that are not prose. Matched first and skipped.
const QRegularExpression kNotProse(uR"re(
    (`+).+?(?<!`)\1(?!`)                    # code span
  | (?<![\\$])\$\$[^$\n]+?\$\$              # $$display math$$
  | (?<![\\$])\$(?=\S)[^$\n]+?(?<=\S)\$     # $inline math$
  | \]\((?:<[^>\n]*>|[^)\s]*)(?:\s+"[^"]*")?\)   # ](link or image target "title")
  | ^\s*\[[^\]]+\]:\s.*                     # [ref]: definition
  | <[^>\n]*>                               # HTML tag or <autolink>
  | \b(?:https?|ftp|file|mailto):\S+        # bare URL
  | \bwww\.\S+
  | \S+@\S+\.\S+                            # e-mail address
  | [\w.~-]*[/\\][\w.~/\\-]+                 # paths like src/main.cpp, ~/notes
  | &\#?\w+;                                # HTML entity
)re"_s,
                                     QRegularExpression::ExtendedPatternSyntaxOption);

// Letters, with inner apostrophes (don't, l'eau). Not part of a longer identifier.
const QRegularExpression kWord(uR"re((?<![\p{L}\p{M}\p{N}_'’.])[\p{L}\p{M}]+(?:['’][\p{L}\p{M}]+)*(?![\p{L}\p{M}\p{N}_]|\.\w))re"_s);

bool looksLikeAnIdentifier(QStringView word)
{
    // camelCase, iPhone, ALLCAPS (acronyms): uppercase after the first letter.
    for (qsizetype i = 1; i < word.size(); ++i) {
        if (word.at(i).isUpper()) {
            return true;
        }
    }
    return false;
}
} // namespace

SpellChecker::SpellChecker()
    : m_speller(std::make_unique<Sonnet::Speller>())
{
}

SpellChecker::~SpellChecker() = default;

bool SpellChecker::isEnabled() const
{
    return m_enabled;
}

void SpellChecker::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

QString SpellChecker::language() const
{
    return m_speller->language();
}

void SpellChecker::setLanguage(const QString &language)
{
    m_speller->setLanguage(language.isEmpty() ? m_speller->defaultLanguage() : language);
    m_cache.clear();
}

QMap<QString, QString> SpellChecker::availableLanguages()
{
    return Sonnet::Speller().availableDictionaries();
}

bool SpellChecker::isAvailable() const
{
    return m_speller->isValid() && m_speller->availableLanguages().contains(m_speller->language());
}

bool SpellChecker::isMisspelled(const QString &word) const
{
    if (const auto it = m_cache.constFind(word); it != m_cache.constEnd()) {
        return *it;
    }
    // Sonnet knows "don't", not "don’t".
    QString plain = word;
    plain.replace(u'’', u'\'');
    const bool misspelled = m_speller->isMisspelled(plain);
    m_cache.insert(word, misspelled);
    return misspelled;
}

QStringList SpellChecker::suggestions(const QString &word) const
{
    QString plain = word;
    plain.replace(u'’', u'\'');
    return m_speller->suggest(plain);
}

void SpellChecker::addToDictionary(const QString &word)
{
    m_speller->addToPersonal(word);
    m_cache.insert(word, false);
}

void SpellChecker::ignore(const QString &word)
{
    m_speller->addToSession(word);
    m_cache.insert(word, false);
}

QList<SpellChecker::Word> SpellChecker::wordsToCheck(const QString &text, int from)
{
    QList<bool> skip(text.size(), false);
    for (auto it = kNotProse.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        for (qsizetype i = m.capturedStart(); i < m.capturedEnd(); ++i) {
            skip[i] = true;
        }
    }

    QList<Word> words;
    for (auto it = kWord.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        const int start = int(m.capturedStart());
        const int length = int(m.capturedLength());
        if (length < 2 || skip.at(start) || skip.at(start + length - 1) || looksLikeAnIdentifier(m.capturedView())) {
            continue;
        }
        words.append({start, length});
    }
    return words;
}
