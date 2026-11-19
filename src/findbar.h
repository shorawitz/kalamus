// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRegularExpression>
#include <QTextCursor>
#include <QWidget>

class MarkdownEditor;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

/**
 * Find and replace, as a bar under the editor (like Kate's), not a dialog.
 *
 * Searches the Markdown source as you type, highlights every match and shows
 * "3 of 12". Replace All is one undo step.
 */
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(MarkdownEditor *editor, QWidget *parent = nullptr);

    struct Options {
        bool matchCase = false;
        bool wholeWords = false;
        bool regex = false;
    };
    /// The search as a regular expression; invalid when @p text is empty or a bad regex.
    static QRegularExpression pattern(const QString &text, const Options &options);
    /// @p replacement with \1 / $1 (and \n, \t in regex mode) filled in from @p match.
    static QString expand(const QString &replacement, const QRegularExpressionMatch &match);

    void setOptions(const Options &options);
    Options options() const;
    void setFindText(const QString &text);
    void setReplaceText(const QString &text);

    int matchCount() const;
    /// 1-based index of the selected match, 0 when the selection is not a match.
    int currentMatch() const;
    QString status() const;

public Q_SLOTS:
    /// Show the bar with the find field focused (pre-filled from a one-line selection).
    void openFind();
    /// Same, with the replace row.
    void openReplace();
    void closeBar();
    bool findNext();
    bool findPrevious();
    /// Replace the selected match, then select the next one.
    bool replace();
    /// Replace every match; returns how many.
    int replaceAll();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void open(bool withReplace);
    void searchFromAnchor(); // as you type
    void refreshMatches(); // after edits or option changes, without moving
    void updateHighlights();
    void updateStatus(const QString &note = {});
    void select(const QTextCursor &match);
    int indexOfSelection() const;

    MarkdownEditor *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QToolButton *m_matchCase;
    QToolButton *m_wholeWords;
    QToolButton *m_regex;
    QWidget *m_replaceRow;
    QLabel *m_status;
    QTimer *m_refreshTimer;
    QList<QTextCursor> m_matches;
    int m_anchor = 0; // where the search started
    bool m_invalid = false;
};
