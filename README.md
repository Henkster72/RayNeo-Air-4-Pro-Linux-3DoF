# RayNeo Air 4 Pro Linux 3DoF

Native Linux experimentation for using the **RayNeo Air 4 Pro** as a low-latency 3DoF head-tracked display. The project is based on the upstream [RayNeo SDK](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR), with Air 4 Pro support and a lightweight Wayland desktop viewport added locally.

This is a working prototype, not a complete OpenXR, Monado, SteamVR, or Linux compositor implementation.

## What works

- RayNeo Air 4 Pro detection over USB (`1bbb:af50`)
- 500 Hz IMU streaming through the existing libusb SDK transport
- Accelerometer, gyroscope, magnetometer, temperature, and device-info events
- Quaternion-based gyro orientation tracking
- Stable yaw, pitch, and roll after stationary calibration
- `R` recentering
- Native Wayland output selection for the RayNeo display
- A synthetic pinned viewport driven by head yaw and pitch
- Real desktop capture through KDE Spectacle
- An additional virtual monitor region to the left of the captured Samsung display

## Tested setup

The current working setup is:

```text
Debian 13
KDE Plasma / Wayland
AMD Renoir / amdgpu
RayNeo Air 4 Pro: 1bbb:af50
Samsung M7: 2560x1440
RayNeo output: 2560x1440
```

The RayNeo is connected as a normal DisplayPort monitor. The viewport selects the native Wayland output named `Technical Concepts Ltd SmartGlasses`. X11/Xwayland is not recommended for the viewport because the compositor may place fullscreen windows on the primary monitor.

## Build on Debian

Install the normal system dependencies:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libusb-1.0-0-dev \
  libsdl2-dev libsdl2-ttf-dev \
  libgl1-mesa-dev \
  spectacle
```

Clone with the OpenVR submodule:

```bash
git clone --recursive https://github.com/Henkster72/RayNeo-Air-4-Pro-Linux-3DoF.git
cd RayNeo-Air-4-Pro-Linux-3DoF
```

Configure and build:

```bash
cmake -B build \
  -DRAYNEO_BUILD_EXAMPLES=ON \
  -DRAYNEO_BUILD_OPENVR_DRIVER=OFF
cmake --build build -j"$(nproc)"
```

The current user must be able to access the USB device. A typical udev rule is:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1bbb", ATTR{idProduct}=="af50", MODE="0660", GROUP="plugdev"
```

After adding the rule, reload udev and replug the glasses:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Run the examples

Raw IMU stream:

```bash
build/examples/simple/RayNeoExample
```

Orientation demo:

```bash
SDL_VIDEODRIVER=wayland build/examples/orientation_demo/RayNeoOrientationDemo
```

Pinned desktop viewport:

```bash
SDL_VIDEODRIVER=wayland build/examples/pinned_viewport/RayNeoPinnedViewport
```

Do not force `SDL_VIDEODRIVER=x11` on a Wayland session. Native Wayland is required here to place the viewport on the RayNeo output instead of the Samsung monitor.

## Viewport behavior

At startup the application captures the complete desktop as a 5120×1440 image. It renders a larger virtual canvas:

```text
[ duplicated Samsung region ][ captured Samsung ][ captured right-hand region ]
```

The captured screen is centered at the neutral head pose. Yaw moves horizontally through this canvas. Pitch moves vertically through a taller virtual space; a strong look up or down moves the finite captured screen out of view.

Controls:

- `R` — recenter; hold the glasses still for one second
- `C` — refresh the desktop capture
- `Esc` — exit

## Current limitations

- Desktop capture is a snapshot at startup. Press `C` to refresh it; it is not yet a continuous PipeWire/KDE screencopy stream.
- The extra left monitor currently duplicates the captured Samsung region rather than providing an independent workspace.
- Orientation is gyro-based and can drift during long sessions.
- Pitch and yaw sensitivity are currently tuned for the tested Samsung M7 / RayNeo setup.
- No positional 6DoF tracking is implemented.
- No OpenXR, Monado, SteamVR, or full virtual-desktop compositor integration is required or included yet.

## Project layout

```text
src/RayneoApi.cpp                         Existing cross-platform SDK transport
examples/simple/                          Raw IMU and device-info example
examples/orientation_demo/                Quaternion orientation demo
examples/pinned_viewport/                 Wayland desktop viewport prototype
include/rayneo/rayneo_api.h               Public C API
```

## Contributing

Useful next steps include:

- continuous Wayland/PipeWire desktop capture without hiding the viewport
- independent live virtual workspaces
- configurable yaw/pitch sensitivity and dead zones
- improved drift correction and magnetometer fusion
- optional OpenXR or compositor integration after the native path is stable

Protocol commands should be derived from known-good SDK or RayNeo implementations. Do not send guessed firmware, update, or service commands to the glasses.

## Credits and references

- [verncat/RayNeo-Air-3S-Pro-OpenVR](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR) — SDK starting point
- [verncat/RayNeo-Air-3S-Pro-OpenVR-Driver](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR-Driver) — driver reference
- [arigandores/AirPin](https://github.com/arigandores/AirPin) — protocol reference
- [peterradzisz/Air4-Pro-Gyro-PC](https://github.com/peterradzisz/Air4-Pro-Gyro-PC) — Air 4 Pro gyro reference

## License

MIT License; see [LICENSE](LICENSE). The upstream SDK copyright and license notice are retained.

---
