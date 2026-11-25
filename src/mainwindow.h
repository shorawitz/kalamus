// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KXmlGuiWindow>

class FileTree;
class KRecentFilesAction;
class KSelectAction;
class QAction;
class FindBar;
class OutlinePanel;
class QTimer;
class MarkdownEditor;
class QDockWidget;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Opens a Markdown file, or shows a folder in the file tree.
    void openPath(const QString &path);
    /// Reopens the folder and file from the last session.
    void restoreSession();

public Q_SLOTS:
    /// Bring the editor and the menu switches in line with KalamusSettings.
    void applySettings();

protected:
    bool queryClose() override;

private:
    void setupActions();
    void newFile();
    void openFile();
    /// Close the document (asking about unsaved changes), leaving an empty one.
    void closeFile();
    void openFolder();
    bool save();
    bool saveAs();
    bool maybeSave();
    bool loadFile(const QString &path);
    bool writeFile(const QString &path);
    void setFolder(const QString &path);
    void exportTo(const QString &format);
    void updateCaption();
    QString baseDir() const;
    void openRecent(const QUrl &url);
    /// Put @p path at the top of File > Open Recent.
    void rememberRecent(const QString &path);
    void refreshOutline();
    void showSettings();

    MarkdownEditor *m_editor;
    FileTree *m_tree;
    QDockWidget *m_treeDock;
    FindBar *m_findBar;
    OutlinePanel *m_outline;
    QDockWidget *m_outlineDock;
    QTimer *m_outlineTimer;
    QString m_path;
    KRecentFilesAction *m_recentFiles = nullptr;
    QAction *m_limitWidth = nullptr;
    QAction *m_checkSpelling = nullptr;
    KSelectAction *m_spellingLanguage = nullptr;
};
