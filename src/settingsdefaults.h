// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>

/// Defaults the settings file can't spell out as plain values.
namespace SettingsDefaults
{
/// The system font, a little larger: Markdown is for reading.
inline QFont textFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(qRound(font.pointSizeF() * 1.15 * 2) / 2.0); // to the half point
    return font;
}

inline QFont codeFont()
{
    // The real family ("Hack"), not an alias like "monospace", so the font list can show it.
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont font(QFontInfo(fixed).family());
    font.setPointSizeF(fixed.pointSizeF());
    return font;
}
}
