// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "recentdocuments.h"
#include "editor/codehighlighter.h"
#include "editor/markdowneditor.h"
#include "exporter.h"
#include "filetree.h"
#include "findbar.h"
#include "outlinepanel.h"
#include "kalamussettings.h"
#include "settingsdialog.h"

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KRecentFilesAction>
#include <KSelectAction>
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
    , m_findBar(new FindBar(m_editor))
    , m_outline(new OutlinePanel(this))
    , m_outlineDock(new QDockWidget(i18n("Outline"), this))
    , m_outlineTimer(new QTimer(this))
{
    // The page, with the find bar under it.
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_editor, 1);
    column->addWidget(m_findBar);
    setCentralWidget(central);

    m_treeDock->setObjectName(u"filesDock"_s);
    m_treeDock->setWidget(m_tree);
    m_treeDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_treeDock);
    resizeDocks({m_treeDock}, {260}, Qt::Horizontal);

    // Outline: a tab next to Files, like Typora's sidebar.
    m_outlineDock->setObjectName(u"outlineDock"_s);
    m_outlineDock->setWidget(m_outline);
    m_outlineDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_outlineDock);
    tabifyDockWidget(m_treeDock, m_outlineDock);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    m_treeDock->raise();

    // Re-read the headings shortly after typing stops.
    m_outlineTimer->setSingleShot(true);
    m_outlineTimer->setInterval(250);
    connect(m_outlineTimer, &QTimer::timeout, this, &MainWindow::refreshOutline);
    connect(m_editor->document(), &QTextDocument::contentsChanged, m_outlineTimer, qOverload<>(&QTimer::start));
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this] {
        m_outline->setCurrentBlock(m_editor->textCursor().blockNumber());
    });
    connect(m_outline, &OutlinePanel::headingActivated, m_editor, &MarkdownEditor::goToBlock);

    connect(m_tree, &FileTree::fileActivated, this, &MainWindow::openPath);
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateCaption);

    setupActions();
    resize(1100, 760); // first-run size; setupGUI() restores the saved one
    setupGUI(Default, u"kalamusui.rc"_s);

    // A window layout saved before the Outline panel existed puts it below Files. Make it
    // a tab once; after that, wherever the user moves it is remembered.
    KConfigGroup view = KSharedConfig::openConfig()->group(u"View"_s);
    if (!view.readEntry("OutlinePlaced", false)) {
        tabifyDockWidget(m_treeDock, m_outlineDock);
        m_treeDock->raise();
        view.writeEntry("OutlinePlaced", true);
    }

    setFolder(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    updateCaption();
    m_editor->setFocus();
}

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    KStandardAction::openNew(this, &MainWindow::newFile, ac);
    KStandardAction::open(this, &MainWindow::openFile, ac);
    m_recentFiles = KStandardAction::openRecent(this, &MainWindow::openRecent, ac);
    m_recentFiles->loadEntries(KSharedConfig::openConfig()->group(u"Recent Files"_s));
    connect(m_recentFiles, &KRecentFilesAction::recentListCleared, this, [this] {
        m_recentFiles->saveEntries(KSharedConfig::openConfig()->group(u"Recent Files"_s));
    });
    KStandardAction::save(this, &MainWindow::save, ac);
    KStandardAction::saveAs(this, &MainWindow::saveAs, ac);
    KStandardAction::close(this, &MainWindow::closeFile, ac);
    KStandardAction::quit(this, &QWidget::close, ac);
    KStandardAction::find(m_findBar, &FindBar::openFind, ac);
    KStandardAction::findNext(m_findBar, &FindBar::findNext, ac);
    KStandardAction::findPrev(m_findBar, &FindBar::findPrevious, ac);
    KStandardAction::replace(m_findBar, &FindBar::openReplace, ac);

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
    QAction *codeBlock = add(u"format_code_block"_s, i18n("Code &Block…"), u"code-context"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    connect(codeBlock, &QAction::triggered, this, [this] {
        // Ask for the language (it picks the colours); remember the last choice.
        KConfigGroup group = KSharedConfig::openConfig()->group(u"Editor"_s);
        const QString last = group.readEntry("LastCodeLanguage", u"python"_s);
        QStringList choices = CodeHighlighter::instance().languageChoices();
        choices.insert(0, u"text"_s);
        bool ok = false;
        const QString language = QInputDialog::getItem(this,
                                                       i18n("Code Block"),
                                                       i18n("Language (sets the colors; type to search):"),
                                                       choices,
                                                       qMax(0, int(choices.indexOf(last))),
                                                       true,
                                                       &ok)
                                     .trimmed();
        if (!ok) {
            return;
        }
        group.writeEntry("LastCodeLanguage", language);
        m_editor->insertCodeBlock(language == u"text"_s ? QString() : language);
    });
    connect(add(u"format_code"_s, i18n("Inline &Code"), u"format-text-code"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft)),
            &QAction::triggered, this, [this, codeBlock] {
                // Several lines of code belong in a code block, not in backticks.
                if (m_editor->selectionSpansLines()) {
                    codeBlock->trigger();
                } else {
                    m_editor->toggleCode();
                }
            });
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

    // Images
    connect(add(u"insert_image"_s, i18n("Insert &Image…"), u"insert-image"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)),
            &QAction::triggered, this, [this] {
                QStringList patterns;
                const auto formats = QImageReader::supportedImageFormats();
                for (const QByteArray &format : formats) {
                    patterns.append(u"*."_s + QString::fromLatin1(format));
                }
                const QList<QUrl> files = QFileDialog::getOpenFileUrls(this,
                                                                       i18n("Insert Image"),
                                                                       QUrl::fromLocalFile(baseDir()),
                                                                       i18n("Images (%1)", patterns.join(u' ')));
                if (!files.isEmpty()) {
                    m_editor->insertImageFiles(files);
                }
            });
    m_editor->setSaveRequester([this] {
        const auto answer = KMessageBox::questionTwoActions(this,
                                                            i18n("Images are stored next to the document, in its assets folder. Save the document first?"),
                                                            i18n("Save Document"),
                                                            KStandardGuiItem::save(),
                                                            KStandardGuiItem::cancel());
        return answer == KMessageBox::PrimaryAction && saveAs();
    });

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

    // These menu switches and Settings > Configure Kalamus share KalamusSettings;
    // applySettings() brings the editor and the switches in line with it.
    m_limitWidth = add(u"view_limit_line_width"_s, i18n("&Limit Line Width"), u"format-justify-center"_s, {});
    m_limitWidth->setCheckable(true);
    m_limitWidth->setToolTip(i18n("Keep lines at a comfortable reading width instead of the full window width"));
    connect(m_limitWidth, &QAction::toggled, this, [this](bool on) {
        KalamusSettings::setLimitLineWidth(on);
        KalamusSettings::self()->save();
        applySettings();
    });

    m_checkSpelling = add(u"tools_spelling_auto"_s, i18n("Check &Spelling"), u"tools-check-spelling"_s, QKeySequence(Qt::SHIFT | Qt::Key_F7));
    m_checkSpelling->setCheckable(true);
    m_checkSpelling->setToolTip(i18n("Underline misspelled words as you type"));
    connect(m_checkSpelling, &QAction::toggled, this, [this](bool on) {
        KalamusSettings::setEnabled(on);
        KalamusSettings::self()->save();
        applySettings();
    });

    m_spellingLanguage = new KSelectAction(QIcon::fromTheme(u"globe"_s), i18n("Spelling &Language"), this);
    ac->addAction(u"tools_spelling_language"_s, m_spellingLanguage);
    const auto dictionaries = SpellChecker::availableLanguages(); // name -> code
    for (auto it = dictionaries.cbegin(); it != dictionaries.cend(); ++it) {
        m_spellingLanguage->addAction(it.key())->setData(it.value());
    }
    m_spellingLanguage->setEnabled(!dictionaries.isEmpty());
    connect(m_spellingLanguage, &KSelectAction::actionTriggered, this, [this](QAction *item) {
        KalamusSettings::setSpellingLanguage(item->data().toString());
        KalamusSettings::self()->save();
        applySettings();
    });

    KStandardAction::preferences(this, &MainWindow::showSettings, ac);
    applySettings();

    QAction *toggleOutline = m_outlineDock->toggleViewAction();
    toggleOutline->setText(i18n("Show &Outline Panel"));
    toggleOutline->setIcon(QIcon::fromTheme(u"view-list-tree"_s));
    ac->addAction(u"view_toggle_outline"_s, toggleOutline);
    KActionCollection::setDefaultShortcut(toggleOutline, QKeySequence(Qt::SHIFT | Qt::Key_F9));
    connect(toggleOutline, &QAction::triggered, this, [this](bool on) {
        if (on) {
            m_outlineDock->raise(); // bring its tab to the front
        }
    });

    QAction *toggleFiles = m_treeDock->toggleViewAction();
    toggleFiles->setText(i18n("Show &Files Panel"));
    toggleFiles->setIcon(QIcon::fromTheme(u"sidebar-show-symbolic"_s));
    ac->addAction(u"view_toggle_files"_s, toggleFiles);
    KActionCollection::setDefaultShortcut(toggleFiles, QKeySequence(Qt::Key_F9));
}

void MainWindow::applySettings()
{
    m_editor->setFont(KalamusSettings::textFont());
    m_editor->setCodeFont(KalamusSettings::codeFont());
    m_editor->setReadableWidth(KalamusSettings::readableWidth());
    m_editor->setLimitLineWidth(KalamusSettings::limitLineWidth());
    m_editor->setSpellCheckingLanguage(KalamusSettings::spellingLanguage());
    m_editor->setSpellCheckingEnabled(KalamusSettings::enabled());
    m_editor->setLoadRemoteImages(KalamusSettings::loadRemoteImages());

    // The menu switches show the same state (without feeding back into the settings).
    const QSignalBlocker block1(m_limitWidth), block2(m_checkSpelling), block3(m_spellingLanguage);
    m_limitWidth->setChecked(KalamusSettings::limitLineWidth());
    m_checkSpelling->setChecked(KalamusSettings::enabled());
    const QString language = m_editor->spellCheckingLanguage(); // "" resolves to the system's
    for (QAction *item : m_spellingLanguage->actions()) {
        if (item->data().toString() == language) {
            m_spellingLanguage->setCurrentAction(item);
        }
    }
}

void MainWindow::showSettings()
{
    if (KConfigDialog::showDialog(u"settings"_s)) {
        return; // already open: brought to the front
    }
    auto *dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &KConfigDialog::settingsChanged, this, &MainWindow::applySettings);
    dialog->show();
}

void MainWindow::refreshOutline()
{
    m_outlineTimer->stop();
    m_outline->setHeadings(Outline::headings(m_editor->document()));
    m_outline->setCurrentBlock(m_editor->textCursor().blockNumber());
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
    m_editor->setBaseDirectory(QString());
    m_editor->loadMarkdown(QString());
    updateCaption();
}

void MainWindow::closeFile()
{
    if (!maybeSave()) {
        return;
    }
    m_path.clear();
    m_editor->setBaseDirectory(QString());
    m_editor->loadMarkdown(QString());
    refreshOutline();
    m_tree->setCurrentIndex({});
    updateCaption();

    KConfigGroup group = sessionGroup();
    group.deleteEntry("LastFile"); // closed on purpose: don't reopen it at the next start
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, i18n("Open Markdown File"), baseDir(), kMarkdownFilter);
    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::openRecent(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (!QFileInfo::exists(path)) {
        KMessageBox::information(this,
                                 i18n("“%1” is no longer there. It may have been moved, renamed or deleted, "
                                      "so it has been removed from the recent files.",
                                      path),
                                 i18n("File Not Found"));
        m_recentFiles->removeUrl(url);
        m_recentFiles->saveEntries(KSharedConfig::openConfig()->group(u"Recent Files"_s));
        return;
    }
    openPath(path);
}

void MainWindow::rememberRecent(const QString &path)
{
    RecentDocuments::add(path); // Plasma's Recent Files too
    m_recentFiles->addUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    KConfigGroup group = KSharedConfig::openConfig()->group(u"Recent Files"_s);
    m_recentFiles->saveEntries(group);
    group.sync(); // another Kalamus window, or a crash, still sees it
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
    m_editor->setBaseDirectory(QFileInfo(path).absolutePath()); // before loading: relative images
    m_editor->loadMarkdown(QString::fromUtf8(file.readAll()));
    refreshOutline();
    m_path = path;
    m_tree->selectFile(path);
    updateCaption();

    KConfigGroup group = sessionGroup();
    group.writeEntry("LastFile", path);
    rememberRecent(path);
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
    m_editor->setBaseDirectory(QFileInfo(path).absolutePath());
    m_editor->document()->setModified(false);
    m_tree->selectFile(path);
    updateCaption();

    KConfigGroup group = sessionGroup();
    group.writeEntry("LastFile", path);
    rememberRecent(path);
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
