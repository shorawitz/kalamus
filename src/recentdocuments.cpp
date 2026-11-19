// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "recentdocuments.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace
{
const auto kService = u"org.kde.ActivityManager"_s;
const auto kPath = u"/ActivityManager/Resources"_s;
const auto kInterface = u"org.kde.ActivityManager.Resources"_s;
constexpr uint kAccessed = 0; // kactivitymanagerd's Event::Accessed

void send(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    // Fire and forget: no reply is needed, and an absent activity manager is fine.
    message.setAutoStartService(false); // only when Plasma's activity manager is running
    QDBusConnection::sessionBus().send(message);
}
} // namespace

void RecentDocuments::add(const QString &path)
{
    if (QStandardPaths::isTestModeEnabled() || !QDBusConnection::sessionBus().isConnected()) {
        return; // tests must not fill the real desktop's history
    }
    const QFileInfo info(path);
    const QString resource = info.absoluteFilePath();
    const QString application = QGuiApplication::desktopFileName().isEmpty() ? QCoreApplication::applicationName() : QGuiApplication::desktopFileName();
    send(u"RegisterResourceMimetype"_s, {resource, QMimeDatabase().mimeTypeForFile(info).name()});
    send(u"RegisterResourceTitle"_s, {resource, info.fileName()});
    send(u"RegisterResourceEvent"_s, {application, 0u, resource, kAccessed});
}
