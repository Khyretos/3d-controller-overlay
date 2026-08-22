# 3D Controller Overlay — Instructions

This page walks through every feature in detail. Screenshots below are placeholders (`images/placeholder-*.png`) — drop your own into an `images/` folder next to this file and the filenames will line up.

## Table of Contents
1. [First Launch](#first-launch)
2. [Opening a Controller Window](#opening-a-controller-window)
3. [Mapping Inputs](#mapping-inputs)
4. [Gyro Support](#gyro-support)
5. [Touchpads](#touchpads)
6. [Highlighting & Press Feedback](#highlighting--press-feedback)
7. [Importing a Custom Model](#importing-a-custom-model)
8. [Lighting](#lighting)
9. [Window & Camera Settings](#window--camera-settings)
10. [The Log Window](#the-log-window)
11. [Data Directory & Backups](#data-directory--backups)
12. [Troubleshooting](#troubleshooting)

---

## First Launch

![Placeholder: first-launch settings window](images/placeholder-first-launch.png)

On first launch, the app extracts its built-in model library to your data directory — this takes a few seconds and only happens once. See the README for exactly where that directory lives on your OS.

If you're on macOS, also see the README's Accessibility section — without granting Accessibility permissions, keyboard/mouse bindings won't register (gamepad/joystick input works regardless).

---

## Opening a Controller Window

![Placeholder: model picker](images/placeholder-model-picker.png)

From the Settings window, pick a model from your library and a connected controller to drive it. Each model corresponds to a specific controller layout (Xbox pad, DualSense, Steam Controller, etc.) with its meshes already assigned to the right buttons/sticks/triggers.

Each controller window is independent — you can open multiple windows for multiple controllers, or the same controller in different views, simultaneously.

---

## Mapping Inputs

![Placeholder: mapping panel](images/placeholder-mapping-panel.png)

Every visible part of a model (a button, stick, trigger, touchpad, etc.) has an **input binding** — what real-world input drives it. Select a mesh in the Mapping panel and either:

- **Capture mode:** click "Listen", then press the physical button/key/click you want bound. The app detects it automatically.
- **Manual selection:** pick from a dropdown of every known input for the selected type (gamepad, joystick, keyboard, or mouse).

Supported binding types:

| Type | What it covers |
|---|---|
| **Gamepad** | SDL's standardized layout — buttons, sticks (combined X/Y), triggers, D-pad. Works with any SDL-recognized controller (see the README's `gamecontrollerdb.txt` section if yours isn't recognized). |
| **Joystick** | Raw, unmapped button/axis/hat indices — useful for controllers SDL doesn't recognize as a standard gamepad. |
| **Keyboard** | Any key on your physical keyboard, captured globally (works even when the overlay isn't focused). |
| **Mouse** | Buttons (including extra side buttons), movement, and scroll. |

Each binding can be **inverted** (useful for axes that read backwards on some hardware) via the Invert checkbox next to it.

---

## Gyro Support

![Placeholder: gyro settings](images/placeholder-gyro.png)

If your controller reports a gyroscope, enable it per-window in the Gyro section. Settings:

- **Sensitivity** — how much rotation the gyro data produces.
- **Correction** — a slow drift-correction pull back toward level; higher values fight drift harder but feel "stickier".
- **Reset combo** — pick two buttons that, held together, snap the gyro's orientation back to neutral (handy after picking the controller up in a rotated position).
- **Debug logging** — logs raw gyro Euler angles periodically; useful when troubleshooting a controller that behaves oddly (pair this with the [Log Window](#the-log-window)).

---

## Touchpads

![Placeholder: touchpad config](images/placeholder-touchpad.png)

Controllers with capacitive touchpads (Steam Controller, DualSense/DualShock) support up to 2 simultaneous fingers per pad, up to 4 pads per window. Configure:

- **Touch width/height** — the physical touch-sensitive area, mapped onto the mesh.
- **Offset/rotation** — fine-tune where the touch indicator sits and how it's oriented relative to the pad mesh.
- Individual **touchpoint** meshes can be bound to a specific finger's X/Y or combined X+Y, and are automatically anchored to their parent touchpad mesh.

Touchpoints that go idle (no finger contact) for more than 5 seconds automatically re-center and hide, so a lifted finger doesn't leave a stray indicator behind.

---

## Highlighting & Press Feedback

![Placeholder: highlight color picker](images/placeholder-highlight.png)

By default, pressing a mapped button glows the mesh in a global highlight color (configurable, including alpha). Individual meshes can override this with a **custom highlight color**, and axis-driven parts (sticks, triggers) support **dual highlighting** — a different color for the positive vs. negative direction, with an adjustable deadzone so small drift doesn't trigger a glow.

---

## Importing a Custom Model

![Placeholder: import preview window](images/placeholder-import.png)

You can bring in your own 3D model (common formats via Assimp) instead of using a built-in one:

1. **Settings → Import Model**, pick your file.
2. A preview window opens listing every mesh found in the file.
3. For each mesh, assign it to a controller part (or leave unassigned to hide it), and optionally set a parent part for correct pivoting (e.g. a touch finger indicator parented to its touchpad).
4. Save — the app converts the imported meshes into a usable model and writes it to your model library.

---

## Lighting

![Placeholder: lighting panel](images/placeholder-lighting.png)

Each window supports multiple **directional**, **point**, and **spot** lights, each with independently adjustable ambient/diffuse/specular strength and color. Point and spot lights also have falloff (constant/linear/quadratic) and can be hidden without deleting them.

---

## Window & Camera Settings

![Placeholder: window settings](images/placeholder-window-settings.png)

Per-window options include:
- **Always on top**, **borderless**, **drag to move**, **scroll to resize** — useful for a clean, unobtrusive overlay over a game.
- **Camera**: distance, yaw/pitch/roll, and a separate **freelook** mode (mouse-look, WASD-style movement) for inspecting the model from any angle.
- **Swap interval** (V-Sync: off / on / adaptive) and background color/opacity.

---

## The Log Window

![Placeholder: log window](images/placeholder-log-window.png)

**Settings → Open Log Window** shows a live, scrolling view of the application log, color-coded by severity, right inside the app. This is especially useful on macOS and Linux, where — unlike Windows — no console is attached unless you launch the app from a terminal, so without this window you'd otherwise see no log output at all during normal use.

The same log is also written to disk in your [data directory](#data-directory--backups) under `logs/`, rotated at 5 MB with 3 files kept, so history survives even after closing the window.

---

## Data Directory & Backups

Everything you configure — bindings, imported models, tab layouts, the controller mapping database — lives in your per-OS data directory (see the README for exact paths). Back that folder up if you want to preserve your setup across a reinstall or move it to another machine.

---

## Troubleshooting

**Keyboard/mouse bindings don't do anything (macOS):** grant Accessibility permissions — see the README.

**My controller shows up but buttons are unlabeled/numbered:** it isn't recognized by SDL's gamepad database. See the README's section on `gamecontrollerdb.txt`, or map it manually using raw Joystick bindings.

**A gyro-equipped controller crashes or behaves oddly on Windows:** please open an issue with the controller model and, if you have it, the contents of the [Log Window](#the-log-window) or the log file from your data directory — this is an active area of hardening.

**First launch feels slow:** expected — the built-in model library is being extracted one time. Subsequent launches are fast.
