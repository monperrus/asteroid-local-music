# Asteroid Local Music

A watch-resident MP3 player for [AsteroidOS](https://asteroidos.org/). It plays
files in the watch user's `Music` directory through GStreamer and PulseAudio;
it does not control music on a paired phone.

The controls are previous track, play/pause, next track and volume. Copy MP3s
to `/home/ceres/Music` on the watch, then open the app. After you start playback,
the next track plays automatically at the end of each song, looping back to
the first track. A playback error leaves the current song selected. Playback
continues when you close the app screen, and the volume setting survives
app restarts and watch reboots.

The D-Bus session service owns the GStreamer process and the playlist. The
launcher opens a UI that reconnects to that service.

## Output routing

Playback goes to Bluetooth headphones when they are connected, and to the
watch loudspeaker when they are not. The label at the top of the app says
which one is in use.

The app picks the sink itself instead of relying on the PulseAudio default
sink: the first `bluez_sink.*` A2DP sink if one exists, otherwise
`sink.primary` (the watch speaker, from `module-droid-card`). It rechecks
every three seconds while playing, so connecting or disconnecting headphones
moves the running stream with `pactl move-sink-input` rather than waiting for
the next track.

Playing on the speaker needs one extra step, done by the app: the vendor audio
HAL only attempts the TFA98xx SpeakerBoost startup at `adev_open`, when no
stream and therefore no bit clock exists, so the amplifier output stage stays
switched off. The app waits for the amplifier PLL to lock, then sets the
`SBSL` bit over `/dev/i2c-5` and clears it again when playback stops. This
needs the speaker bringup of
[asteroid-sawfish-audio](https://github.com/monperrus/asteroid-sawfish-audio)
to be in effect: the sound card bound, `/dev/i2c-5` writable and PulseAudio
restarted with the droid card. Two consequences worth knowing:

- The speaker sink is capped at 55% while the app selects it, because
  SpeakerBoost is the speaker's excursion and thermal protection and starting
  it by hand does not verify that the speaker model is resident. The app
  volume rides on the stream itself, below that cap.
- Clearing `SBSL` does not switch the amplifier off: it stays enabled
  (`AMPS`, `SWS`) as long as the droid sink is clocked, and on this bringup
  `sink.primary` was still `RUNNING` minutes after playback ended. Restarting
  PulseAudio drops it. This is the bringup's behaviour, not the app's — the
  same happens after `play-tetris.sh`.

## Install on a watch

The app targets current Qt 6 AsteroidOS nightlies. Install the matching ARM
AsteroidOS SDK, then build from this checkout:

```sh
source /path/to/environment-setup-armv7vehf-neon-oe-linux-gnueabi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR="$PWD/stage" cmake --install build
```

Copy the installed files and a local music folder to the watch (substitute its
IP address if necessary):

```sh
ssh ceres@192.168.0.101 'mkdir -p /home/ceres/Music'
tar -C /path/to/music -cf - . | \
  ssh ceres@192.168.0.101 'tar -C /home/ceres/Music -xf -'
scp stage/usr/bin/asteroid-local-music \
    stage/usr/lib/asteroid-local-music.so \
    stage/usr/libexec/asteroid-local-music-player \
    stage/usr/share/applications/asteroid-local-music.desktop \
    stage/usr/share/dbus-1/services/org.asteroidos.LocalMusic.service \
    ceres@192.168.0.101:/tmp/
```

Remove the stock phone-control app, then install the local app files:

```sh
ssh ceres@192.168.0.101 '
  sudo opkg remove asteroid-music
  sudo install -D -m 755 /tmp/asteroid-local-music /usr/bin/asteroid-local-music
  sudo install -D -m 755 /tmp/asteroid-local-music.so /usr/lib/asteroid-local-music.so
  sudo install -D -m 755 /tmp/asteroid-local-music-player /usr/libexec/asteroid-local-music-player
  sudo install -D -m 644 /tmp/asteroid-local-music.desktop \
    /usr/share/applications/asteroid-local-music.desktop
  sudo install -D -m 644 /tmp/org.asteroidos.LocalMusic.service \
    /usr/share/dbus-1/services/org.asteroidos.LocalMusic.service
'
```

Restart the launcher or reboot the watch if the new **Local Music** icon does
not appear immediately.
