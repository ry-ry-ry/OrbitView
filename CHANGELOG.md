# Changelog

All notable changes to OrbitView are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **MAJOR** for changes that break existing `config.ini` files, saved views or `orbitview/*` command bindings.
- **MINOR** for new features that keep existing setups working.
- **PATCH** for bug fixes.

To release, add a section here, set `VERSION` to the same number and push to `main`.
The release workflow publishes that section as the release notes.

## [1.1.0] - 2026-11-17

### Added

- Cinematic camera on `F12` (command `orbitview/cinematic`). It cycles through
  exterior shots of different parts of the aircraft (nose, flight deck,
  engine, wing, wingtip, tail, belly, wide side, rear and front angles), each
  drifting slowly for about 7 seconds, with a short dip to black between
  shots. `A` / `D` skip to another shot; `F12` or `End` return to the view
  you were in.
- Flyby shots in the cinematic camera: when the aircraft is moving faster
  than about 50 kt, the camera now and then waits beside the flight path
  ahead and tracks the aircraft as it passes. `orbitview/cinematic_flyby`
  starts one straight away.
- Binoculars: hold the right mouse button and click the middle one to raise
  or lower them (command `orbitview/binoculars`). They start at 6x and the
  mouse wheel sets 2x to 50x, separately from the normal zoom. Looking around
  slows down to match, and outside views pan from where the camera is rather
  than orbiting. A compass tape along the bottom shows the magnetic heading
  of the view. In the cockpit they narrow X-Plane's field of view, which is
  put back when they're lowered.
- Cockpit zoom of OrbitView's own: hold the right button and scroll. It
  goes from 0.4x, well wider than X-Plane's zoom-out limit, to 3x in finer
  steps, and is saved with each cockpit view. The plain wheel is still
  X-Plane's, so it keeps turning knobs. It works by changing X-Plane's field
  of view, which is put back when you return to 1x or leave the cockpit; if
  X-Plane quits or crashes while it's changed, OrbitView restores it on the
  next start.
- `cinematic_shot_time`, `cinematic_fade`, `cinematic_flyby`,
  `cinematic_distance` and `binocular_zoom` in `config.ini`.

## [1.0.2] - 2026-11-14

### Fixed

- The right stick only nudged the camera a degree or two per push. 1.0.1
  relied on an X-Plane dataref that turned out to report the pad as missing
  even when it's connected. OrbitView now starts using the stick once it has
  been seen centred, and stops if every assigned axis drops to the reading
  X-Plane gives for a missing device.

## [1.0.1] - 2026-11-14

### Fixed

- The camera no longer spins up and to one side when the gamepad assigned to
  *View left/right* / *View up/down* isn't plugged in. OrbitView now reads the
  stick only while X-Plane reports the hardware as connected, and only after
  the stick has been seen centred.

## [1.0.0] - 2026-11-03

Initial release.

[1.1.0]: ../../compare/v1.0.2...v1.1.0
[1.0.2]: ../../compare/v1.0.1...v1.0.2
[1.0.1]: ../../compare/v1.0.0...v1.0.1
[1.0.0]: ../../releases/tag/v1.0.0
