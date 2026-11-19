// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsdialog.h"
#include "kalamussettings.h"
#include "spellchecker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

#include <KColorSchemeManager>
#include <KColorSchemeModel>
#include <KFontRequester>
#include <KLocalizedString>

using namespace Qt::StringLiterals;

SettingsDialog::SettingsDialog(QWidget *parent)
    : KConfigDialog(parent, u"settings"_s, KalamusSettings::self())
{
    setFaceType(KPageDialog::List);
    addPage(appearancePage(), i18n("Appearance"), u"preferences-desktop-theme"_s, i18n("Colors, fonts and line width"));
    addPage(editorPage(), i18n("Editor"), u"document-edit"_s, i18n("Spelling and images"));
    updateWidgets();
}

QWidget *SettingsDialog::appearancePage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    // Color scheme: the same list as Plasma's System Settings, "Default" follows Plasma.
    m_scheme = new QComboBox(page);
    m_scheme->setModel(KColorSchemeManager::instance()->model());
    m_scheme->setToolTip(i18n("“Default” follows the color scheme chosen in System Settings"));
    connect(m_scheme, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtons);
    form->addRow(i18n("Color scheme:"), m_scheme);

    auto *textFont = new KFontRequester(page);
    textFont->setObjectName(u"kcfg_TextFont"_s);
    form->addRow(i18n("Text font:"), textFont);

    auto *codeFont = new QFontComboBox(page);
    codeFont->setObjectName(u"kcfg_CodeFont"_s);
    codeFont->setFontFilters(QFontComboBox::MonospacedFonts);
    codeFont->setEditable(false);
    // Tell the settings manager to use the font, not the combo box text.
    codeFont->setProperty("kcfg_property", QByteArray("currentFont"));
    codeFont->setProperty("kcfg_propertyNotify", QByteArray(SIGNAL(currentFontChanged(QFont))));
    codeFont->setToolTip(i18n("Used for code, formulas being edited and table source; its size follows the text font"));
    form->addRow(i18n("Code font:"), codeFont);

    auto *limit = new QCheckBox(i18n("Keep lines at a comfortable reading width"), page);
    limit->setObjectName(u"kcfg_LimitLineWidth"_s);
    form->addRow(i18n("Line width:"), limit);

    auto *width = new QSpinBox(page);
    width->setObjectName(u"kcfg_ReadableWidth"_s);
    width->setSuffix(i18n(" em"));
    width->setToolTip(i18n("About this many letters “m” fit on a line; 66 is roughly 860 pixels at the default font"));
    width->setEnabled(limit->isChecked());
    connect(limit, &QCheckBox::toggled, width, &QWidget::setEnabled);
    form->addRow(i18n("Reading width:"), width);
    return page;
}

QWidget *SettingsDialog::editorPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    auto *spelling = new QCheckBox(i18n("Check spelling as you type"), page);
    spelling->setObjectName(u"kcfg_Enabled"_s);
    form->addRow(i18n("Spelling:"), spelling);

    m_language = new QComboBox(page);
    m_language->addItem(i18n("System default"), QString());
    const auto dictionaries = SpellChecker::availableLanguages(); // name -> code
    for (auto it = dictionaries.cbegin(); it != dictionaries.cend(); ++it) {
        m_language->addItem(it.key(), it.value());
    }
    m_language->setToolTip(i18n("Install more dictionaries with the hunspell-* packages"));
    connect(m_language, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtons);
    form->addRow(i18n("Dictionary:"), m_language);

    auto *remote = new QCheckBox(i18n("Load images from the web"), page);
    remote->setObjectName(u"kcfg_LoadRemoteImages"_s);
    form->addRow(i18n("Images:"), remote);
    auto *note = new QLabel(i18n("Pictures linked from websites are downloaded when a document is opened. "
                                 "Turn this off to work offline or keep your reading private."),
                            page);
    note->setWordWrap(true);
    note->setEnabled(false); // dimmed, as a hint
    form->addRow(QString(), note);
    return page;
}

QString SettingsDialog::selectedSchemeId() const
{
    return m_scheme->currentIndex() <= 0 ? QString() : m_scheme->currentData(KColorSchemeModel::IdRole).toString();
}

QString SettingsDialog::selectedLanguage() const
{
    return m_language->currentData().toString();
}

bool SettingsDialog::hasChanged()
{
    return KConfigDialog::hasChanged() || selectedSchemeId() != KColorSchemeManager::instance()->activeSchemeId()
        || selectedLanguage() != KalamusSettings::spellingLanguage();
}

bool SettingsDialog::isDefault()
{
    return KConfigDialog::isDefault() && m_scheme->currentIndex() <= 0 && selectedLanguage().isEmpty();
}

void SettingsDialog::updateSettings()
{
    KConfigDialog::updateSettings();
    KalamusSettings::setSpellingLanguage(selectedLanguage());
    KalamusSettings::self()->save();
    if (selectedSchemeId() != KColorSchemeManager::instance()->activeSchemeId()) {
        KColorSchemeManager::instance()->activateScheme(m_scheme->model()->index(qMax(0, m_scheme->currentIndex()), 0));
    }
    Q_EMIT settingsChanged(objectName());
}

void SettingsDialog::updateWidgets()
{
    KConfigDialog::updateWidgets();
    const QModelIndex scheme = KColorSchemeManager::instance()->indexForSchemeId(KColorSchemeManager::instance()->activeSchemeId());
    m_scheme->setCurrentIndex(scheme.isValid() ? scheme.row() : 0);
    m_language->setCurrentIndex(qMax(0, m_language->findData(KalamusSettings::spellingLanguage())));
}

void SettingsDialog::updateWidgetsDefault()
{
    KConfigDialog::updateWidgetsDefault();
    m_scheme->setCurrentIndex(0);
    m_language->setCurrentIndex(0);
}
