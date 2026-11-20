#!/usr/bin/env python3
"""CLI regressions using isolated configs, PTYs, and a fake receiver."""

import json
import os
from pathlib import Path
import pty
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())


class SdrCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nixalarm-sdr-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.config = self.root / "config.toml"
        self.env = dict(os.environ, XDG_CONFIG_HOME=str(self.root / "xdg"))
        self.env.pop("DISPLAY", None)
        self.env.pop("WAYLAND_DISPLAY", None)
        self.env.pop("SDL_VIDEODRIVER", None)
        self.env["SDL_AUDIODRIVER"] = "dummy"

    def run_cli(self, *args):
        return subprocess.run(
            [BINARY, "--config", str(self.config), *args],
            env=self.env, capture_output=True, text=True, timeout=10,
        )

    def write_source(self, gain):
        self.config.write_text(
            'alarm_source = "radio"\n'
            '[sources.radio]\ntype = "sdr"\nfrequency_mhz = 162.400\n'
            + ("" if gain is None else f"gain = {gain}\n")
        )

    def test_invalid_gain_fails_before_video_or_arming(self):
        for gain in (None, '"auto"', '"20junk"', '"nan"', '"inf"', "-10.0",
                     '"1e100"', '""'):
            with self.subTest(gain=gain):
                self.write_source(gain)
                result = self.run_cli("07:30")
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("armed for", result.stderr)
                self.assertNotIn("SDL init", result.stderr)
                self.assertIn("gain", result.stderr)
                self.assertIn("--configure-source", result.stderr)

    def test_source_listing_ignores_unconfigured_sdr(self):
        self.write_source('"auto"')
        result = self.run_cli("--list-sources")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("radio", result.stdout)

    def test_sdr_fallback_is_validated_before_arming(self):
        self.config.write_text(
            'alarm_source = "song"\nfallback_source = "radio"\n'
            '[sources.song]\ntype = "file"\npath = "/missing/audio"\n'
            '[sources.radio]\ntype = "sdr"\ngain = "auto"\n'
        )
        result = self.run_cli("07:30")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("radio", result.stderr)
        self.assertNotIn("armed for", result.stderr)

    def test_valid_gain_and_unused_sdr_reach_video_initialization(self):
        for body in (
            'alarm_source = "radio"\n[sources.radio]\ntype = "sdr"\ngain = 0\n',
            'alarm_source = "generated"\nfallback_source = "radio"\n'
            '[sources.radio]\ntype = "sdr"\ngain = "auto"\n',
            'alarm_source = "radio"\nalarms = []\n'
            '[sources.radio]\ntype = "sdr"\ngain = "auto"\n',
        ):
            with self.subTest(body=body):
                self.config.write_text(body)
                self.env["SDL_VIDEODRIVER"] = "nixalarm-nonexistent-driver"
                result = self.run_cli()
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("SDL init failed", result.stderr)
                self.assertNotIn("requires an explicit", result.stderr)

    def test_noninteractive_wizard_does_not_create_config(self):
        self.config = self.root / "missing" / "config.toml"
        result = self.run_cli("--configure-source", "weatherband_162_400")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertFalse(self.config.parent.exists())

    def test_help_and_version_do_not_create_config(self):
        for option in ("--help", "--version"):
            result = self.run_cli(option)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(self.config.exists())

    def test_wizard_rejects_conflicting_actions_without_writes(self):
        for extra in (["07:30"], ["--source", "generated"],
                      ["--test-source", "generated"], ["--list-sources"],
                      ["--fullscreen"], ["--windowed"]):
            for args in (
                ["--configure-source", "weatherband_162_400", *extra],
                [*extra, "--configure-source", "weatherband_162_400"],
            ):
                with self.subTest(args=args):
                    result = self.run_cli(*args)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertFalse(self.config.exists())

    def wizard(self, name, answers):
        master, slave = pty.openpty()
        try:
            process = subprocess.Popen(
                [BINARY, "--config", str(self.config),
                 "--configure-source", name],
                stdin=slave, stderr=slave, stdout=subprocess.PIPE, env=self.env,
            )
            os.close(slave)
            slave = None
            try:
                os.write(master, answers)
                output, _ = process.communicate(timeout=10)
                return process.returncode, output.decode()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        finally:
            os.close(master)
            if slave is not None:
                os.close(slave)

    def test_builtin_wizard_outputs_complete_definition_without_writes(self):
        code, output = self.wizard("weatherband_162_400", b"20\nyes\n")
        self.assertEqual(code, 0)
        self.assertIn("[sources.weatherband_162_400]", output)
        self.assertIn('type = "sdr"', output)
        self.assertIn("frequency_mhz", output)
        self.assertIn("gain", output)
        self.assertFalse(self.config.exists())

    def test_existing_wizard_preserves_file_and_prints_assignment(self):
        self.write_source('"auto"')
        before = self.config.read_bytes()
        code, output = self.wizard("radio", b"20\nyes\n")
        self.assertEqual(code, 0)
        self.assertIn("gain", output)
        self.assertNotIn("[sources.", output)
        self.assertEqual(self.config.read_bytes(), before)

    def test_wizard_accepts_source_name_with_leading_dash(self):
        self.config.write_text('[sources.-radio]\ntype = "sdr"\n')
        before = self.config.read_bytes()
        code, output = self.wizard("-radio", b"20\nyes\n")
        self.assertEqual(code, 0)
        self.assertIn("gain", output)
        self.assertEqual(self.config.read_bytes(), before)

    def test_cancelled_wizard_has_no_toml(self):
        code, output = self.wizard("weatherband_162_400", b"20\nno\n")
        self.assertEqual(code, 1)
        self.assertEqual(output, "")
        self.assertFalse(self.config.exists())

    def test_wizard_eof_has_no_toml(self):
        code, output = self.wizard("weatherband_162_400", b"\x04")
        self.assertEqual(code, 1)
        self.assertEqual(output, "")
        self.assertFalse(self.config.exists())

    def test_wizard_retries_invalid_gain(self):
        code, output = self.wizard("weatherband_162_400", b"auto\nnan\n20\nyes\n")
        self.assertEqual(code, 0)
        self.assertIn("gain", output)
        self.assertNotIn("nan", output)
        self.assertNotIn("auto", output)

    def test_wizard_rejects_unknown_and_non_sdr_sources(self):
        for name in ("unknown", "generated"):
            code, output = self.wizard(name, b"20\nyes\n")
            self.assertEqual(code, 2)
            self.assertEqual(output, "")
            self.assertFalse(self.config.exists())

    def test_wizard_rejects_unsupported_source_section_name(self):
        self.config.write_text('[sources.radio name]\ntype = "sdr"\n')
        before = self.config.read_bytes()
        code, output = self.wizard("radio name", b"20\nyes\n")
        self.assertEqual(code, 2)
        self.assertEqual(output, "")
        self.assertEqual(self.config.read_bytes(), before)

    def test_wizard_rejects_directory_as_config(self):
        self.config.mkdir()
        code, output = self.wizard("weatherband_162_400", b"20\nyes\n")
        self.assertIn(code, (1, 2))
        self.assertEqual(output, "")
        self.assertEqual(list(self.config.iterdir()), [])

    @unittest.skipUnless(shutil.which("xvfb-run"), "requires xvfb-run")
    def test_receiver_always_receives_explicit_gain(self):
        receiver = self.root / "rtl_fm"
        capture = self.root / "argv.json"
        receiver.write_text(
            f"#!{sys.executable}\n"
            "import json, os, sys\n"
            "with open(os.environ['NIXALARM_TEST_CAPTURE'], 'w') as f:\n"
            "    json.dump(sys.argv[1:], f)\n"
            "sys.stdout.buffer.write(b'\\0' * 4096)\n"
            "sys.stdout.buffer.flush()\n"
        )
        receiver.chmod(0o755)
        env = dict(self.env, PATH=f"{self.root}:{os.environ['PATH']}",
                   NIXALARM_TEST_CAPTURE=str(capture))
        for fallback in (False, True):
            with self.subTest(fallback=fallback):
                capture.unlink(missing_ok=True)
                self.write_source('"20.0"')
                if fallback:
                    self.config.write_text(
                        'alarm_source = "broken"\nfallback_source = "radio"\n'
                        '[sources.broken]\ntype = "file"\n'
                        'path = "/nonexistent/nixalarm-test-audio"\n'
                        + self.config.read_text().split("\n", 1)[1]
                    )
                process = subprocess.Popen(
                    ["xvfb-run", "-a", BINARY, "--config", str(self.config),
                     "--test-source", "broken" if fallback else "radio"],
                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    start_new_session=True,
                )
                try:
                    deadline = time.monotonic() + 15
                    args = None
                    while time.monotonic() < deadline:
                        try:
                            args = json.loads(capture.read_text())
                            break
                        except (FileNotFoundError, json.JSONDecodeError):
                            pass
                        if process.poll() is not None:
                            break
                        time.sleep(0.05)
                    self.assertIsNotNone(args, "fake rtl_fm was not invoked")
                    self.assertEqual(args.count("-g"), 1)
                    self.assertEqual(float(args[args.index("-g") + 1]), 20.0)
                finally:
                    import signal
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        process.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.communicate()


if __name__ == "__main__":
    unittest.main()
