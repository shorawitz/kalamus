// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTreeView>

class QFileSystemModel;

/// Sidebar listing the Markdown files under a folder.
class FileTree : public QTreeView
{
    Q_OBJECT

public:
    explicit FileTree(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    QString rootPath() const;
    void selectFile(const QString &path);

Q_SIGNALS:
    void fileActivated(const QString &path);

private:
    QFileSystemModel *m_model;
};
