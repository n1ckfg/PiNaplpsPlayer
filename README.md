## PiNaplpsPlayer

### Compatibility
- Tested with <a href="https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/of_v0.12.1_linuxaarch64_release.tar.gz">openFrameworks 0.12.1 aarch64</a>, Raspberry Pi OS Bookworm on Raspberry Pi 4B and 5.
- Tested with EGL customized <a href="https://fox-gieg.com/patches/rpi/of_v0.11.2_linuxarmv6l_release_egl.zip">openFrameworks 0.11.2 armv6</a> on Raspberry Pi OS Buster on Raspberry Pi 3B+.
- Likely also works with <a href="https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/of_v0.12.1_linuxarmv6l_release.tar.gz">openFrameworks 0.12.1 armv6</a> but has not been extensively tested yet.

### Setup
- Set the RPi to X11 mode and reboot. (sudo raspi-config > Advanced Options > Wayland > X11)
- Clone in `apps/myApps/` directory of your openFrameworks installation.
- Run `bash setup.sh`.
- Examine `bin/data/settings.html` for settings.
- Compile with `make` (or for example `make -j4` with at least 4GB RAM).
- Launch with `./bin/PiNaplpsPlayer`  

