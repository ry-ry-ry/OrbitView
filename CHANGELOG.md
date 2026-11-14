# Changelog

All notable changes to OrbitView are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **MAJOR** for changes that break existing `config.ini` files, saved views or `orbitview/*` command bindings.
- **MINOR** for new features that keep existing setups working.
- **PATCH** for bug fixes.

To release, add a section here, set `VERSION` to the same number and push to `main`.
The release workflow publishes that section as the release notes.

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

[1.0.2]: ../../compare/v1.0.1...v1.0.2
[1.0.1]: ../../compare/v1.0.0...v1.0.1
[1.0.0]: ../../releases/tag/v1.0.0
