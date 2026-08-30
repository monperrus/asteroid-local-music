/* Local MP3 playback backend for AsteroidOS. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class LocalPlayback : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(int trackNumber READ trackNumber NOTIFY trackChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit LocalPlayback(QObject *parent = nullptr);
    QString title() const;
    QString artist() const;
    int trackNumber() const;
    int trackCount() const;
    bool playing() const;
    qreal volume() const;
    QString status() const;
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void reload();
    void setVolume(qreal volume);
signals:
    void trackChanged();
    void playingChanged();
    void volumeChanged();
    void statusChanged();
private:
    void startCurrentTrack();
    void playTrack(int index);
    void stop();
    void setStatus(const QString &status);
    void loadTracks();
    QStringList m_tracks;
    QProcess m_process;
    int m_currentTrack = -1;
    int m_pendingTrack = -1;
    qreal m_volume = 1.0;
    bool m_paused = false;
    bool m_userStopped = false;
    QString m_status;
};
