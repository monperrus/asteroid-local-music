/* TFA98xx SpeakerBoost startup for the local speaker. SPDX-License-Identifier: GPL-3.0-or-later */
#include "speakeramp.h"

#include <QFileInfo>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
constexpr const char *DevicePath = "/dev/i2c-5";
constexpr int I2cSlave = 0x0703;
constexpr int Address = 0x34;
constexpr int StatusRegister = 0x00;
constexpr int SysCtrlRegister = 0x09;
constexpr int SpeakerBoostStartup = 1 << 5;
constexpr int PllLocked = 1 << 1;
constexpr int PollInterval = 500;
constexpr int MaxAttempts = 20;
}

SpeakerAmp::SpeakerAmp(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(PollInterval);
    connect(&m_timer, &QTimer::timeout, this, &SpeakerAmp::pollForClock);
}

bool SpeakerAmp::isAccessible() const
{
    return QFileInfo(QLatin1String(DevicePath)).isWritable();
}

void SpeakerAmp::enable()
{
    if (m_savedSysCtrl >= 0 || m_timer.isActive())
        return;
    if (!isAccessible()) {
        emit failed(tr("No access to the speaker amplifier (%1).").arg(QLatin1String(DevicePath)));
        return;
    }
    m_attempts = 0;
    m_timer.start();
}

void SpeakerAmp::disable()
{
    m_timer.stop();
    if (m_savedSysCtrl < 0)
        return;
    // The amplifier latches SBSL until it powers down, so this write-back may
    // not clear the bit immediately; it clears itself once the amplifier goes
    // idle, which needs no cleanup here.
    writeRegister(SysCtrlRegister, m_savedSysCtrl);
    m_savedSysCtrl = -1;
}

void SpeakerAmp::pollForClock()
{
    const int status = readRegister(StatusRegister);
    if (status >= 0 && (status & PllLocked)) {
        m_timer.stop();
        const int sysCtrl = readRegister(SysCtrlRegister);
        if (sysCtrl < 0 || !writeRegister(SysCtrlRegister, sysCtrl | SpeakerBoostStartup)) {
            emit failed(tr("Could not start the speaker amplifier."));
            return;
        }
        m_savedSysCtrl = sysCtrl;
        return;
    }
    if (++m_attempts < MaxAttempts)
        return;
    m_timer.stop();
    // No bit clock: starting the amplifier here would switch it on blind.
    emit failed(tr("The speaker amplifier is not being clocked."));
}

int SpeakerAmp::readRegister(int reg) const
{
    const int fd = ::open(DevicePath, O_RDWR);
    if (fd < 0)
        return -1;
    int value = -1;
    unsigned char address = static_cast<unsigned char>(reg);
    unsigned char data[2];
    if (::ioctl(fd, I2cSlave, Address) >= 0 && ::write(fd, &address, 1) == 1
        && ::read(fd, data, 2) == 2)
        value = (data[0] << 8) | data[1];
    ::close(fd);
    return value;
}

bool SpeakerAmp::writeRegister(int reg, int value) const
{
    const int fd = ::open(DevicePath, O_RDWR);
    if (fd < 0)
        return false;
    const unsigned char frame[3] = {static_cast<unsigned char>(reg),
                                    static_cast<unsigned char>((value >> 8) & 0xFF),
                                    static_cast<unsigned char>(value & 0xFF)};
    const bool written = ::ioctl(fd, I2cSlave, Address) >= 0 && ::write(fd, frame, 3) == 3;
    ::close(fd);
    return written;
}
