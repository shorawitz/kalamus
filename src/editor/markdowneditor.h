// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTextEdit>

class MarkdownHighlighter;

/**
 * The writing surface. Holds the Markdown source as plain text and lets
 * MarkdownHighlighter make it look rendered. Adds the editing helpers people
 * expect from a word processor: Ctrl+B/I/K, headings, list continuation.
 */
class MarkdownEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    /// Replace the whole document (clears undo history and the modified flag).
    void loadMarkdown(const QString &markdown);
    QString markdown() const;

public Q_SLOTS:
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleCode();
    void insertLink();
    /// 0 = paragraph, 1..6 = heading level. Applying the current level again turns it off.
    void setHeading(int level);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void toggleWrap(const QString &marker);
    bool continueBlock();
    bool indentListItem(bool outdent);
    void applyLineSpacing();
    void updateMargins();

    MarkdownHighlighter *m_highlighter = nullptr;
};
