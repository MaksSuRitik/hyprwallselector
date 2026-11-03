# Release status

Version 0.3.0 is an early development release. Automated GUI tests run on an
isolated virtual display. Real-session verification targets Hyprland and DMS;
Caelestia/MiDnight renderer handover is covered by automated protocol/configuration
tests and real-session image/video verification. Optional scene rendering limits
are documented in README.md.

The AUR recipe is supplied separately. The GitHub repository and an AUR listing
are different deliverables: publishing sources does not submit the AUR package.

Native desktop preview additionally uses the shipped Quickshell companion component. Preview transaction tests cover coalescing, close/confirm races, stale requests and owner-death rollback; the QML bridge test runs when Quickshell and Python are available.
