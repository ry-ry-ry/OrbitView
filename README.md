# OrbitView

A preset-based camera for **X-Plane 12** on Windows, in the style of ChasePlane. Each aircraft gets its own list of cockpit and exterior views. You flick between them with one key, adjust them with the mouse, keyboard or gamepad, and save your own.

- **Views per aircraft.** Factory cockpit and exterior views are built from each aircraft's eye point and size. Add, rename, delete and save your own.
- **Smooth everywhere.** Glides between views, with eased mouse, keyboard and stick input.
- **Full precision in the cockpit.** Cockpit views drive the pilot's head, so the panel doesn't shimmer when you're far from X-Plane's local origin.
- **Cinematic camera.** Press `F12` for a hands-off show: slow drifting shots of the nose, flight deck, engines, wings, tail and belly, about 7 seconds each with a dip to black between them, and now and then a flyby where the camera waits beside the flight path and the aircraft roars past.
- **Binoculars.** Hold the right button and click the middle one to magnify whatever you're looking at, up to 50x, with a heading tape so you know where it is.
- **Camera effects (optional).** G-force head sway, turbulence jolts, runway rumble and a touchdown bump scaled by your sink rate.
- **Scales with X-Plane's UI** and stays usable on 4K screens.

## Install

1. Download `OrbitView-vX.Y.Z.zip` from [Releases](../../releases/latest).
2. Extract it into `X-Plane 12\Resources\plugins\`, so you end up with `plugins\OrbitView\64\win.xpl`.
3. Start X-Plane. The menu is under **Plugins > OrbitView**.

## Controls

| Input | Action |
|---|---|
| `A` / `D` | Previous / next view |
| `End` | Switch between cockpit and exterior |
| Right-drag | Look around (cockpit) / orbit (exterior) |
| Gamepad right stick | Look around / orbit (uses the axes set to *View left/right* and *View up/down*) |
| Mouse wheel, `=` / `-` | Zoom. In the cockpit this is X-Plane's own zoom, so pointing at a knob still turns it |
| Right-drag + wheel (cockpit) | OrbitView's own cockpit zoom: finer steps, from 0.4x (much wider than X-Plane's zoom-out) to 3x. It's kept per view, so **Save to selected** remembers it |
| `Shift+W` / `S` | Move forward / back (exterior: closer / farther) |
| `Shift+A` / `D` | Move left / right |
| `Shift+E` / `Q` | Move up / down |
| `Shift+Space` | Reset the current view |
| `Ctrl+Shift+Space` | Add the current camera as a new view |
| Hold right button + middle click | Raise / lower binoculars. The wheel sets their magnification (2x to 50x, separate from the normal zoom), right-drag looks around, and a compass along the bottom shows the magnetic heading you're looking at |
| `F12` | Cinematic camera on / off. While it runs, `A` / `D` skip to another shot and `End` also leaves it |

Every action is also an `orbitview/*` command, so you can bind it to anything in X-Plane's keyboard or joystick settings. If any of these keys are already in use in your keymap, rebind them there, or set `keyboard = 0` in `config.ini` and use the commands instead.

## Views and preferences

**Plugins > OrbitView > Views window...** lists the views in the current category:

- Click a view to jump to it.
- Move the camera where you want it, then **Add as new view** or **Save to selected**.
- **Rename**, **Delete** and **Reset** do what they say.

Up to 12 views per category are saved per aircraft in `OrbitView\presets\`. **Restore factory views** puts back the original set.

The **Preferences** tab turns camera effects on and off, sets their strength, and controls whether they also apply to exterior views. Effects are off by default. Everything else (mouse and stick sensitivity, smoothing, move speed and so on) is in `config.ini`, documented inline. **Plugins > OrbitView > Reload config.ini** applies your changes without restarting.

## How it works

- **Cockpit views** write X-Plane's pilot-head datarefs, which are relative to the aircraft, so they keep full precision. While OrbitView is running, X-Plane's own G-loaded internal camera is switched off; your setting is restored afterwards.
- **Exterior views** take the camera with `XPLMControlCamera`. They follow the aircraft's heading smoothly and stay above the terrain.
- **Input:**
  - Keys come through a key sniffer that runs after windows, so typing in text fields is unaffected.
  - Right-drag and the wheel come from X-Plane's main window, so right-clicks don't start X-Plane's own pan.
- **X-Plane's own views:** if you pick a view with X-Plane's keys, OrbitView follows into that category. It steps aside in VR, and when another plugin already controls the camera.

## Building

You need the Visual Studio 2022 C++ build tools. The X-Plane SDK headers and libraries are included in `SDK\`.

```bash
build.bat           # builds build\OrbitView\64\win.xpl
build.bat install   # also copies it into X-Plane
```

`install` finds X-Plane from its own install record. Set the `XPLANE` environment variable to use a different folder.

## License

[MIT](LICENSE). The X-Plane SDK in `SDK\` is under its own license (`SDK\license.txt`).
