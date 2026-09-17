## PiNaplpsPlayer

### Compatibility
- Tested with <a href="https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/of_v0.12.1_linuxaarch64_release.tar.gz">openFrameworks 0.12.1 aarch64</a>, Raspberry Pi OS Bookworm on Raspberry Pi 4B and 5.
- Tested with EGL customized <a href="https://fox-gieg.com/patches/rpi/of_v0.11.2_linuxarmv6l_release_egl.zip">openFrameworks 0.11.2 armv6</a> on Raspberry Pi OS Buster on Raspberry Pi 3B+.
- Likely also works with <a href="https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/of_v0.12.1_linuxarmv6l_release.tar.gz">openFrameworks 0.12.1 armv6</a> but has not been extensively tested yet.

### Setup
- Clone in `apps/myApps/` directory of your openFrameworks installation.
- Run `bash setup.sh`.
- Examine `bin/data/settings.html` for settings.
- Compile with `make` (or for example `make -j4` with at least 4GB RAM).
- Launch with `./bin/PiNaplpsPlayer`  

### RPi OS Trixie compatibility
- Disable the screen saver: sudo raspi-config > Display Options > Screen Blanking > Off
- Set the GUI to X11 mode: sudo raspi-config > Advanced Options > Wayland > X11

