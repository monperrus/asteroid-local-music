/* Local music UI connection to the session player. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <QObject>
#include <QVariantMap>

class PlaybackClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(int trackNumber READ trackNumber NOTIFY trackChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
public:
    explicit PlaybackClient(QObject *parent = nullptr);
    QString title() const { return m_state.value(QStringLiteral("title")).toString(); }
    QString artist() const { return m_state.value(QStringLiteral("artist")).toString(); }
    int trackNumber() const { return m_state.value(QStringLiteral("trackNumber")).toInt(); }
    int trackCount() const { return m_state.value(QStringLiteral("trackCount")).toInt(); }
    bool playing() const { return m_state.value(QStringLiteral("playing")).toBool(); }
    double volume() const { return m_state.value(QStringLiteral("volume"), 1.0).toDouble(); }
    QString status() const { return m_state.value(QStringLiteral("status")).toString(); }
    QString output() const { return m_state.value(QStringLiteral("output")).toString(); }
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void reload();
    void setVolume(double volume);
signals:
    void trackChanged();
    void playingChanged();
    void volumeChanged();
    void statusChanged();
    void outputChanged();
private slots:
    void refresh();
private:
    void readState(const QString &method);
    QVariantMap m_state;
};
