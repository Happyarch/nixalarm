# Choose the alarm sound

Goal: play a chosen audio source for the alarm and keep a fallback for failures.

Prerequisites: a built `nixalarm` binary (see [build.md](../contributing/build.md)), a working display for the clock window, and working audio output. From a plain build tree the command is `./build/nixalarm`; after install or packaging it is `nixalarm`. The examples below use `nixalarm`. SDR sources additionally need `rtl_fm` on `PATH` and connected RTL-SDR hardware.

The config file lives at `$XDG_CONFIG_HOME/nixalarm/config.toml`, or `$HOME/.config/nixalarm/config.toml` when `XDG_CONFIG_HOME` is unset. A normal launch creates it with defaults on first run; the gain wizard never creates it. See [nixalarm.5](../man/nixalarm.5) for the authoritative source definitions.

## Steps

1. Do nothing to keep the default. The built-in `generated` source is a procedural alarm tone that needs no files, network, or hardware. A fresh config stays on it, with every other example commented out, so a new install contacts nothing on its own.

2. List everything available:

   ```sh
   nixalarm --list-sources
   ```

   Expected: each line names one source and its kind. Built-ins include `generated` plus one RTL-SDR preset per weather-band channel from 162.400 to 162.550 MHz in 25 kHz steps. There are no presets for particular transmitters or stations.

3. Test a source immediately, without scheduling an alarm:

   ```sh
   nixalarm --test-source generated
   ```

   Expected: playback starts at once. Tap Space or hold Space to stop the test and exit. Testing never changes the saved alarm source. If the source produces no audio, the program reports the cause (missing file, unreachable address, missing hardware) and the fallback plays instead. An unusable SDR gain never reaches that path: it exits with status 2 before playback starts.

4. Add a local file source pointing at an audio file you own:

   ```toml
   [sources.local_song]
   type = "file"
   path = "/home/user/Music/alarm.flac"
   ```

   Expected: `--list-sources` shows the new name, and `--test-source local_song` plays it. Any format the local FFmpeg build decodes is accepted. Local files loop by seeking back to the start; streams play until they end.

5. Add an internet stream source. Write the section first, then fill in the address of a stream you already use; the section is incomplete until `url` holds a real address:

   ```toml
   [sources.my_stream]
   type = "internet"
   ```

   Expected: after setting `url`, `--test-source my_stream` plays the stream. Treat internet radio mirrors as convenience audio, not emergency alerting.

6. Add a MIDI source pointing at a MIDI file you own:

   ```toml
   [sources.midi_alarm]
   type = "midi"
   path = "/home/user/Music/alarm.mid"
   soundfont = "/usr/share/soundfonts/default.sf2"
   ```

   Expected: `--test-source midi_alarm` renders the file through FluidSynth. Set `soundfont` when the system provides no common default SoundFont path; otherwise auto-detection applies.

7. Select a source for one alarm run, using a name from `--list-sources` or one of the sections added above:

   ```sh
   nixalarm --source local_song 07:30
   ```

   Expected: the terminal arms the alarm and names the source at trigger time. To select a source persistently instead, set `alarm_source` at the top level of the config file, before any section headers:

   ```toml
   alarm_source = "local_song"
   ```

   If the name is unknown, startup stops with `nixalarm: unknown source: ...`; check the spelling with `--list-sources`. A `[sources.NAME]` section replaces a same-named built-in preset entirely.

8. Understand the fallback before relying on radio or network. If the selected source fails without producing audio, the alarm plays `fallback_source` instead, which defaults to `generated`. A source that plays and later stops does not trigger the fallback, and a `generated` alarm never falls back because it cannot fail that way. The configured start timeout bounds the FFmpeg protocol wait and the initial wait for SDR samples; it is not a total deadline for playback.

## Set up SDR gain

Every SDR source needs an explicit manual tuner gain in dB, including the built-in presets. Missing gain and the legacy `gain = "auto"` setting prevent an SDR alarm from being armed. The program never rewrites the config to fix this; every change below is applied by hand. Receiver gain is separate from playback volume: gain is applied by the radio tuner, volume is applied later in software. Too much gain can overload a strong signal and too little can lose a weak one. There is no universal value. Extra decimal places are truncated toward the tenth of a dB that `rtl_fm` resolves, and quoted numeric values remain accepted.

1. Run the read-only wizard for a configured source or a built-in preset:

   ```sh
   nixalarm --configure-source weatherband_162_400
   ```

   Add `--config PATH` to use a different config file. Only `--config` may accompany this action. The wizard requires an interactive terminal: standard input and standard error must be terminals, while standard output may be a pipe.

   Expected: the wizard asks for a gain, validates the value, asks for confirmation, and then prints TOML to standard output. Prompts and instructions go to standard error. The wizard does not access hardware, create or edit files, initialize audio, or arm an alarm. If the config path names an unreadable existing file, the wizard reports it instead of describing defaults.

2. Apply the printed snippet to the indicated config file by hand. For an existing source section, add or replace only its `gain` assignment without duplicating the section header. For a built-in preset, add the complete printed section: defining a source section replaces the preset, so a gain-only section is insufficient. Do not redirect the wizard output over the config file; the shell truncates the target before the wizard reads it.

3. Test reception before relying on the alarm:

   ```sh
   nixalarm --test-source weatherband_162_400
   ```

   Expected: radio audio plays. An accepted gain is syntactically valid, not hardware-tested, so choose the value for the receiver and reception conditions. Direct-sampling hardware may not apply tuner gain in the same way.

4. Select the source with `--source NAME` or the top-level `alarm_source` key, as in step 7. The wizard and `--test-source` never change the default source.

SDR startup checks only cover sources the run can actually use: the selected source and, when relevant, the fallback. Unrelated unused SDR sources never block startup, and `--help`, `--list-sources`, and clock-only runs skip the gain check. A clock-only run still parses the config and still rejects an unknown primary source name, but it starts no audio. Normal launches never prompt; an unusable gain in a reachable SDR source is reported before the clock arms.

### SDR reception notes

- `modulation` selects how `rtl_fm` demodulates. Use `nbfm` for narrowband FM such as the weather band and other public-service channels, `wbfm` for ordinary broadcast FM, and `am` for the broadcast and shortwave bands. A broadcast station tuned as `nbfm` comes through as noise or near-silence; that mismatch is what `wbfm` is for. The legacy `sdr_weatherband` type still parses as `sdr` with `nbfm`.
- AM broadcast sits below the roughly 24 MHz floor of a bare RTL-SDR tuner. Reach it through the direct-sampling branch by setting `direct_sampling = true`, or put an upconverter in front. The program warns when an AM source below that floor lacks the setting, and when a wideband FM source names an AM-band frequency.
- NOAA Weather Radio is a North American service, so outside its coverage area the built-in presets may sit silent however good the hardware is; broadcast FM or AM is the SDR option that travels.[^1] Coverage is location-dependent everywhere, so there is no preset for a specific transmitter: a broadcast station goes in the config as an `sdr` source with a frequency the local receiver hears.

## Next

- [Schedule an alarm](alarms.md) that plays the source.
- [Change the clock appearance](appearance.md).
- [nixalarm.1](../man/nixalarm.1) for the authoritative command line, built-in source list, and exit status.
- [nixalarm.5](../man/nixalarm.5) for the authoritative source definitions and gain validation.

## References

[^1]: National Weather Service, "NOAA Weather Radio All Hazards," accessed 2026, https://www.weather.gov/nwr/
