/* Local music session service. SPDX-License-Identifier: GPL-3.0-or-later */

#include "localplayback.h"

#include <QCoreApplication>
#include <QDBusConnection>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.asteroidos.LocalMusic")))
        return 1;
    LocalPlayback player;
    if (!bus.registerObject(QStringLiteral("/"), &player,
                            QDBusConnection::ExportScriptableSlots
                            | QDBusConnection::ExportScriptableInvokables
                            | QDBusConnection::ExportScriptableSignals))
        return 1;
    bus.connect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"),
                QStringLiteral("org.freedesktop.DBus.Local"), QStringLiteral("Disconnected"),
                &app, SLOT(quit()));
    return app.exec();
}
