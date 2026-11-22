/* Local music UI connection to the session player. SPDX-License-Identifier: GPL-3.0-or-later */

#include "playbackclient.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>

static const auto service = QStringLiteral("org.asteroidos.LocalMusic");
static const auto objectPath = QStringLiteral("/");

static QDBusMessage request(const QString &name)
{
    return QDBusMessage::createMethodCall(service, objectPath, service, name);
}

static void command(const QString &name)
{
    QDBusConnection::sessionBus().asyncCall(request(name));
}

PlaybackClient::PlaybackClient(QObject *parent) : QObject(parent)
{
    auto bus = QDBusConnection::sessionBus();
    bus.connect(service, objectPath, service, QStringLiteral("stateChanged"), this, SLOT(refresh()));
    auto *watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &PlaybackClient::refresh);
    readState(QStringLiteral("open"));
}

void PlaybackClient::refresh()
{
    readState(QStringLiteral("snapshot"));
}

void PlaybackClient::readState(const QString &method)
{
    const QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(
        request(method), QDBus::Block, 3000);
    QVariantMap state = reply.isValid() ? reply.value() : QVariantMap{
        {QStringLiteral("status"), tr("Could not connect to the local music player.")}
    };
    m_state.swap(state);
    const auto changed = [this, &state](const QString &key) {
        return state.value(key) != m_state.value(key);
    };
    if (changed(QStringLiteral("title")) || changed(QStringLiteral("artist"))
            || changed(QStringLiteral("trackNumber")) || changed(QStringLiteral("trackCount")))
        emit trackChanged();
    if (changed(QStringLiteral("playing"))) emit playingChanged();
    if (changed(QStringLiteral("volume"))) emit volumeChanged();
    if (changed(QStringLiteral("status"))) emit statusChanged();
    if (changed(QStringLiteral("output"))) emit outputChanged();
}

void PlaybackClient::playPause() { command(QStringLiteral("playPause")); }
void PlaybackClient::next() { command(QStringLiteral("next")); }
void PlaybackClient::previous() { command(QStringLiteral("previous")); }
void PlaybackClient::reload() { command(QStringLiteral("reload")); }

void PlaybackClient::setVolume(double volume)
{
    auto message = request(QStringLiteral("setVolume"));
    message << volume;
    // Read back the committed value so rapid button presses use the latest level.
    QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    refresh();
}
