/* Local MP3 playback backend for AsteroidOS. SPDX-License-Identifier: GPL-3.0-or-later */
#include "localplayback.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QVariantMap>
#include <cerrno>
#include <cmath>
#include <csignal>

static QString playbackStatePath()
{
    return QDir::temp().filePath(QStringLiteral("asteroid-local-music-player.ini"));
}

LocalPlayback::LocalPlayback(QObject *parent) : QObject(parent)
{
    QSettings preferences(QStringLiteral("asteroid-local-music"), QStringLiteral("playback"));
    bool validVolume = false;
    const double storedVolume = preferences.value(QStringLiteral("volume"), 1.0).toDouble(&validVolume);
    if (validVolume && std::isfinite(storedVolume))
        m_volume = qBound(0.0, storedVolume, 1.0);

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LocalPlayback::readPlayerOutput);
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_userStopped) {
            m_process.kill();
            return;
        }
        m_playerProcessId = m_process.processId();
        savePlaybackState();
        m_sinkWatcher.start();
        emit playingChanged();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        readPlayerOutput();
        finishTrack(m_reachedEnd && exitStatus == QProcess::NormalExit && exitCode == 0);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setStatus(tr("Could not start GStreamer playback."));
            finishTrack(false);
        }
    });
    connect(&m_amplifier, &SpeakerAmp::failed, this, [this](const QString &reason) {
        setStatus(reason);
    });
    m_sinkWatcher.setInterval(3000);
    connect(&m_sinkWatcher, &QTimer::timeout, this, &LocalPlayback::followSinkChanges);
    connect(this, &LocalPlayback::trackChanged, this, &LocalPlayback::stateChanged);
    connect(this, &LocalPlayback::playingChanged, this, &LocalPlayback::stateChanged);
    connect(this, &LocalPlayback::volumeChanged, this, &LocalPlayback::stateChanged);
    connect(this, &LocalPlayback::statusChanged, this, &LocalPlayback::stateChanged);
    connect(this, &LocalPlayback::outputChanged, this, &LocalPlayback::stateChanged);
    loadTracks();
    restorePlaybackState();
}

LocalPlayback::~LocalPlayback()
{
    m_process.disconnect(this);
    stop();
    if (m_process.state() == QProcess::Starting && m_process.waitForStarted(1000))
        m_process.kill();
    if (m_process.state() != QProcess::NotRunning)
        m_process.waitForFinished(1000);
}

QVariantMap LocalPlayback::open()
{
    if (m_process.state() == QProcess::NotRunning && !playerIsRunning())
        loadTracks(true);
    return snapshot();
}

QVariantMap LocalPlayback::snapshot() const
{
    return {
        {QStringLiteral("title"), title()}, {QStringLiteral("artist"), artist()},
        {QStringLiteral("trackNumber"), trackNumber()}, {QStringLiteral("trackCount"), trackCount()},
        {QStringLiteral("playing"), playing()}, {QStringLiteral("volume"), volume()},
        {QStringLiteral("status"), status()}, {QStringLiteral("output"), output()}
    };
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

void LocalPlayback::setVolume(double volume)
{
    if (!std::isfinite(volume)) return;
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(m_volume, volume)) return;
    m_volume = volume;
    QSettings preferences(QStringLiteral("asteroid-local-music"), QStringLiteral("playback"));
    preferences.setValue(QStringLiteral("volume"), m_volume);
    preferences.sync();
    emit volumeChanged();
    applyVolumeToCurrentStream();
}

void LocalPlayback::loadTracks(bool preserveSelection)
{
    const QString selected = preserveSelection ? m_tracks.value(m_currentTrack) : QString();
    const QString musicDirectory = QDir::homePath() + QStringLiteral("/Music");
    QDir directory(musicDirectory);
    QStringList tracks = directory.entryList({QStringLiteral("*.mp3"), QStringLiteral("*.MP3")}, QDir::Files, QDir::Name);
    for (QString &track : tracks) track = directory.absoluteFilePath(track);
    if (preserveSelection && tracks == m_tracks)
        return;
    m_tracks = tracks;
    const int selectedIndex = m_tracks.indexOf(selected);
    m_currentTrack = m_tracks.isEmpty() ? -1 : qMax(0, selectedIndex);
    if (m_tracks.isEmpty()) setStatus(tr("Copy MP3 files to %1").arg(musicDirectory));
    else setStatus(QString());
    emit trackChanged();
}

void LocalPlayback::reload() { stop(); loadTracks(); }

void LocalPlayback::playPause()
{
    if (m_tracks.isEmpty()) { loadTracks(); if (m_tracks.isEmpty()) return; }
    if (!playerIsRunning()) {
        if (m_process.state() == QProcess::NotRunning) startCurrentTrack();
    }
    else if (m_paused) { ::kill(static_cast<pid_t>(m_playerProcessId), SIGCONT); m_paused = false; savePlaybackState(); emit playingChanged(); }
    else { ::kill(static_cast<pid_t>(m_playerProcessId), SIGSTOP); m_paused = true; savePlaybackState(); emit playingChanged(); }
}

void LocalPlayback::playTrack(int index)
{
    if (m_tracks.isEmpty()) return;
    index = (index + m_tracks.size()) % m_tracks.size();
    if (m_process.state() != QProcess::NotRunning) {
        m_pendingTrack = index; m_userStopped = true;
        if (m_process.state() == QProcess::Running) m_process.kill();
    } else {
        if (playerIsRunning())
            ::kill(static_cast<pid_t>(m_playerProcessId), SIGKILL);
        m_playerProcessId = 0;
        clearPlaybackState();
        m_currentTrack = index; emit trackChanged(); startCurrentTrack();
    }
}

void LocalPlayback::next() { playTrack((m_pendingTrack >= 0 ? m_pendingTrack : m_currentTrack) + 1); }
void LocalPlayback::previous() { playTrack((m_pendingTrack >= 0 ? m_pendingTrack : m_currentTrack) - 1); }

void LocalPlayback::stop()
{
    m_pendingTrack = -1; m_userStopped = true;
    if (m_process.state() == QProcess::Running) m_process.kill();
    else if (playerIsRunning()) ::kill(static_cast<pid_t>(m_playerProcessId), SIGKILL);
    m_playerProcessId = 0;
    m_sinkWatcher.stop();
    clearPlaybackState();
    setSink(QString());
    m_paused = false; emit playingChanged();
}

void LocalPlayback::applyVolumeToCurrentStream()
{
    m_volumePending = true;
    const int sinkInput = currentSinkInputId();
    if (sinkInput < 0)
        return;

    // PulseAudio's native volume uses 65536 units for 100%.
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {
        QStringLiteral("set-sink-input-volume"), QString::number(sinkInput),
        QString::number(qRound(m_volume * 65536))
    });
    m_volumePending = !pactl.waitForFinished(1000)
        || pactl.exitStatus() != QProcess::NormalExit || pactl.exitCode() != 0;
}

int LocalPlayback::currentSinkInputId() const
{
    if (!playerIsRunning())
        return -1;

    QProcess pactl;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    pactl.setProcessEnvironment(environment);
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
    if (!playerIsRunning()) {
        // A process adopted after an upgrade has no output pipe to report EOS.
        if (m_process.state() == QProcess::NotRunning)
            finishTrack(false);
        return;
    }
    if (m_volumePending)
        applyVolumeToCurrentStream();
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
    savePlaybackState();
    applyVolumeToCurrentStream();
}

void LocalPlayback::readPlayerOutput()
{
    while (m_process.canReadLine()) {
        if (m_process.readLine().startsWith("Got EOS from element "))
            m_reachedEnd = true;
    }
}

void LocalPlayback::finishTrack(bool reachedEnd)
{
    m_playerProcessId = 0;
    m_paused = false;
    m_sinkWatcher.stop();
    clearPlaybackState();
    emit playingChanged();

    if (m_pendingTrack >= 0) {
        m_currentTrack = m_pendingTrack;
        m_pendingTrack = -1;
        startCurrentTrack();
    } else if (!m_userStopped && reachedEnd && !m_tracks.isEmpty()) {
        m_currentTrack = (m_currentTrack + 1) % m_tracks.size();
        startCurrentTrack();
    } else {
        setSink(QString());
        if (!m_userStopped && m_status.isEmpty())
            setStatus(tr("Playback stopped before the end of this track."));
    }
}

void LocalPlayback::startCurrentTrack()
{
    if (m_currentTrack < 0 || m_currentTrack >= m_tracks.size()) return;
    m_userStopped = false; m_paused = false; setStatus(QString());
    m_reachedEnd = false;
    m_volumePending = false;
    const QString sink = preferredSink();
    // GStreamer's pulsesink takes linear gain, which PulseAudio maps to the
    // cube root on its native volume scale.
    const double gain = m_volume * m_volume * m_volume;
    QStringList arguments = {
        QStringLiteral("--no-fault"), QStringLiteral("filesrc"), QStringLiteral("location=") + m_tracks.at(m_currentTrack),
        QStringLiteral("!"), QStringLiteral("mpegaudioparse"), QStringLiteral("!"), QStringLiteral("mpg123audiodec"),
        QStringLiteral("!"), QStringLiteral("audioconvert"), QStringLiteral("!"), QStringLiteral("audioresample"),
        QStringLiteral("!"), QStringLiteral("pulsesink"), QStringLiteral("volume=") + QString::number(gain, 'g', 16) };
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
    QSettings state(playbackStatePath(), QSettings::IniFormat);
    state.setValue(QStringLiteral("pid"), m_playerProcessId);
    state.setValue(QStringLiteral("track"), m_currentTrack);
    state.setValue(QStringLiteral("paused"), m_paused);
    state.setValue(QStringLiteral("sink"), m_sink);
    state.sync();
}

void LocalPlayback::clearPlaybackState()
{
    QSettings state(playbackStatePath(), QSettings::IniFormat);
    state.clear();
    state.sync();
}

void LocalPlayback::restorePlaybackState()
{
    QSettings state(playbackStatePath(), QSettings::IniFormat);
    const qint64 savedPid = state.value(QStringLiteral("pid")).toLongLong();
    if (m_tracks.isEmpty()) {
        clearPlaybackState();
        return;
    }

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
        if (QFileInfo(QString::fromLocal8Bit(arguments.value(0))).fileName() != QStringLiteral("gst-launch-1.0")
            || !arguments.contains("pulsesink"))
            continue;
        QString location, sink;
        for (const QByteArray &argument : arguments) {
            if (argument.startsWith("location="))
                location = QString::fromLocal8Bit(argument.mid(qstrlen("location=")));
            if (argument.startsWith("device="))
                sink = QString::fromLocal8Bit(argument.mid(qstrlen("device=")));
        }
        const int track = m_tracks.indexOf(location);
        if (track < 0)
            continue;
        m_playerProcessId = pid;
        m_currentTrack = track;
        m_paused = pid == savedPid && state.value(QStringLiteral("paused")).toBool();
        if (pid == savedPid)
            sink = state.value(QStringLiteral("sink"), sink).toString();
        setSink(sink);
        savePlaybackState();
        applyVolumeToCurrentStream();
        m_sinkWatcher.start();
        emit trackChanged();
        emit playingChanged();
        return;
    }
    clearPlaybackState();
}

void LocalPlayback::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status; emit statusChanged();
}
