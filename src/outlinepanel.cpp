// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outlinepanel.h"

#include <QHeaderView>
#include <QSet>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
constexpr int BlockRole = Qt::UserRole;
}

OutlinePanel::OutlinePanel(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setUniformRowHeights(true);
    setFrameShape(QFrame::NoFrame);
    setIndentation(14);
    setExpandsOnDoubleClick(false);
    setTextElideMode(Qt::ElideRight);
    header()->setStretchLastSection(true);

    auto activate = [this](QTreeWidgetItem *item) {
        if (!m_updating && item && item->data(0, BlockRole).isValid()) {
            Q_EMIT headingActivated(item->data(0, BlockRole).toInt());
        }
    };
    connect(this, &QTreeWidget::itemClicked, this, activate);
    connect(this, &QTreeWidget::itemActivated, this, activate); // Enter key
    setHeadings({});
}

QList<OutlineHeading> OutlinePanel::headings() const
{
    return m_headings;
}

void OutlinePanel::setHeadings(const QList<OutlineHeading> &headings)
{
    if (headings == m_headings && (topLevelItemCount() > 0)) {
        return;
    }
    // Keep folds of sections that still exist (matched by title).
    QSet<QString> collapsed;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->childCount() > 0 && !m_items.at(i)->isExpanded()) {
            collapsed.insert(m_headings.at(i).title);
        }
    }

    m_updating = true;
    clear();
    m_items.clear();
    m_headings = headings;
    if (headings.isEmpty()) {
        auto *placeholder = new QTreeWidgetItem(this, {i18n("No headings")});
        placeholder->setFlags(Qt::NoItemFlags);
        QFont italic = font();
        italic.setItalic(true);
        placeholder->setFont(0, italic);
        m_updating = false;
        return;
    }

    // A heading nests under the nearest heading above it with a smaller level.
    QList<std::pair<int, QTreeWidgetItem *>> parents; // (level, item)
    for (const OutlineHeading &heading : headings) {
        while (!parents.isEmpty() && parents.last().first >= heading.level) {
            parents.removeLast();
        }
        auto *item = parents.isEmpty() ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parents.last().second);
        item->setText(0, heading.title);
        item->setToolTip(0, heading.title);
        item->setData(0, BlockRole, heading.blockNumber);
        if (heading.level == 1) {
            QFont bold = font();
            bold.setBold(true);
            item->setFont(0, bold);
        }
        parents.append({heading.level, item});
        m_items.append(item);
    }
    expandAll();
    for (int i = 0; i < m_items.size(); ++i) {
        if (collapsed.contains(m_headings.at(i).title)) {
            m_items.at(i)->setExpanded(false);
        }
    }
    m_updating = false;
}

void OutlinePanel::setCurrentBlock(int blockNumber)
{
    const int section = Outline::sectionAt(m_headings, blockNumber);
    m_updating = true;
    if (section < 0) {
        clearSelection();
        setCurrentItem(nullptr);
    } else {
        QTreeWidgetItem *item = m_items.at(section);
        // Inside a folded section, mark the visible ancestor.
        for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) {
            if (!p->isExpanded()) {
                item = p;
            }
        }
        setCurrentItem(item);
        scrollToItem(item);
    }
    m_updating = false;
}
