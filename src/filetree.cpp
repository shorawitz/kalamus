// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filetree.h"

#include <QFileSystemModel>

using namespace Qt::StringLiterals;

FileTree::FileTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QFileSystemModel(this))
{
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilters({u"*.md"_s, u"*.markdown"_s, u"*.mdown"_s, u"*.mkd"_s, u"*.txt"_s});
    m_model->setNameFilterDisables(false); // hide non-matching files instead of greying them out
    m_model->setReadOnly(true);

    setModel(m_model);
    for (int column = 1; column < m_model->columnCount(); ++column) {
        hideColumn(column);
    }
    setHeaderHidden(true);
    setUniformRowHeights(true);
    setAnimated(true);
    setFrameShape(QFrame::NoFrame);

    // activated() honours the Plasma single-/double-click setting.
    connect(this, &QTreeView::activated, this, [this](const QModelIndex &index) {
        if (!m_model->isDir(index)) {
            Q_EMIT fileActivated(m_model->filePath(index));
        }
    });
}

void FileTree::setRootPath(const QString &path)
{
    setRootIndex(m_model->setRootPath(path));
}

QString FileTree::rootPath() const
{
    return m_model->rootPath();
}

void FileTree::selectFile(const QString &path)
{
    const QModelIndex index = m_model->index(path);
    if (index.isValid()) {
        setCurrentIndex(index);
        scrollTo(index);
    }
}
