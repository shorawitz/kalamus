// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KConfigDialog>

class QComboBox;

/**
 * Settings > Configure Kalamus: Appearance and Editor pages.
 *
 * Most options are KalamusSettings entries the dialog manages by itself (widgets
 * named kcfg_<Entry>). The color scheme (KColorSchemeManager keeps it) and the
 * dictionary (shown by name, stored as a code) are handled here.
 */
class SettingsDialog : public KConfigDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent);

protected:
    bool hasChanged() override;
    bool isDefault() override;
    void updateSettings() override;
    void updateWidgets() override;
    void updateWidgetsDefault() override;

private:
    QWidget *appearancePage();
    QWidget *editorPage();
    QString selectedSchemeId() const;
    QString selectedLanguage() const;

    QComboBox *m_scheme = nullptr;
    QComboBox *m_language = nullptr;
};
