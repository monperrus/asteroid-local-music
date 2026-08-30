# Asteroid Local Music

A watch-resident MP3 player for [AsteroidOS](https://asteroidos.org/). It plays
files in the watch user's `Music` directory through GStreamer and PulseAudio;
it does not control music on a paired phone.

The controls are previous track, play/pause, next track and volume. Copy MP3s
to `/home/ceres/Music` on the watch, then open the app.

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
    stage/usr/share/applications/asteroid-local-music.desktop \
    ceres@192.168.0.101:/tmp/
```

Remove the stock phone-control app, then install the local app files:

```sh
ssh ceres@192.168.0.101 '
  sudo opkg remove asteroid-music
  sudo install -D -m 755 /tmp/asteroid-local-music /usr/bin/asteroid-local-music
  sudo install -D -m 755 /tmp/asteroid-local-music.so /usr/lib/asteroid-local-music.so
  sudo install -D -m 644 /tmp/asteroid-local-music.desktop \
    /usr/share/applications/asteroid-local-music.desktop
'
```

Restart the launcher or reboot the watch if the new **Local Music** icon does
not appear immediately.
