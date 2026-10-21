# Privacy

This fork of OrcaSlicer sends one small, anonymous usage ping (a "census") and nothing else. This document says exactly what that is, why it exists, what it actually risks, and every way to turn it off.

This document covers that usage census, which is the only telemetry this fork adds. The upstream Bambu/cloud telemetry is disabled in this fork. It does not cover ordinary network features you choose to use, such as checking for updates, syncing presets, signing in, or talking to your printer, which connect only when you use them.

## What is collected

Each ping is a single HTTPS POST to `https://telemetry.namelessnanashi.dev/census` with a JSON body containing exactly two fields:

- `id`: the SHA-256 hash of a random UUID that was generated once, on this install, and stored in your config. The UUID is random. It is not derived from your hardware, account, name, or network. Only its hash is ever sent, and the raw UUID never leaves your machine.
- `project`: the fixed string `OrcaSlicer-Nanashi`, which identifies this fork, not you.

That is the entire payload. No files, models, G-code, settings, profiles, account details, OS version, or hardware information are sent.

The ping is sent once at startup and then about every two hours while the app keeps running, so that a session spanning more than one day is counted on each day. The server counts each install once per UTC day and keeps nothing else. The resulting count is shown on a public dashboard: https://telemetry.namelessnanashi.dev/?project=OrcaSlicer-Nanashi .

The server's code is public, so you can read exactly what it does: https://github.com/NanashiTheNameless/NamelessTelemetry . It is published for transparency. It is not released under an open-source license.

Nothing is sent until you finish the setup wizard, and nothing is sent at all if telemetry is off (see below).

## Why it is collected

To get an honest count of how many people actually use this fork, and roughly how often. That is the kind of number a volunteer project uses to justify the effort. There is no advertising, profiling, sale, or sharing of data.

## What it actually risks

Being honest about the residual risk, even though the payload is minimal:

- The `id` is a stable pseudonym. It is the same hash on every ping from this install, so the server can tell that "install X was active on these days." It cannot, from the payload alone, tell who install X is.
- The network request exposes your IP address. Like any web request, the POST reveals your IP to the server, along with timing. The app sends nothing that ties your IP to your identity, but a server operator who logged IP addresses could, in principle, associate an install's IP addresses and active days. You are trusting the deployed server to keep only the per-day count it says it keeps.
- It reveals activity timing. The ping schedule shows that OrcaSlicer was running at certain times, which the server reduces to one count per day.

If any of that is more than you want to share, turn it off. It is designed to be fully optional.

## How to disable it

Any one of these stops all pings once applied. Preferences and Stealth Mode take effect immediately; the installer choice is applied the next time OrcaSlicer starts.

### Any operating system

- Setup wizard: uncheck "Send anonymous usage statistics" on first run.
- Preferences: on the Online tab, turn off "Send anonymous usage statistics" at any time. Your choice is saved and can be turned back on.
- Stealth Mode: turning on Stealth Mode also turns telemetry off.
- Command line: launch with `--turn-telemetry-off` once. It turns telemetry off, saves that choice, and keeps starting normally. Useful for scripted or unattended installs.
- Windows installer: on the installer's final page, tick "Turn off anonymous usage statistics (telemetry)". The installer leaves a one-time marker next to OrcaSlicer; on the next launch, OrcaSlicer turns telemetry off in your config and removes the marker.
- Environment variable (forces it off without changing your saved setting): set `DO_NOT_TRACK`, `TELEMETRY_OPTOUT`, or `ORCA_TELEMETRY_OPTOUT` to `1`, or set `TELEMETRY` to `0`.

### Windows only

- System policy: set a nonzero `TelemetryOptOut` DWORD under `HKLM\SOFTWARE\Policies\OrcaSlicer` or `HKCU\SOFTWARE\Policies\OrcaSlicer`. This forces telemetry off for the whole machine or for the user, and it cannot be overridden from the app's settings.
