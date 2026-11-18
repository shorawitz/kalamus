// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "editor/markdowneditor.h"
#include "exporter.h"
#include "filetree.h"

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KStandardAction>
#include <KStandardGuiItem>

using namespace Qt::StringLiterals;

namespace
{
const auto kMarkdownFilter = u"Markdown (*.md *.markdown *.mdown *.mkd);;Text (*.txt);;All files (*)"_s;

KConfigGroup sessionGroup()
{
    return KSharedConfig::openConfig()->group(u"Session"_s);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_editor(new MarkdownEditor(this))
    , m_tree(new FileTree(this))
    , m_treeDock(new QDockWidget(i18n("Files"), this))
{
    setCentralWidget(m_editor);

    m_treeDock->setObjectName(u"filesDock"_s);
    m_treeDock->setWidget(m_tree);
    m_treeDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_treeDock);
    resizeDocks({m_treeDock}, {260}, Qt::Horizontal);

    connect(m_tree, &FileTree::fileActivated, this, &MainWindow::openPath);
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateCaption);

    setupActions();
    resize(1100, 760); // first-run size; setupGUI() restores the saved one
    setupGUI(Default, u"kalamusui.rc"_s);

    setFolder(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    updateCaption();
    m_editor->setFocus();
}

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    KStandardAction::openNew(this, &MainWindow::newFile, ac);
    KStandardAction::open(this, &MainWindow::openFile, ac);
    KStandardAction::save(this, &MainWindow::save, ac);
    KStandardAction::saveAs(this, &MainWindow::saveAs, ac);
    KStandardAction::quit(this, &QWidget::close, ac);

    QAction *undo = KStandardAction::undo(m_editor, &QTextEdit::undo, ac);
    QAction *redo = KStandardAction::redo(m_editor, &QTextEdit::redo, ac);
    QAction *cut = KStandardAction::cut(m_editor, &QTextEdit::cut, ac);
    QAction *copy = KStandardAction::copy(m_editor, &QTextEdit::copy, ac);
    KStandardAction::paste(m_editor, &QTextEdit::paste, ac);
    KStandardAction::selectAll(m_editor, &QTextEdit::selectAll, ac);
    undo->setEnabled(false);
    redo->setEnabled(false);
    cut->setEnabled(false);
    copy->setEnabled(false);
    connect(m_editor, &QTextEdit::undoAvailable, undo, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::redoAvailable, redo, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, cut, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, copy, &QAction::setEnabled);

    auto add = [&](const QString &name, const QString &text, const QString &icon, const QKeySequence &shortcut) {
        QAction *action = ac->addAction(name);
        action->setText(text);
        if (!icon.isEmpty()) {
            action->setIcon(QIcon::fromTheme(icon));
        }
        if (!shortcut.isEmpty()) {
            KActionCollection::setDefaultShortcut(action, shortcut);
        }
        return action;
    };

    connect(add(u"file_open_folder"_s, i18n("Open &Folder…"), u"folder-open"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)),
            &QAction::triggered, this, &MainWindow::openFolder);

    connect(add(u"file_export_html"_s, i18n("&HTML…"), u"text-html"_s, {}), &QAction::triggered, this, [this] {
        exportTo(u"html"_s);
    });
    connect(add(u"file_export_pdf"_s, i18n("&PDF…"), u"application-pdf"_s, QKeySequence(Qt::CTRL | Qt::Key_E)), &QAction::triggered, this, [this] {
        exportTo(u"pdf"_s);
    });
    QAction *docx = add(u"file_export_docx"_s, i18n("&Word Document (.docx)…"), u"x-office-document"_s, {});
    QAction *odt = add(u"file_export_odt"_s, i18n("&OpenDocument Text (.odt)…"), u"application-vnd.oasis.opendocument.text"_s, {});
    connect(docx, &QAction::triggered, this, [this] {
        exportTo(u"docx"_s);
    });
    connect(odt, &QAction::triggered, this, [this] {
        exportTo(u"odt"_s);
    });
    if (!Exporter::pandocAvailable()) {
        for (QAction *a : {docx, odt}) {
            a->setEnabled(false);
            a->setToolTip(i18n("Needs pandoc: sudo apt install pandoc"));
        }
    }

    connect(add(u"format_bold"_s, i18n("&Bold"), u"format-text-bold"_s, QKeySequence(Qt::CTRL | Qt::Key_B)),
            &QAction::triggered, m_editor, &MarkdownEditor::toggleBold);
    connect(add(u"format_italic"_s, i18n("&Italic"), u"format-text-italic"_s, QKeySequence(Qt::CTRL | Qt::Key_I)),
            &QAction::triggered, m_editor, &MarkdownEditor::toggleItalic);
    connect(add(u"format_strikethrough"_s, i18n("&Strikethrough"), u"format-text-strikethrough"_s, QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_5)),
            &QAction::triggered, m_editor, &MarkdownEditor::toggleStrikethrough);
    connect(add(u"format_code"_s, i18n("Inline &Code"), u"format-text-code"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft)),
            &QAction::triggered, m_editor, &MarkdownEditor::toggleCode);
    connect(add(u"format_link"_s, i18n("&Link"), u"insert-link"_s, QKeySequence(Qt::CTRL | Qt::Key_K)),
            &QAction::triggered, m_editor, &MarkdownEditor::insertLink);

    connect(add(u"format_paragraph"_s, i18n("&Paragraph"), u"format-text-direction-ltr"_s, QKeySequence(Qt::CTRL | Qt::Key_0)),
            &QAction::triggered, m_editor, [this] {
                m_editor->setHeading(0);
            });
    static constexpr Qt::Key digits[] = {Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6};
    for (int level = 1; level <= 6; ++level) {
        QAction *heading = add(u"format_heading_%1"_s.arg(level), i18n("Heading &%1", level), QString(), QKeySequence(Qt::CTRL | digits[level - 1]));
        connect(heading, &QAction::triggered, m_editor, [this, level] {
            m_editor->setHeading(level);
        });
    }

    // Tables
    connect(add(u"table_insert"_s, i18n("Insert &Table"), u"insert-table"_s, QKeySequence(Qt::CTRL | Qt::Key_T)), &QAction::triggered, m_editor, [this] {
        m_editor->insertTable();
    });
    QList<QAction *> tableActions;
    auto tableAction = [&](const QString &name, const QString &text, const QString &icon, auto slot) {
        QAction *action = add(name, text, icon, {});
        connect(action, &QAction::triggered, m_editor, slot);
        action->setEnabled(false);
        tableActions.append(action);
        return action;
    };
    tableAction(u"table_row_below"_s, i18n("Insert &Row Below"), u"edit-table-insert-row-below"_s, [this] {
        m_editor->insertTableRowBelow();
    });
    tableAction(u"table_column_after"_s, i18n("Insert &Column After"), u"edit-table-insert-column-right"_s, [this] {
        m_editor->insertTableColumnAfter();
    });
    tableAction(u"table_delete_row"_s, i18n("&Delete Row"), u"edit-table-delete-row"_s, [this] {
        m_editor->deleteTableRow();
    });
    tableAction(u"table_delete_column"_s, i18n("D&elete Column"), u"edit-table-delete-column"_s, [this] {
        m_editor->deleteTableColumn();
    });
    tableAction(u"table_align_left"_s, i18n("Align Column &Left"), u"format-justify-left"_s, [this] {
        m_editor->setTableColumnAlignment(MarkdownTable::Align::Left);
    });
    tableAction(u"table_align_center"_s, i18n("Align Column C&enter"), u"format-justify-center"_s, [this] {
        m_editor->setTableColumnAlignment(MarkdownTable::Align::Center);
    });
    tableAction(u"table_align_right"_s, i18n("Align Column &Right"), u"format-justify-right"_s, [this] {
        m_editor->setTableColumnAlignment(MarkdownTable::Align::Right);
    });
    tableAction(u"table_format"_s, i18n("&Format Table"), u"format-indent-more"_s, [this] {
        m_editor->formatTable();
    });
    m_editor->setTableActions(tableActions);
    connect(m_editor, &MarkdownEditor::tableContextChanged, this, [tableActions](bool inTable) {
        for (QAction *action : tableActions) {
            action->setEnabled(inTable);
        }
    });

    QAction *toggleFiles = m_treeDock->toggleViewAction();
    toggleFiles->setText(i18n("Show &Files Panel"));
    toggleFiles->setIcon(QIcon::fromTheme(u"sidebar-show-symbolic"_s));
    ac->addAction(u"view_toggle_files"_s, toggleFiles);
    KActionCollection::setDefaultShortcut(toggleFiles, QKeySequence(Qt::Key_F9));
}

void MainWindow::restoreSession()
{
    const KConfigGroup group = sessionGroup();
    const QString folder = group.readEntry("LastFolder", QString());
    const QString file = group.readEntry("LastFile", QString());
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
        setFolder(folder);
    }
    if (!file.isEmpty() && QFileInfo(file).isFile()) {
        loadFile(file);
    }
}

void MainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        setFolder(info.absoluteFilePath());
        return;
    }
    if (info.absoluteFilePath() == m_path || !maybeSave()) {
        return;
    }
    if (loadFile(info.absoluteFilePath()) && !info.absoluteFilePath().startsWith(m_tree->rootPath() + u'/')) {
        setFolder(info.absolutePath());
    }
}

void MainWindow::newFile()
{
    if (!maybeSave()) {
        return;
    }
    m_path.clear();
    m_editor->loadMarkdown(QString());
    updateCaption();
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, i18n("Open Markdown File"), baseDir(), kMarkdownFilter);
    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::openFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, i18n("Open Folder"), m_tree->rootPath());
    if (!path.isEmpty()) {
        setFolder(path);
        m_treeDock->show();
    }
}

void MainWindow::setFolder(const QString &path)
{
    m_tree->setRootPath(path);
    m_treeDock->setWindowTitle(QFileInfo(path).fileName());
    if (!m_path.isEmpty()) {
        m_tree->selectFile(m_path);
    }
    KConfigGroup group = sessionGroup();
    group.writeEntry("LastFolder", path);
}

bool MainWindow::loadFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        KMessageBox::error(this, i18n("Could not open %1:\n%2", path, file.errorString()));
        return false;
    }
    m_editor->loadMarkdown(QString::fromUtf8(file.readAll()));
    m_path = path;
    m_tree->selectFile(path);
    updateCaption();

    KConfigGroup group = sessionGroup();
    group.writeEntry("LastFile", path);
    return true;
}

bool MainWindow::writeFile(const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        KMessageBox::error(this, i18n("Could not save %1:\n%2", path, file.errorString()));
        return false;
    }
    file.write(m_editor->markdown().toUtf8());
    if (!file.commit()) {
        KMessageBox::error(this, i18n("Could not save %1:\n%2", path, file.errorString()));
        return false;
    }
    m_path = path;
    m_editor->document()->setModified(false);
    m_tree->selectFile(path);
    updateCaption();

    KConfigGroup group = sessionGroup();
    group.writeEntry("LastFile", path);
    return true;
}

bool MainWindow::save()
{
    return m_path.isEmpty() ? saveAs() : writeFile(m_path);
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, i18n("Save As"), m_path.isEmpty() ? baseDir() : m_path, kMarkdownFilter);
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += u".md"_s;
    }
    return writeFile(path);
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified()) {
        return true;
    }
    const QString name = m_path.isEmpty() ? i18n("Untitled") : QFileInfo(m_path).fileName();
    const auto answer = KMessageBox::warningTwoActionsCancel(this,
                                                             i18n("“%1” has unsaved changes. Save them?", name),
                                                             i18n("Unsaved Changes"),
                                                             KStandardGuiItem::save(),
                                                             KStandardGuiItem::discard());
    switch (answer) {
    case KMessageBox::PrimaryAction:
        return save();
    case KMessageBox::SecondaryAction:
        return true;
    default:
        return false;
    }
}

bool MainWindow::queryClose()
{
    return maybeSave();
}

void MainWindow::exportTo(const QString &format)
{
    const QString stem = m_path.isEmpty() ? i18n("Untitled") : QFileInfo(m_path).completeBaseName();
    const QString suggested = baseDir() + u'/' + stem + u'.' + format;
    const QString filter = format.toUpper() + u" (*."_s + format + u')';
    const QString target = QFileDialog::getSaveFileName(this, i18n("Export"), suggested, filter);
    if (target.isEmpty()) {
        return;
    }

    QString error;
    bool ok = false;
    const QString markdown = m_editor->markdown();
    if (format == u"html"_s) {
        ok = Exporter::exportHtml(markdown, stem, target, &error);
    } else if (format == u"pdf"_s) {
        ok = Exporter::exportPdf(markdown, baseDir(), target, &error);
    } else {
        ok = Exporter::exportWithPandoc(markdown, format, baseDir(), target, &error);
    }
    if (!ok) {
        KMessageBox::error(this, i18n("Export failed:\n%1", error));
    }
}

void MainWindow::updateCaption()
{
    const QString name = m_path.isEmpty() ? i18n("Untitled") : QFileInfo(m_path).fileName();
    setCaption(name, m_editor->document()->isModified());
}

QString MainWindow::baseDir() const
{
    if (!m_path.isEmpty()) {
        return QFileInfo(m_path).absolutePath();
    }
    return m_tree->rootPath();
}
