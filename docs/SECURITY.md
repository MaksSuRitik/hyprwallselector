# Shortcut service

The GUI and all media decoding run as the logged-in user. A separate root service
opens keyboard evdev devices read-only. It recognizes only registered chords and
sends a directed D-Bus activation signal; raw input is not logged or exposed.

Registration checks D-Bus peer credentials and requires an active local graphical
logind session. Activation checks that the seat/session is active and unlocked.
The service never executes commands received from clients, never uses EVIOCGRAB,
and never creates a virtual keyboard. Devices are closed when no clients remain.

This passive approach coexists with GPU Screen Recorder's no-grab mode, but the
shortcut can also reach the focused application. Another application that
exclusively grabs the physical keyboard can prevent shortcut delivery. The chosen
lock screen must publish LockedHint to logind. These are deliberate limitations,
not reasons to grant the GUI root access or add the user to the input group.

The packaged systemd service has an empty capability bounding set, read-only
system/home access, no new privileges and only read access to input devices.
Users who do not want an input observer can mask hyprwallselector-hotkeys.service
and use `hyprwallselector --toggle` from their compositor's own shortcut instead.
