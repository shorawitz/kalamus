// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/**
 * Plasma's recent documents (the launcher's Recent Files, the file dialog's Recent
 * Files place, Dolphin's recentlyused:/). Kalamus tells Plasma's activity manager over
 * D-Bus that it used a file; nothing happens outside Plasma or in tests.
 */
namespace RecentDocuments
{
void add(const QString &path);
}
