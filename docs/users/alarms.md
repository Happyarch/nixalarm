# Schedule an alarm that rings at a local time

Goal: ring an alarm at a chosen local time, then snooze or dismiss it.

Prerequisites: a built `nixalarm` binary (see [build.md](../contributing/build.md)), a working display for the clock window, and working audio output. From a plain build tree the command is `./build/nixalarm`; after install or packaging it is `nixalarm`. The examples below use `nixalarm`; prefix with `./build/` when running from the build tree.

The config file lives at `$XDG_CONFIG_HOME/nixalarm/config.toml`, or `$HOME/.config/nixalarm/config.toml` when `XDG_CONFIG_HOME` is unset. A normal launch creates it with defaults on first run. See [nixalarm.5](../man/nixalarm.5) for the authoritative key list.

## Steps

1. Schedule a one-off alarm by passing a time on the command line:

   ```sh
   nixalarm 07:30
   nixalarm "7:30 PM"
   ```

   Expected: the terminal reports the armed time, such as `nixalarm: armed for ...`, and a clock window opens. Times without `AM` or `PM` use 24-hour local time; times with `AM` or `PM` use 12-hour local time. A time that already passed today rings tomorrow. If the time is malformed, startup stops with `nixalarm: invalid alarm time, expected HH:MM or HH:MM AM/PM`; fix the argument and retry.

2. When the alarm rings, tap Space to snooze or hold Space to dismiss. The defaults snooze for 10 minutes and dismiss after a 10-second hold.

   Expected: a snooze prints the new ring time (`nixalarm: snoozed until ...`); a dismissal prints `nixalarm: alarm dismissed`. A progress bar tracks the hold on the seven-segment, nixie, and analog faces; the dial faces draw none and signal ringing by sound alone.

3. Save regular alarms so launcher or desktop starts need no argument. Put the list at the top level of the config file, before any section headers:

   ```toml
   alarms = ["07:30", "5:30 PM"]
   ```

   Expected: running `nixalarm` with no time argument arms every listed time in order. Empty strings are skipped. An invalid entry stops startup with `nixalarm: config alarms must be HH:MM, HH:MM AM/PM, or empty strings`; fix the entry and retry. Each listed time rings once; nothing repeats daily.

4. Run clock-only by leaving the list empty:

   ```toml
   alarms = []
   ```

   Expected: `nixalarm` reports `nixalarm: no alarms configured; running as clock only` and never starts audio.

The alarm only rings while the program runs: there is no background service, so keep the window open (or minimized) until the alarm time. If the selected sound is wrong, the source failed and the fallback played instead; see [sources.md](sources.md) to choose and test a source. If the name is unknown, startup stops with `nixalarm: unknown source: ...`; check it with `nixalarm --list-sources`.

## Window controls

- `F11` or `f`: toggle fullscreen.
- `q` or Escape: quit.

## Next

- [Choose the alarm sound](sources.md).
- [Change the clock appearance](appearance.md).
- [nixalarm.1](../man/nixalarm.1) for the authoritative command line, time format, controls, and exit status.
- [nixalarm.5](../man/nixalarm.5) for the authoritative configuration keys.
