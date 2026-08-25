# RayNeo Air 4 Pro Linux 3DoF

Native Linux experimentation for using compatible **RayNeo glasses** as a low-latency 3DoF head-tracked display. The project is based on the upstream [RayNeo SDK](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR), with Air 4 Pro support and a lightweight Wayland desktop viewport added locally.

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
  libgl1-mesa-dev
```

Desktop capture is optional and is only used by the experimental `--live`
mode.

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

### 5. Start it after a reboot

Use this one command from the project directory:

```bash
./run-rayneo
```

If you want the shorter `rayneo` command available after opening a new
terminal, install the project-local launcher once:

```bash
./tools/install-rayneo-command.sh
rayneo
```

It checks that the glasses are connected, checks/builds the viewer, pins the
RayNeo window to the glasses output and starts the head-tracking-only demo.
It does not capture the desktop or change KDE virtual desktops.

Use `rayneo` for the immediate colored-panel test. Use `rayneo --live` only
when you explicitly want the slower experimental capture of the Samsung/source
monitor.

The normal layout is deliberately simple:

```text
Samsung/source monitor = your normal desktop
RayNeo = the head-tracked viewport
```

The head-tracking demo renders colored reference panels directly. There is no
5120x1440 desktop capture, no duplicated Samsung image, and no Center/Right
workspace switching.
Keep the glasses still for the first second while it says `Gyro calibrated`.

Press `R` to recenter, `C` to refresh the capture immediately, or `Esc` to
quit. `Ctrl+C` in the terminal also exits.

### 6. Optional: start the head-tracking demo manually

Use the native Wayland backend:

```bash
SDL_VIDEODRIVER=wayland \
  build/examples/pinned_viewport/RayNeoPinnedViewport --headtracking
```

This mode renders synthetic colored panels at the display rate. It is the
recommended first test because it isolates USB IMU, orientation, recentering,
and RayNeo output placement from desktop capture.

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

The captured image deliberately excludes the mouse pointer. Wayland already draws the real compositor pointer, and including a second captured pointer would show two cursors. Pointer routing into the synthetic canvas is not implemented yet.

Do not use `SDL_VIDEODRIVER=x11` in a Wayland session. Xwayland may put the
viewport on the source monitor instead of the RayNeo output.

### 7. Manual workspace setup and troubleshooting

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

### 8. Optional advanced 3x3 workspace grid

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

### 9. Optional: test the IMU first

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

### The viewport appears on the wrong monitor

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

### The desktop image is old or updates slowly

The prototype refreshes the selected source-monitor region with repeated KDE
Spectacle captures. This is live, but it is not yet a low-latency PipeWire
video stream. Press `C` for an immediate refresh. The terminal's `live=`
counter shows how many background frames have been captured.

### The image is dark above or below the screen

That is intentional when looking far up or down: the captured physical screen is finite, so it can move completely outside the virtual viewport.

## What works

- RayNeo Air 4 Pro detection over USB (`1bbb:af50`), with configurable VID/PID
- 500 Hz IMU streaming through the existing libusb SDK transport
- Accelerometer, gyroscope, magnetometer, temperature, and device-info events
- Quaternion-based gyro orientation tracking
- Stable yaw, pitch, and roll after stationary calibration
- `R` recentering
- Native Wayland output selection for the RayNeo display
- A synthetic pinned viewport driven by head yaw and pitch
- Live source-monitor refresh through KDE Spectacle
- An additional visual monitor region to the left of the captured source display
- Experimental switching between real KDE workspaces using head pose

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

The RayNeo is connected as a normal DisplayPort monitor. The viewport selects the native Wayland output named `Technical Concepts Ltd SmartGlasses`. X11/Xwayland is not recommended for the viewport because the compositor may place fullscreen windows on the primary monitor.

## Viewport behavior

The default head-tracking demo renders a 3x3 colored reference grid. The
experimental pinned-source mode captures the complete desktop, extracts the
first non-RayNeo output as the source monitor, and refreshes that region in the
background. Its canvas is:

```text
[ duplicated source region ][ captured source ][ captured right-hand region ]
```

The captured screen is centered at the neutral head pose. Yaw moves horizontally through this canvas. Pitch moves vertically through a taller virtual space; a strong look up or down moves the finite captured screen out of view.

The extra monitor is currently a visual duplicate inside the OpenGL canvas. It is not a real KDE output, so applications cannot be launched or interacted with on that region yet.

Controls:

- `R` — recenter; hold the glasses still for one second
- `C` — refresh the desktop capture in pinned-source mode
- `Esc` — exit

## Current limitations

- Experimental live capture refreshes through repeated Spectacle screenshots, not a continuous PipeWire/KDE screencopy stream. It is intentionally not part of the default head-tracking command.
- The extra visual monitor currently duplicates the captured source region rather than providing an independent workspace.
- The viewport does not yet route keyboard or mouse input to a synthetic monitor region.
- The extra visual region is not a real Wayland/KDE output and cannot host application windows.
- KDE workspace mode changes the live workspace, but still refreshes the RayNeo texture through Spectacle screenshots.
- KDE workspace mode does not yet forward RayNeo mouse clicks or keyboard input to the active workspace.
- Orientation is gyro-based and can drift during long sessions.
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
