# RayNeo Air 4 Pro Linux 3DoF

Release: `v0.1.1`

Native Linux experimentation for using compatible **RayNeo glasses** as a normal second display with verified Air 4 Pro IMU access. The project is based on the upstream [RayNeo SDK](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR), with Air 4 Pro support and a small 3DoF visual test added locally.

This is a working prototype, not a complete OpenXR, Monado, SteamVR, or Linux compositor implementation.

## Quick start

These steps are the shortest route to verifying immediate 3DoF head tracking inside the glasses. They assume Debian 13, KDE Plasma, and a Wayland session.

### 1. Connect the glasses

Connect both:

- DisplayPort or USB-C video, so the RayNeo appears as a normal monitor
- USB data, so Linux can read the IMU

Check that Linux sees the USB device. The defaults below are for the Air 4 Pro;
other compatible devices can use their own IDs through `RAYNEO_VID` and
`RAYNEO_PID`:

```bash
lsusb -d "${RAYNEO_VID:-1bbb}:${RAYNEO_PID:-af50}"
```

You should see the glasses listed by USB. The SDK transport is not tied to a
particular source-monitor brand.

For a compatible RayNeo device with different USB IDs, start the viewer with
for example:

```bash
RAYNEO_VID=1234 RAYNEO_PID=5678 rayneo
```

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
  libpipewire-0.3-dev libspa-0.2-dev
```

The SDL/OpenGL packages are only needed for the optional coloured `--demo`
test. The normal second-display setup does not capture the desktop.

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

If CMake mentions a deleted path under `/home/.../ramtmp`, the build directory
was created with temporary dependency paths. Move that generated directory
aside and configure it again:

```bash
mv build "build-stale-$(date +%Y%m%d-%H%M%S)"
cmake -B build \
  -DRAYNEO_BUILD_EXAMPLES=ON \
  -DRAYNEO_BUILD_OPENVR_DRIVER=OFF
cmake --build build -j"$(nproc)"
```

### 5. Use the RayNeo as your normal second display

In **System Settings → Display & Monitor → Display Configuration**:

1. Enable the RayNeo display.
2. Choose **Extend**; do not mirror the Samsung monitor.
3. Place RayNeo to the right of the Samsung monitor.
4. Apply the layout.

Now move Chrome, YouTube, or any other window to the RayNeo display exactly
as you would with any ordinary second monitor. It is live and interactive
without this project running.

Do **not** start a viewport, capture, portal, or virtual-desktop tool for this
normal setup: such a tool draws over the RayNeo display and hides your app.

### 6. Verify the IMU without changing either display

Use this command from the project directory:

```bash
./run-rayneo
```

If you want the shorter `rayneo` command available after opening a new
terminal, install the project-local launcher once:

```bash
./tools/install-rayneo-command.sh
rayneo
```

The default `rayneo` command prints ten seconds of live IMU samples in the
terminal. It does not open a window, capture a monitor, alter KDE settings, or
cover the RayNeo display.

The normal layout is deliberately simple:

```text
Samsung = your normal desktop
RayNeo  = your normal second display, placed to the right
```

Move the glasses and confirm that `acc`, `gyro(dps)`, and `tick` change. This
proves that the USB IMU is working while your normal second display remains
usable.

### 7. Optional: coloured 3DoF tracking test

Run this only when no application needs to remain visible in the RayNeo
glasses:

```bash
rayneo --demo
```

It intentionally opens a fullscreen coloured-panel test on the RayNeo output.
It proves yaw, pitch, roll, calibration, and `R` recentering, but it is not a
desktop viewer and it is not meant to run alongside Chrome.

Keep the glasses still for the first second. The program will print:

```text
Target display: 1: Technical Concepts Ltd SmartGlasses
Gyro calibrated; viewport tracking enabled
```

Now test the movement:

- Turn your head left and right: the colored panels move horizontally.
- Look up and down: the panel grid moves vertically.
- Press `R` and hold still for one second to recenter.
- Press `Esc` to quit.

`Ctrl+C` in the terminal also exits cleanly.

Do not use `SDL_VIDEODRIVER=x11` in a Wayland session. Xwayland may put the
viewport on the source monitor instead of the RayNeo output.

### 8. Live Samsung + Chrome viewport

`rayneo --live` provides the experimental live viewport:

```bash
./run-rayneo --live
```

Keep Chrome open. KDE may show one or two Share dialogs: if a monitor picker
appears, choose the Samsung monitor; then choose the Chrome window. KDE may
restore the Samsung permission silently. Do not select SmartGlasses or New
Virtual Output.

Look straight ahead for the Samsung view and turn right for Chrome. Chrome
clicks are passed through to the selected window, and the viewer is kept above
Chrome so its image does not become stuck after interaction. Hold the glasses
still during calibration. Press `Ctrl+C` to stop the viewer.

This mode requires KDE Plasma Wayland with the ScreenCast and RemoteDesktop
portals, PipeWire, and a physical RayNeo display output. It does not create or
modify KDE virtual desktops.

## Experimental code not part of the normal setup

The repository still contains earlier experimental desktop-capture and KDE
workspace code for development reference. It is not part of the `rayneo`
launcher, does not provide a reliable interactive virtual desktop, and should
not be used for a Samsung + RayNeo extended-display setup.

### Archived workspace notes

The old experimental workspace mode uses two KDE workspaces:

```text
Center = source-monitor desktop content
Right  = Chrome/YouTube content
RayNeo = head-tracked viewer
```

The normal `rayneo` command does not use this mode. To run it separately while
troubleshooting, create the setup with:

```bash
tools/setup-kde-two-workspaces.sh
```

Move Chrome to the source monitor of the `Right` workspace. Then run:

```bash
SDL_VIDEODRIVER=wayland \
  build/examples/pinned_viewport/RayNeoPinnedViewport --kde-two-workspaces
```

Keep your head centered during calibration. Looking right activates the live
`Right` workspace; returning near center activates `Center`. This is a
workspace switch, not a fake second physical monitor.

### 9. Optional advanced 3x3 workspace grid

The normal viewport above is a visual panning test. It does not create a real
second monitor. For live KDE workspaces, first create a 3x3 Plasma workspace
grid:

```bash
tools/setup-kde-workspace-grid.sh
```

On a normal fresh KDE session with one workspace, this inserts four workspaces
before your existing workspace and four after it. Your existing main workspace
therefore becomes workspace 5, in the geometric center. It then changes the
grid to three rows.

If you already ran an older version of this helper, it appended workspaces and
left your main workspace in the top-left. Move anything important out of the
old RayNeo workspaces, then repair that exact layout with:

```bash
tools/setup-kde-workspace-grid.sh --repair-old-grid
```

The repair option removes only workspaces whose names start with `RayNeo Grid`
and rebuilds the corrected layout. For any other existing KDE workspace
layout, the helper refuses to change it; configure that layout manually.

The nine positions used by the head tracker are:

```text
1 (upper-left)  2 (above)  3 (upper-right)
4 (left)       5 (center) 6 (right)
7 (lower-left) 8 (below)  9 (lower-right)
```

In KDE’s workspace list these are named `Upper Left`, `Above`, `Upper Right`,
`Left`, `Center`, `Right`, `Lower Left`, `Below`, and `Lower Right`. They are
workspaces, not separate RayNeo applications.

All eight surrounding workspaces are selectable by head pose. Put Chrome,
terminals, or other applications on the workspaces you want to use. Plasma
keeps each workspace live; these are not copied screenshots.

Head tracking changes the active KDE workspace; it does not automatically move
an existing window into that workspace. For example, to make Chrome appear on
the right position, use this simple test:

1. Stop `RayNeoPinnedViewport` with `Ctrl+C` in the terminal where it is
   running. This only stops our viewport; it does not stop KDE or Chrome.
2. Focus the Chrome window on the source monitor.
3. Press `Alt+F3` to open Chrome’s KDE window menu, then choose `More Actions`
   → `Move to Desktop` → `Right`.
4. Start the RayNeo viewport again. Looking right now activates `Right` and
   shows Chrome there.

Do not choose `Configure Virtual Desktops` for this. That panel only edits the
workspace grid; it is not the window-move menu. If Chrome is placed directly on
the physical RayNeo output, the fullscreen viewport covers it, so put Chrome
on the source-monitor output for this prototype.

Important: a physical monitor and a KDE virtual desktop are different things.
In the tested setup, the ordinary monitor is the source display and the RayNeo
is the physical output to its right. The fullscreen RayNeo viewport covers that
physical RayNeo output while it runs, so applications placed directly on the
RayNeo output are underneath the viewport. Put the Chrome/YouTube window on
the source-monitor side of the KDE workspace you want to visit; head movement then
switches the KDE workspace and the RayNeo viewport shows that workspace.

The setup script also enables the included KWin integration. It automatically
puts the `RayNeo pinned viewport` window on all desktops, so the viewport stays
visible while KWin changes the workspace. If the grid is already configured,
enable that part separately:

```bash
tools/enable-rayneo-kwin-sticky.sh
```

Start the workspace mode with:

```bash
SDL_VIDEODRIVER=wayland \
  build/examples/pinned_viewport/RayNeoPinnedViewport --kde-workspaces
```

Keep your head centered during the first-second gyro calibration. Looking
left, right, up, or down switches to the corresponding KDE workspace. Return
near the center pose to return to workspace 5. The program uses a small
hysteresis band so it does not flap between workspaces while your head is
nearly centered.

This mode currently uses the existing Spectacle capture path to refresh the
RayNeo image. The workspaces themselves are real and live, but the image path
is still a prototype and is slower than a native PipeWire stream. Mouse and
keyboard routing from the RayNeo window into the active source workspace is
also not implemented yet.

### 10. Optional: test the IMU first

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

For another compatible device, replace the two ID values with its
`RAYNEO_VID` and `RAYNEO_PID` values.

Then add yourself to `plugdev`, reload the rules, and replug the glasses:

```bash
sudo usermod -aG plugdev "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Log out and in once after adding yourself to the group.

### The coloured demo appears on the wrong monitor

Confirm that the command includes:

```bash
SDL_VIDEODRIVER=wayland
```

The program identifies outputs by a `RayNeo` or `SmartGlasses` name when
available. If the target output has another name, set it explicitly, for
example:

```bash
RAYNEO_DISPLAY=1 ./run-rayneo
```

`RAYNEO_DISPLAY` accepts either a display index or a substring of its name.

## What works

- RayNeo Air 4 Pro detection over USB (`1bbb:af50`), with configurable VID/PID
- 500 Hz IMU streaming through the existing libusb SDK transport
- Accelerometer, gyroscope, magnetometer, temperature, and device-info events
- Quaternion-based gyro orientation tracking
- Stable yaw, pitch, and roll after stationary calibration
- `R` recentering
- RayNeo as a normal, live KDE extended display beside the primary monitor
- A synthetic coloured head-tracking demo driven by yaw and pitch
- Live Samsung monitor and Chrome window capture through KDE PipeWire portals
- Chrome pointer/click forwarding with KWin stacking protection
- Gravity-assisted pitch and roll correction to prevent vertical drift

## Tested setup

The current working setup is:

```text
Debian 13
KDE Plasma / Wayland
AMD Renoir / amdgpu
RayNeo Air 4 Pro: 1bbb:af50
Source monitor: 2560x1440
RayNeo output: 2560x1440
```

The RayNeo is connected as a normal DisplayPort monitor. In the tested layout,
KDE places it directly to the right of the source monitor. Chrome is moved to
it as a normal application window; no RayNeo program needs to be running.

## Coloured demo behaviour

`rayneo --demo` renders a 3x3 coloured reference grid on the RayNeo output.
Yaw moves the grid horizontally and pitch moves it vertically. It is a hardware
and orientation test only; it deliberately replaces whatever is normally on
the RayNeo display until it exits.

Controls:

- `R` — recenter; hold the glasses still for one second
- `Esc` — exit

## Current limitations

- The live viewport is currently tuned for one Samsung-class source monitor and
  one Chrome window on KDE Plasma Wayland.
- KDE’s portal picker may show one or two dialogs depending on remembered
  permissions; it must return one monitor stream and one window stream.
- The live viewport is a focused two-panel experience, not a general spatial
  compositor or arbitrary multi-window desktop.
- Pitch and yaw sensitivity are currently tuned for the tested 2560x1440 source monitor / RayNeo setup.
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
- native PipeWire/KDE capture for smooth live workspace video
- pointer and keyboard routing through the Wayland RemoteDesktop/EIS path
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
