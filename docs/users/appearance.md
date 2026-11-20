# Change the clock appearance

Goal: pick a clock theme and tune it for the room and location.

Prerequisites: a built `nixalarm` binary (see [build.md](../contributing/build.md)) and a working display for the clock window. The dial themes additionally need OpenGL 3.3.

The config file lives at `$XDG_CONFIG_HOME/nixalarm/config.toml`, or `$HOME/.config/nixalarm/config.toml` when `XDG_CONFIG_HOME` is unset. A normal launch creates it with defaults on first run. Style keys sit in the `[style]` section; observer coordinates are top-level keys before any section headers. See [nixalarm.5](../man/nixalarm.5) for the authoritative key list.

## Steps

1. Set the theme in the `[style]` section, before any color overrides:

   ```toml
   [style]
   theme = "sinnoh_green"
   show_seconds = false
   ```

   Expected: the next launch draws the new face. A theme selects both the rendering style and its colors, so set it first and put overrides after it. An unknown name prints `nixalarm: unknown theme: ...` and keeps the last valid theme. Attribution for bundled artwork lives in [assets/ATTRIBUTION.md](../../assets/ATTRIBUTION.md).

2. Adjust the seven-segment faces. The `terminal_glow` and `sinnoh_green` themes draw seven-segment digits and accept `background`, `segment_on`, `segment_off`, and `glow` overrides.

3. Adjust the analog face. The `analog` theme draws a vector dial with hour, minute, and sweeping seconds hands. It uses `background` for the window fill and `segment_on` for the pending-alarm marker color; `segment_off` and `glow` have no effect on it. Set `show_seconds = false` to hide only the seconds hand; the theme enables it by default. Set `roman_numerals = true` for the Roman-numeral face instead of the default 12-hour Arabic dial. The hands follow the same time math as the source vector artwork, driven natively. A small alarm marker appears while an alarm or snooze is pending and pulses red while ringing.

4. Adjust the nixie face. The `nixie` theme draws photoreal tube digits and ignores all four color overrides. It shows the 4-tube (HH:MM) layout by default; set `show_seconds = true` for the 6-tube (HH:MM:SS) face. If the 4-tube assets are not staged, the face falls back to the 6-tube layout with the extra tubes unlit.

5. Point the dial faces at the real location. The `sundial`, `moondial`, and `auto_dial` themes trace the shadow the gnomon would cast under the real sun or moon right now, so they need the observer position. Set signed degrees at the top level of the config, before any section headers, with positive north and positive east:

   ```toml
   latitude = 0.0
   longitude = 0.0
   ```

   Expected: the plate tilt and hour-line spacing are optimized for the latitude at startup, and the camera stays pinned to the plate so framing matches anywhere. The shipped default is the 0,0 point; replace it with the actual location or the dial is built and read for the wrong place.

6. Choose what the dial reads. Uncorrected, the two dials follow different bodies: the sundial reads apparent solar time, the uncorrected time of a garden dial, while the moondial reads the equivalent lunar time, offset by half a turn because its hour lines are laid out for a light that culminates at midnight. Either uncorrected reading can sit an hour or more off a watch depending on position in the time zone, the equation of time, and daylight saving. Set `dial_time` in `[style]` to correct it: `mean` aligns the dial with local mean solar time, while `clock` aligns it with civil time, applying the zone offset and daylight saving from the system so the dial agrees with a wall clock. The correction shifts the rendered light by the same amount, leaving shadow lengths seasonal, so in corrected modes the rendered sun also rises and sets at corrected times.

7. Read the dial by hour bands. The engraved lines are the boundaries between hours, drawn at the half hours, and each hour numeral sits inside the band its two boundaries enclose. Every boundary is a ray from the gnomon foot run out to the edge of the disc, because each hour plane contains the style and the shadow only falls on the side away from the light. The numerals are engraved as relief rather than painted, so lettering fills with shadow under a low sun, and each numeral is scaled to the room its own band leaves it. The boundaries are laid out for the latitude summer solstice, the longest day, so the dial carries every hour its light can ever be up for.

8. Prefer `auto_dial` for a clock that stays useful around the clock. A dial only reads while its own light clears the plate: the `sundial` face needs the sun above the horizon, and either dial stops reading when the gnomon shadow slides off the plate edge, which happens before the body sets. The pinned `moondial` face follows the moon alone, so it can show moonlight during daylight hours while the moon is up rather than being limited to nights. `auto_dial` shows whichever dial can currently be read and dissolves from one to the other at the changeover. The changeover follows readability, not sunset, and the moon side additionally needs the moon more than roughly a quarter lit before it counts as readable. When neither dial reads, the face holds the current dial instead of switching to an equally unreadable one. The two plates differ in tilt and hour lines, so the transition is a crossfade. None of the dial faces draws a ringing indicator; a ringing alarm is signaled by sound alone.

9. Tune the window and display. Size, fullscreen, always-on-top, and the ringing flash rate come from the window settings; the 12 or 24-hour digit display comes from `use_24_hour`. A dial that disagrees with a watch usually means `latitude`, `longitude`, or `dial_time` needs attention per steps 5 and 6. Only `background` still applies around the three dial faces.

## Next

- [Schedule an alarm](alarms.md) under the new face.
- [Choose the alarm sound](sources.md).
- [nixalarm.1](../man/nixalarm.1) for the authoritative command line and controls.
- [nixalarm.5](../man/nixalarm.5) for the authoritative style keys and theme behavior.
