/* TFA98xx SpeakerBoost startup for the local speaker. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <QObject>
#include <QTimer>

/*
 * The vendor audio HAL only attempts the TFA98xx SpeakerBoost startup at
 * adev_open, when no stream and therefore no bit clock exists, so the SBSL
 * bit never gets set and the amplifier output stage stays switched off. This
 * sets SBSL over i2c once a stream is clocking the amplifier (PLLS asserted)
 * and restores SYS_CTRL when playback stops.
 *
 * Keep the sink volume moderate: SpeakerBoost is the speaker's excursion and
 * thermal protection, and starting it by hand does not verify that the
 * speaker model is resident, so that protection state is unknown.
 */
class SpeakerAmp : public QObject
{
    Q_OBJECT
public:
    explicit SpeakerAmp(QObject *parent = nullptr);
    bool isAccessible() const;
    void enable();
    void disable();
signals:
    void failed(const QString &reason);
private:
    void pollForClock();
    int readRegister(int reg) const;
    bool writeRegister(int reg, int value) const;
    QTimer m_timer;
    int m_attempts = 0;
    int m_savedSysCtrl = -1;
};
