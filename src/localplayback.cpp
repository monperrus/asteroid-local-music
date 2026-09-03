/* Local MP3 playback backend for AsteroidOS. SPDX-License-Identifier: GPL-3.0-or-later */
#include "localplayback.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <cerrno>
#include <csignal>

LocalPlayback::LocalPlayback(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::started, this, [this] {
        m_playerProcessId = m_process.processId();
        savePlaybackState();
        emit playingChanged();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        const bool startPending = m_pendingTrack >= 0;
        const bool advance = !m_userStopped && !startPending;
        m_playerProcessId = 0;
        clearPlaybackState();
        m_paused = false;
        if (!startPending && !advance)
            setSink(QString());
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
    connect(&m_amplifier, &SpeakerAmp::failed, this, [this](const QString &reason) {
        setStatus(reason);
    });
    m_sinkWatcher.setInterval(3000);
    connect(&m_sinkWatcher, &QTimer::timeout, this, &LocalPlayback::followSinkChanges);
    m_sinkWatcher.start();
    loadTracks();
    restorePlaybackState();
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
bool LocalPlayback::playing() const { return playerIsRunning() && !m_paused; }
qreal LocalPlayback::volume() const { return m_volume; }
QString LocalPlayback::status() const { return m_status; }

QString LocalPlayback::output() const
{
    if (m_sink.isEmpty()) return QString();
    return m_sink.startsWith(QStringLiteral("bluez_sink.")) ? tr("Bluetooth") : tr("Speaker");
}

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
    if (!playerIsRunning()) { if (m_currentTrack < 0) m_currentTrack = 0; startCurrentTrack(); }
    else if (m_paused) { ::kill(static_cast<pid_t>(m_playerProcessId), SIGCONT); m_paused = false; savePlaybackState(); emit playingChanged(); }
    else { ::kill(static_cast<pid_t>(m_playerProcessId), SIGSTOP); m_paused = true; savePlaybackState(); emit playingChanged(); }
}

void LocalPlayback::playTrack(int index)
{
    if (m_tracks.isEmpty()) return;
    index = (index + m_tracks.size()) % m_tracks.size();
    if (m_process.state() == QProcess::Running) {
        m_pendingTrack = index; m_userStopped = true;
        ::kill(static_cast<pid_t>(m_process.processId()), SIGKILL);
    } else {
        if (playerIsRunning())
            ::kill(static_cast<pid_t>(m_playerProcessId), SIGKILL);
        m_playerProcessId = 0;
        clearPlaybackState();
        m_currentTrack = index; emit trackChanged(); startCurrentTrack();
    }
}

void LocalPlayback::next() { if (!m_tracks.isEmpty()) playTrack(m_currentTrack + 1); }
void LocalPlayback::previous() { if (!m_tracks.isEmpty()) playTrack(m_currentTrack - 1); }

void LocalPlayback::stop()
{
    m_pendingTrack = -1; m_userStopped = true;
    if (m_process.state() == QProcess::Running) ::kill(static_cast<pid_t>(m_process.processId()), SIGKILL);
    else if (playerIsRunning()) ::kill(static_cast<pid_t>(m_playerProcessId), SIGKILL);
    m_playerProcessId = 0;
    clearPlaybackState();
    setSink(QString());
    m_paused = false; emit playingChanged();
}

void LocalPlayback::applyVolumeToCurrentStream()
{
    const int sinkInput = currentSinkInputId();
    if (sinkInput < 0)
        return;

    QProcess::startDetached(QStringLiteral("pactl"), {
        QStringLiteral("set-sink-input-volume"), QString::number(sinkInput),
        QStringLiteral("%1%").arg(qRound(m_volume * 100))
    });
}

int LocalPlayback::currentSinkInputId() const
{
    if (!playerIsRunning())
        return -1;

    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sink-inputs")});
    if (!pactl.waitForFinished(1000))
        return -1;

    const QString processId = QString::number(m_playerProcessId);
    const QRegularExpression streamPattern(
        QStringLiteral("Sink Input #(\\d+)(?:(?!Sink Input #)[\\s\\S])*?application\\.process\\.id = \\\"%1\\\"")
            .arg(QRegularExpression::escape(processId)));
    const QRegularExpressionMatch match = streamPattern.match(QString::fromLocal8Bit(pactl.readAllStandardOutput()));
    if (!match.hasMatch())
        return -1;
    return match.captured(1).toInt();
}

/*
 * Bluetooth headphones, when connected, show up as a bluez_sink.* A2DP sink
 * and are what the user means to listen through. Without them, play on the
 * watch speaker (sink.primary from module-droid-card) rather than on whatever
 * PulseAudio happens to consider the default sink.
 */
QString LocalPlayback::preferredSink() const
{
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sinks")});
    if (!pactl.waitForFinished(1000))
        return QString();

    QString bluetooth, speaker, fallback;
    const QStringList lines = QString::fromLocal8Bit(pactl.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString name = line.split(QLatin1Char('\t')).value(1);
        if (name.isEmpty() || name.startsWith(QStringLiteral("auto_null")))
            continue;
        if (name.startsWith(QStringLiteral("bluez_sink."))) {
            if (bluetooth.isEmpty()) bluetooth = name;
        } else if (name == QStringLiteral("sink.primary")) {
            speaker = name;
        } else if (fallback.isEmpty()) {
            fallback = name;
        }
    }
    if (!bluetooth.isEmpty()) return bluetooth;
    return speaker.isEmpty() ? fallback : speaker;
}

void LocalPlayback::prepareSink(const QString &sink)
{
    if (sink.isEmpty())
        return;
    QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), sink, QStringLiteral("0")});
    if (sink == QStringLiteral("sink.primary")) {
        // The speaker amplifier is started without its excursion and thermal
        // protection verified, so cap the sink; the app volume rides on the
        // stream itself.
        QProcess::execute(QStringLiteral("pactl"),
                          {QStringLiteral("set-sink-volume"), sink, QStringLiteral("55%")});
    }
}

void LocalPlayback::setSink(const QString &sink)
{
    if (m_sink != sink) {
        m_sink = sink;
        emit outputChanged();
    }
    prepareSink(sink);
    if (sink == QStringLiteral("sink.primary")) m_amplifier.enable();
    else m_amplifier.disable();
}

void LocalPlayback::followSinkChanges()
{
    if (!playerIsRunning())
        return;
    const QString target = preferredSink();
    if (target.isEmpty() || target == m_sink)
        return;
    const int sinkInput = currentSinkInputId();
    if (sinkInput < 0)
        return;
    if (QProcess::execute(QStringLiteral("pactl"),
                          {QStringLiteral("move-sink-input"), QString::number(sinkInput), target}) != 0)
        return;
    setSink(target);
    applyVolumeToCurrentStream();
}

void LocalPlayback::startCurrentTrack()
{
    if (m_currentTrack < 0 || m_currentTrack >= m_tracks.size()) return;
    m_userStopped = false; m_paused = false; setStatus(QString());
    const QString sink = preferredSink();
    QStringList arguments = {
        QStringLiteral("-q"), QStringLiteral("filesrc"), QStringLiteral("location=") + m_tracks.at(m_currentTrack),
        QStringLiteral("!"), QStringLiteral("mpegaudioparse"), QStringLiteral("!"), QStringLiteral("mpg123audiodec"),
        QStringLiteral("!"), QStringLiteral("audioconvert"), QStringLiteral("!"), QStringLiteral("audioresample"),
        QStringLiteral("!"), QStringLiteral("pulsesink"), QStringLiteral("volume=") + QString::number(m_volume, 'f', 2) };
    if (!sink.isEmpty())
        arguments.append(QStringLiteral("device=") + sink);
    setSink(sink);
    m_process.start(QStringLiteral("gst-launch-1.0"), arguments);
    emit trackChanged(); emit playingChanged();
}

bool LocalPlayback::playerIsRunning() const
{
    return m_playerProcessId > 0 && (::kill(static_cast<pid_t>(m_playerProcessId), 0) == 0 || errno == EPERM);
}

void LocalPlayback::savePlaybackState()
{
    QSettings state(QStringLiteral("/tmp/asteroid-local-music-player.ini"), QSettings::IniFormat);
    state.setValue(QStringLiteral("pid"), m_playerProcessId);
    state.setValue(QStringLiteral("track"), m_currentTrack);
    state.setValue(QStringLiteral("paused"), m_paused);
    state.setValue(QStringLiteral("sink"), m_sink);
    state.sync();
}

void LocalPlayback::clearPlaybackState()
{
    QSettings state(QStringLiteral("/tmp/asteroid-local-music-player.ini"), QSettings::IniFormat);
    state.clear();
    state.sync();
}

void LocalPlayback::restorePlaybackState()
{
    QSettings state(QStringLiteral("/tmp/asteroid-local-music-player.ini"), QSettings::IniFormat);
    m_playerProcessId = state.value(QStringLiteral("pid")).toLongLong();
    const int savedTrack = state.value(QStringLiteral("track"), m_currentTrack).toInt();
    m_paused = state.value(QStringLiteral("paused")).toBool();
    if (playerIsRunning() && !m_tracks.isEmpty()) {
        m_currentTrack = qBound(0, savedTrack, m_tracks.size() - 1);
        setSink(state.value(QStringLiteral("sink")).toString());
        emit trackChanged();
        emit playingChanged();
        return;
    }
    m_playerProcessId = 0;
    m_paused = false;
    clearPlaybackState();

    const QDir proc(QStringLiteral("/proc"));
    for (const QString &entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isProcess = false;
        const qint64 pid = entry.toLongLong(&isProcess);
        if (!isProcess)
            continue;
        QFile commandLine(proc.filePath(entry + QStringLiteral("/cmdline")));
        if (!commandLine.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> arguments = commandLine.readAll().split('\0');
        bool isGStreamer = false;
        QString location, sink;
        for (const QByteArray &argument : arguments) {
            if (argument.contains("gst-launch-1.0"))
                isGStreamer = true;
            if (argument.startsWith("location="))
                location = QString::fromLocal8Bit(argument.mid(qstrlen("location=")));
            if (argument.startsWith("device="))
                sink = QString::fromLocal8Bit(argument.mid(qstrlen("device=")));
        }
        if (!isGStreamer || !location.startsWith(QStringLiteral("/home/ceres/Music/")))
            continue;
        const int track = m_tracks.indexOf(location);
        if (track < 0)
            continue;
        m_playerProcessId = pid;
        m_currentTrack = track;
        setSink(sink);
        savePlaybackState();
        emit trackChanged();
        emit playingChanged();
        return;
    }
}

void LocalPlayback::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status; emit statusChanged();
}
