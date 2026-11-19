// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "outline.h"

#include <QTreeWidget>

/// Sidebar listing the document's headings as a tree; click one to go there.
class OutlinePanel : public QTreeWidget
{
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);

    /// Rebuild the tree (only when the headings really changed, so folds survive typing).
    void setHeadings(const QList<OutlineHeading> &headings);
    QList<OutlineHeading> headings() const;
    /// Highlight the section that contains @p blockNumber (where the cursor is).
    void setCurrentBlock(int blockNumber);

Q_SIGNALS:
    /// The user picked a heading; @p blockNumber is its line.
    void headingActivated(int blockNumber);

private:
    QList<OutlineHeading> m_headings;
    QList<QTreeWidgetItem *> m_items; // parallel to m_headings
    bool m_updating = false;
};
