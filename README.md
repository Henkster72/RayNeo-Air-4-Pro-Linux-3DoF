# RayNeo Air 4 Pro Linux 3DoF

Native Linux experimentation for using the **RayNeo Air 4 Pro** as a low-latency 3DoF head-tracked display. The project is based on the upstream [RayNeo SDK](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR), with Air 4 Pro support and a lightweight Wayland desktop viewport added locally.

This is a working prototype, not a complete OpenXR, Monado, SteamVR, or Linux compositor implementation.

## Quick start

These steps are the shortest route to seeing the desktop move inside the glasses. They assume Debian 13, KDE Plasma, and a Wayland session.

### 1. Connect the glasses

Connect both:

- DisplayPort or USB-C video, so the RayNeo appears as a normal monitor
- USB data, so Linux can read the IMU

Check that Linux sees the USB device:

```bash
lsusb -d 1bbb:af50
```

You should see a RayNeo device with ID `1bbb:af50`.

Check the desktop session:

```bash
echo "$XDG_SESSION_TYPE"
```

For this prototype it should say:

```text
wayland
```

### 2. Install the dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libusb-1.0-0-dev \
  libsdl2-dev libsdl2-ttf-dev \
  libgl1-mesa-dev \
  spectacle
```

`spectacle` is used to capture the current desktop before the RayNeo viewport opens.

### 3. Download the project

```bash
cd ~/Documents
git clone --recursive https://github.com/Henkster72/RayNeo-Air-4-Pro-Linux-3DoF.git
cd RayNeo-Air-4-Pro-Linux-3DoF
```

### 4. Build it

```bash
cmake -B build \
  -DRAYNEO_BUILD_EXAMPLES=ON \
  -DRAYNEO_BUILD_OPENVR_DRIVER=OFF
cmake --build build -j"$(nproc)"
```

### 5. Start the pinned desktop

Use the native Wayland backend:

```bash
SDL_VIDEODRIVER=wayland build/examples/pinned_viewport/RayNeoPinnedViewport
```

Important: the current viewport uses a desktop snapshot, not a live video stream. It captures the desktop when it starts, so moving video or newly opened windows will not update automatically. Press `C` to take a fresh snapshot.

Keep the glasses still for the first second. The program will print:

```text
Target display: 1: Technical Concepts Ltd SmartGlasses
Gyro calibrated; viewport tracking enabled
```

Now test the movement:

- Turn your head left and right: the desktop moves horizontally.
- Look up and down: the finite virtual screen moves vertically and can leave view.
- Press `R` and hold still for one second to recenter.
- Press `C` to capture the desktop again after changing windows.
- Press `Esc` to quit.

Do not use `SDL_VIDEODRIVER=x11` in a Wayland session. Xwayland may put the viewport on the Samsung monitor instead of the RayNeo.

### 6. Optional: test the IMU first

If you want to confirm the glasses before starting the viewport:

```bash
build/examples/simple/RayNeoExample
```

Move the glasses and look for changing `acc`, `gyro(dps)`, and `tick` values.

## If it does not start

### Permission denied or no device attached

If `lsusb` sees the glasses but the example cannot open them, add a udev rule:

```bash
sudoedit /etc/udev/rules.d/70-rayneo.rules
```

Add this line:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1bbb", ATTR{idProduct}=="af50", MODE="0660", GROUP="plugdev"
```

Then add yourself to `plugdev`, reload the rules, and replug the glasses:

```bash
sudo usermod -aG plugdev "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Log out and in once after adding yourself to the group.

### The viewport appears on the wrong monitor

Confirm that the command includes:

```bash
SDL_VIDEODRIVER=wayland
```

The program should identify the RayNeo as `Technical Concepts Ltd SmartGlasses`. If KDE gives the output a different name, update the display-name match in `examples/pinned_viewport/main.cpp`.

### The desktop image is old

This is expected in version `v0.1`: the prototype captures a snapshot at startup rather than continuously streaming the desktop. Press `C` after changing windows or when a video frame needs updating. The viewport briefly hides while KDE takes the new screenshot.

### The image is dark above or below the screen

That is intentional when looking far up or down: the captured physical screen is finite, so it can move completely outside the virtual viewport.

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
