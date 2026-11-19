// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "findbar.h"
#include "markdowneditor.h"

#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
constexpr int MaxMatches = 10000; // beyond this, counting stops (and says so)

QColor mix(const QColor &a, const QColor &b, qreal amount)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * amount,
                            a.greenF() + (b.greenF() - a.greenF()) * amount,
                            a.blueF() + (b.blueF() - a.blueF()) * amount);
}
} // namespace

FindBar::FindBar(MarkdownEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
    , m_find(new QLineEdit(this))
    , m_replace(new QLineEdit(this))
    , m_matchCase(new QToolButton(this))
    , m_wholeWords(new QToolButton(this))
    , m_regex(new QToolButton(this))
    , m_replaceRow(new QWidget(this))
    , m_status(new QLabel(this))
    , m_refreshTimer(new QTimer(this))
{
    auto button = [this](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setIcon(QIcon::fromTheme(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    auto option = [](QToolButton *b, const QString &text, const QString &tip) {
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(true);
    };

    QToolButton *close = button(u"dialog-close"_s, i18n("Close (Esc)"));
    QToolButton *previous = button(u"go-up"_s, i18n("Previous match (Shift+F3)"));
    QToolButton *next = button(u"go-down"_s, i18n("Next match (F3)"));
    option(m_matchCase, i18n("Match Case"), i18n("Tell uppercase and lowercase apart"));
    option(m_wholeWords, i18n("Whole Words"), i18n("Only match whole words"));
    option(m_regex, i18n("Regex"), i18n("Search with a regular expression; use \\1 or $1 in the replacement"));
    m_find->setPlaceholderText(i18n("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace->setPlaceholderText(i18n("Replace with"));
    m_replace->setClearButtonEnabled(true);
    m_status->setMinimumWidth(m_status->fontMetrics().horizontalAdvance(u"0000 of 0000"_s));

    auto *replaceOne = new QToolButton(this);
    replaceOne->setText(i18n("Replace"));
    replaceOne->setAutoRaise(true);
    auto *replaceEvery = new QToolButton(this);
    replaceEvery->setText(i18n("Replace All"));
    replaceEvery->setAutoRaise(true);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    grid->addWidget(close, 0, 0);
    grid->addWidget(m_find, 0, 1);
    grid->addWidget(previous, 0, 2);
    grid->addWidget(next, 0, 3);
    grid->addWidget(m_matchCase, 0, 4);
    grid->addWidget(m_wholeWords, 0, 5);
    grid->addWidget(m_regex, 0, 6);
    grid->addWidget(m_status, 0, 7);
    grid->setColumnStretch(1, 1);

    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->addWidget(m_replace, 1);
    replaceLayout->addWidget(replaceOne);
    replaceLayout->addWidget(replaceEvery);
    grid->addWidget(m_replaceRow, 1, 1, 1, 6);

    connect(close, &QToolButton::clicked, this, &FindBar::closeBar);
    connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(replaceOne, &QToolButton::clicked, this, &FindBar::replace);
    connect(replaceEvery, &QToolButton::clicked, this, &FindBar::replaceAll);
    connect(m_find, &QLineEdit::textEdited, this, &FindBar::searchFromAnchor);
    for (QToolButton *b : {m_matchCase, m_wholeWords, m_regex}) {
        connect(b, &QToolButton::toggled, this, &FindBar::searchFromAnchor);
    }
    m_find->installEventFilter(this);
    m_replace->installEventFilter(this);

    // Keep the matches current while the document is edited.
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(150);
    connect(m_refreshTimer, &QTimer::timeout, this, &FindBar::refreshMatches);
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (isVisible()) {
            m_refreshTimer->start();
        }
    });
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this] {
        if (isVisible()) {
            updateStatus();
        }
    });

    hide();
}

QRegularExpression FindBar::pattern(const QString &text, const Options &options)
{
    if (text.isEmpty()) {
        return QRegularExpression(u"["_s); // invalid on purpose: nothing to search for
    }
    QString source = options.regex ? text : QRegularExpression::escape(text);
    if (options.wholeWords) {
        source = u"(?<!\\w)(?:"_s + source + u")(?!\\w)"_s;
    }
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.matchCase) {
        flags |= QRegularExpression::CaseInsensitiveOption;
    }
    return QRegularExpression(source, flags);
}

QString FindBar::expand(const QString &replacement, const QRegularExpressionMatch &match)
{
    QString out;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement.at(i);
        const QChar n = i + 1 < replacement.size() ? replacement.at(i + 1) : QChar();
        if ((c == u'\\' || c == u'$') && n.isDigit()) {
            out += match.captured(n.digitValue());
            ++i;
        } else if (c == u'\\' && n == u'n') {
            out += u'\n';
            ++i;
        } else if (c == u'\\' && n == u't') {
            out += u'\t';
            ++i;
        } else if (c == u'\\' && (n == u'\\' || n == u'$')) {
            out += n;
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

void FindBar::setOptions(const Options &options)
{
    const QSignalBlocker b1(m_matchCase), b2(m_wholeWords), b3(m_regex);
    m_matchCase->setChecked(options.matchCase);
    m_wholeWords->setChecked(options.wholeWords);
    m_regex->setChecked(options.regex);
    refreshMatches();
}

FindBar::Options FindBar::options() const
{
    return {m_matchCase->isChecked(), m_wholeWords->isChecked(), m_regex->isChecked()};
}

void FindBar::setFindText(const QString &text)
{
    m_find->setText(text);
    searchFromAnchor();
}

void FindBar::setReplaceText(const QString &text)
{
    m_replace->setText(text);
}

int FindBar::matchCount() const
{
    return m_matches.size();
}

int FindBar::currentMatch() const
{
    return indexOfSelection() + 1;
}

QString FindBar::status() const
{
    return m_status->text();
}

void FindBar::openFind()
{
    open(false);
}

void FindBar::openReplace()
{
    open(true);
}

void FindBar::open(bool withReplace)
{
    const QTextCursor cursor = m_editor->textCursor();
    m_anchor = cursor.selectionStart();
    if (cursor.hasSelection() && !cursor.selectedText().contains(QChar::ParagraphSeparator)) {
        m_find->setText(cursor.selectedText());
    }
    m_replaceRow->setVisible(withReplace);
    show();
    m_find->setFocus();
    m_find->selectAll();
    refreshMatches();
}

void FindBar::closeBar()
{
    hide();
    m_matches.clear();
    m_editor->setExtraSelections({});
    m_editor->setFocus();
}

void FindBar::searchFromAnchor()
{
    refreshMatches();
    if (m_matches.isEmpty()) {
        // Nothing found: leave the cursor where the search started.
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(m_anchor);
        m_editor->setTextCursor(cursor);
        updateStatus();
        return;
    }
    for (const QTextCursor &match : std::as_const(m_matches)) {
        if (match.selectionStart() >= m_anchor) {
            select(match);
            return;
        }
    }
    select(m_matches.first());
    updateStatus(i18n("(wrapped)"));
}

void FindBar::refreshMatches()
{
    m_matches.clear();
    const QRegularExpression re = pattern(m_find->text(), options());
    m_invalid = !m_find->text().isEmpty() && !re.isValid();
    if (re.isValid()) {
        QTextDocument *doc = m_editor->document();
        for (QTextBlock block = doc->firstBlock(); block.isValid() && m_matches.size() < MaxMatches; block = block.next()) {
            for (auto it = re.globalMatch(block.text()); it.hasNext() && m_matches.size() < MaxMatches;) {
                const auto m = it.next();
                if (m.capturedLength() == 0) {
                    continue; // e.g. "^" or "a*": nothing to select
                }
                QTextCursor match(doc);
                match.setPosition(block.position() + int(m.capturedStart()));
                match.setPosition(block.position() + int(m.capturedEnd()), QTextCursor::KeepAnchor);
                m_matches.append(match);
            }
        }
    }
    updateHighlights();
    updateStatus();
}

void FindBar::updateHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QColor base = palette().color(QPalette::Base);
    QTextCharFormat format;
    format.setBackground(mix(base, QColor(0xf6, 0xd3, 0x2d), 0.55)); // Breeze yellow
    selections.reserve(m_matches.size());
    for (const QTextCursor &match : std::as_const(m_matches)) {
        selections.append({match, format});
    }
    m_editor->setExtraSelections(selections);
}

void FindBar::updateStatus(const QString &note)
{
    QPalette pal = m_find->palette();
    const bool bad = m_invalid || (!m_find->text().isEmpty() && m_matches.isEmpty());
    pal.setColor(QPalette::Base, bad ? mix(palette().color(QPalette::Base), QColor(0xda, 0x44, 0x53), 0.3) : palette().color(QPalette::Base));
    m_find->setPalette(pal);

    QString text;
    if (m_invalid) {
        text = i18n("Invalid pattern");
    } else if (m_find->text().isEmpty()) {
        text.clear();
    } else if (m_matches.isEmpty()) {
        text = i18n("Not found");
    } else {
        const QString total = m_matches.size() >= MaxMatches ? i18n("%1+", MaxMatches) : QString::number(m_matches.size());
        const int current = indexOfSelection();
        text = current >= 0 ? i18nc("match 3 of 12", "%1 of %2", current + 1, total) : i18np("1 match", "%2 matches", m_matches.size(), total);
    }
    if (!note.isEmpty()) {
        text += u' ' + note;
    }
    m_status->setText(text);
}

int FindBar::indexOfSelection() const
{
    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return -1;
    }
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches.at(i).selectionStart() == cursor.selectionStart() && m_matches.at(i).selectionEnd() == cursor.selectionEnd()) {
            return i;
        }
    }
    return -1;
}

void FindBar::select(const QTextCursor &match)
{
    m_editor->setTextCursor(match);
    m_editor->ensureCursorVisible();
    updateStatus();
}

bool FindBar::findNext()
{
    if (!isVisible()) {
        open(false);
        return !m_matches.isEmpty();
    }
    if (m_matches.isEmpty()) {
        return false;
    }
    const QTextCursor cursor = m_editor->textCursor();
    const int from = cursor.hasSelection() ? cursor.selectionStart() + 1 : cursor.position();
    for (const QTextCursor &match : std::as_const(m_matches)) {
        if (match.selectionStart() >= from) {
            select(match);
            return true;
        }
    }
    select(m_matches.first());
    updateStatus(i18n("(wrapped)"));
    return true;
}

bool FindBar::findPrevious()
{
    if (!isVisible()) {
        open(false);
    }
    if (m_matches.isEmpty()) {
        return false;
    }
    const int before = m_editor->textCursor().selectionStart();
    for (auto it = m_matches.crbegin(); it != m_matches.crend(); ++it) {
        if (it->selectionStart() < before) {
            select(*it);
            return true;
        }
    }
    select(m_matches.last());
    updateStatus(i18n("(wrapped)"));
    return true;
}

bool FindBar::replace()
{
    const int index = indexOfSelection();
    if (index < 0) {
        return findNext(); // first show which match would be replaced
    }
    QTextCursor match = m_editor->textCursor();
    const QString replacement = [&]() -> QString {
        if (!m_regex->isChecked()) {
            return m_replace->text();
        }
        const QTextBlock block = match.block();
        const auto m = pattern(m_find->text(), options())
                           .match(block.text(), match.selectionStart() - block.position(), QRegularExpression::NormalMatch,
                                  QRegularExpression::AnchorAtOffsetMatchOption);
        return expand(m_replace->text(), m);
    }();
    match.insertText(replacement);
    m_anchor = match.position();
    refreshMatches();
    // Continue with the next match after the replacement.
    for (const QTextCursor &next : std::as_const(m_matches)) {
        if (next.selectionStart() >= m_anchor) {
            select(next);
            return true;
        }
    }
    if (!m_matches.isEmpty()) {
        select(m_matches.first());
        updateStatus(i18n("(wrapped)"));
    }
    return true;
}

int FindBar::replaceAll()
{
    refreshMatches();
    if (m_matches.isEmpty()) {
        return 0;
    }
    const QRegularExpression re = pattern(m_find->text(), options());
    const int count = int(m_matches.size());
    QTextCursor edit(m_editor->document());
    edit.beginEditBlock(); // one undo step
    for (auto it = m_matches.rbegin(); it != m_matches.rend(); ++it) {
        QTextCursor match = *it;
        QString replacement = m_replace->text();
        if (m_regex->isChecked()) {
            const QTextBlock block = match.block();
            replacement = expand(replacement,
                                 re.match(block.text(), match.selectionStart() - block.position(), QRegularExpression::NormalMatch,
                                          QRegularExpression::AnchorAtOffsetMatchOption));
        }
        match.insertText(replacement);
    }
    edit.endEditBlock();
    refreshMatches();
    // Nothing left to find is the point here, not an error: no red, no "Not found".
    QPalette pal = m_find->palette();
    pal.setColor(QPalette::Base, palette().color(QPalette::Base));
    m_find->setPalette(pal);
    m_status->setText(i18np("Replaced 1", "Replaced %1", count));
    return count;
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        closeBar();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == m_replace) {
                replace();
            } else if (key->modifiers() & Qt::ShiftModifier) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            closeBar();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
