/* Local MP3 playback backend for AsteroidOS. SPDX-License-Identifier: GPL-3.0-or-later */
#include "localplayback.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <csignal>

LocalPlayback::LocalPlayback(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        const bool startPending = m_pendingTrack >= 0;
        const bool advance = !m_userStopped && !startPending;
        m_paused = false;
        emit playingChanged();
        if (startPending) {
            m_currentTrack = m_pendingTrack;
            m_pendingTrack = -1;
            startCurrentTrack();
        } else if (advance && !m_tracks.isEmpty()) {
            m_currentTrack = (m_currentTrack + 1) % m_tracks.size();
            emit trackChanged();
            startCurrentTrack();
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process.error() == QProcess::FailedToStart)
            setStatus(tr("Could not start GStreamer playback."));
    });
    loadTracks();
}

QString LocalPlayback::title() const
{
    return m_currentTrack >= 0 && m_currentTrack < m_tracks.size()
        ? QFileInfo(m_tracks.at(m_currentTrack)).completeBaseName() : QString();
}

QString LocalPlayback::artist() const
{
    const QString name = title();
    const int separator = name.indexOf(QStringLiteral(" -"));
    return separator >= 0 ? name.left(separator) : tr("Local music");
}

int LocalPlayback::trackNumber() const { return m_currentTrack + 1; }
int LocalPlayback::trackCount() const { return m_tracks.size(); }
bool LocalPlayback::playing() const { return m_process.state() == QProcess::Running && !m_paused; }
qreal LocalPlayback::volume() const { return m_volume; }
QString LocalPlayback::status() const { return m_status; }

void LocalPlayback::setVolume(qreal volume)
{
    volume = qBound<qreal>(0.0, volume, 1.0);
    if (qFuzzyCompare(m_volume, volume)) return;
    m_volume = volume;
    emit volumeChanged();
    applyVolumeToCurrentStream();
}

void LocalPlayback::loadTracks()
{
    const QString musicDirectory = QDir::homePath() + QStringLiteral("/Music");
    QDir directory(musicDirectory);
    m_tracks = directory.entryList({QStringLiteral("*.mp3"), QStringLiteral("*.MP3")}, QDir::Files, QDir::Name);
    for (QString &track : m_tracks) track = directory.absoluteFilePath(track);
    if (m_tracks.isEmpty()) setStatus(tr("Copy MP3 files to %1").arg(musicDirectory));
    else { m_currentTrack = 0; setStatus(tr("%1 tracks in local library").arg(m_tracks.size())); }
    emit trackChanged();
}

void LocalPlayback::reload() { stop(); loadTracks(); }

void LocalPlayback::playPause()
{
    if (m_tracks.isEmpty()) { loadTracks(); if (m_tracks.isEmpty()) return; }
    if (m_process.state() != QProcess::Running) { if (m_currentTrack < 0) m_currentTrack = 0; startCurrentTrack(); }
    else if (m_paused) { ::kill(static_cast<pid_t>(m_process.processId()), SIGCONT); m_paused = false; emit playingChanged(); }
    else { ::kill(static_cast<pid_t>(m_process.processId()), SIGSTOP); m_paused = true; emit playingChanged(); }
}

void LocalPlayback::playTrack(int index)
{
    if (m_tracks.isEmpty()) return;
    index = (index + m_tracks.size()) % m_tracks.size();
    if (m_process.state() == QProcess::Running) {
        m_pendingTrack = index; m_userStopped = true;
        ::kill(static_cast<pid_t>(m_process.processId()), SIGKILL);
    } else { m_currentTrack = index; emit trackChanged(); startCurrentTrack(); }
}

void LocalPlayback::next() { if (!m_tracks.isEmpty()) playTrack(m_currentTrack + 1); }
void LocalPlayback::previous() { if (!m_tracks.isEmpty()) playTrack(m_currentTrack - 1); }

void LocalPlayback::stop()
{
    m_pendingTrack = -1; m_userStopped = true;
    if (m_process.state() == QProcess::Running) ::kill(static_cast<pid_t>(m_process.processId()), SIGKILL);
    m_paused = false; emit playingChanged();
}

void LocalPlayback::applyVolumeToCurrentStream()
{
    if (m_process.state() != QProcess::Running)
        return;

    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sink-inputs")});
    if (!pactl.waitForFinished(1000))
        return;

    const QString processId = QString::number(m_process.processId());
    const QRegularExpression streamPattern(
        QStringLiteral("Sink Input #(\\d+)(?:(?!Sink Input #)[\\s\\S])*?application\\.process\\.id = \\\"%1\\\"")
            .arg(QRegularExpression::escape(processId)));
    const QRegularExpressionMatch match = streamPattern.match(QString::fromLocal8Bit(pactl.readAllStandardOutput()));
    if (!match.hasMatch())
        return;

    QProcess::startDetached(QStringLiteral("pactl"), {
        QStringLiteral("set-sink-input-volume"), match.captured(1),
        QStringLiteral("%1%").arg(qRound(m_volume * 100))
    });
}

void LocalPlayback::startCurrentTrack()
{
    if (m_currentTrack < 0 || m_currentTrack >= m_tracks.size()) return;
    m_userStopped = false; m_paused = false; setStatus(QString());
    m_process.start(QStringLiteral("gst-launch-1.0"), {
        QStringLiteral("-q"), QStringLiteral("filesrc"), QStringLiteral("location=") + m_tracks.at(m_currentTrack),
        QStringLiteral("!"), QStringLiteral("mpegaudioparse"), QStringLiteral("!"), QStringLiteral("mpg123audiodec"),
        QStringLiteral("!"), QStringLiteral("audioconvert"), QStringLiteral("!"), QStringLiteral("audioresample"),
        QStringLiteral("!"), QStringLiteral("pulsesink"), QStringLiteral("volume=") + QString::number(m_volume, 'f', 2) });
    emit trackChanged(); emit playingChanged();
}

void LocalPlayback::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status; emit statusChanged();
}
