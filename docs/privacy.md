# Privacy and telemetry

Kettle Logic has no account and no sign-up, and your recipes stay on your computer. The app can send a small amount of
anonymous data to help find and fix problems. This page says exactly what, and how to turn it off.

## The three levels

You choose a level the first time you open the app, and can change it any time in **Settings** under **Privacy**.
**Limited** is the default.

| Level | What it sends |
|---|---|
| **Off** | Nothing. No data is sent and no crash report is kept for later. |
| **Limited** | That the app was opened, errors and crashes, and the result of an update check. |
| **Enhanced** | Everything in Limited, and which features are used. |

A user who upgraded from a version that never asked sees a notice once, with the same choice, before anything is sent.

## What every report contains

Every report carries these values, and no others:

| Value | Example |
|---|---|
| Platform | `macos`, `windows`, or `linux` |
| App version | `0.1.0` |
| Engine version | `0.1.0` |
| Operating system version, the major version only | `11`, `26`, `24` |
| Language, the language only | `en` |
| A random ID for this install | `3f2b…` (a UUID) |

The random ID is made when the app first sends something. It is not made from your name, your computer, or anything
else about you. **Settings > Privacy > Reset** replaces it, so later reports are not linked to earlier ones.

## What each event contains

| Event | Level | Extra values |
|---|---|---|
| `app_open` | Limited | None. |
| `app_error` | Limited | An error domain and code (fixed words or numbers), and a short tag for the part of the app, such as `ui`. |
| `app_crash` | Limited | The same, from the crash the last run left. It is sent the next time the app starts. |
| `update_check_result` | Limited | `available`, `up_to_date`, or `failed`. |
| `feature_used`, `recipe_action`, `export_recipe`, `import_recipe` | Enhanced | A fixed name for the feature or file format, and counts such as the number of mash steps. |

## What is never sent

- Your recipes, or anything in them: names, ingredients, notes, journal entries, or numbers you typed.
- File names, folder names, or paths. A value that looks like a path or an address is dropped before sending.
- Your name, email address, or account. There are none.
- Your IP address is seen by Google's servers as for any web request, as the network requires. Kettle Logic does not
  store it, does not send location, and marks every report as not for ad personalization.
- The text of an error message. Only a fixed code is sent.

## How it is sent

Reports go to Google Analytics 4 over HTTPS, using its Measurement Protocol. They are sent in the background, so a slow
or missing network never holds up the app, and a report that cannot be sent is dropped.

A crash is not sent while it happens. The app leaves a small file in its data folder (`crash-report.txt`) that holds only
the crash's type and a tag for the part of the app. On the next start, if the level is not Off, that file is sent and
deleted. At **Off** the file is deleted without being sent, and turning the level to Off deletes any file waiting.

## Building without it

A build sends nothing unless it is given a Google Analytics measurement ID and API secret:

| App | How |
|---|---|
| Windows, Linux | `cmake -S apps/windows ... -DKETTLELOGIC_GA_MEASUREMENT_ID=G-XXXXXXXXXX -DKETTLELOGIC_GA_API_SECRET=...` (add `-DKETTLELOGIC_GA_DEBUG=ON` to validate reports instead of recording them) |
| macOS | `KL_GA_MEASUREMENT_ID=G-XXXXXXXXXX KL_GA_API_SECRET=... sh apps/macos/make-app.sh` (add `KL_GA_DEBUG=YES` to validate), or set the same names as build settings in the Xcode project |

The API secret is part of the shipped app, as it is for any program that reports to Google Analytics from a user's
computer. It can be used only to send reports, not to read them.

For the Linux Flatpak and the Windows installer, pass the same CMake options and Xcode build settings when building the release.
