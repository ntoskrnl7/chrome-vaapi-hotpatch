"""Launcher policy tests: no installed Chrome, VA-API driver, or GPU required."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
LAUNCHERS = (
    "h264-only/run-h264-vaapi-chrome.sh",
    "h264-h265/run-chrome-h264-h265-vaapi.sh",
    "h264-h265/dist-run-chrome-h264-h265-vaapi.sh",
)


class LauncherTests(unittest.TestCase):
    def run_launcher(self, launcher, settings):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / launcher
            script.parent.mkdir(parents=True)
            shutil.copy2(ROOT / launcher, script)
            mock_bin = root / "bin"
            mock_bin.mkdir()
            for name, body in {
                "make": "exit 0",
                "vainfo": 'echo "${MOCK_VAINFO:-Intel iHD driver}"',
                "chrome": '''if [[ "${1:-}" == --version ]]; then
  echo 'Google Chrome 148.0.7778.96'
else
  printf '%s\\n' "$@" >"$CAPTURE"
  printf 'packed=%s\\n' "${CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS:-unset}" >>"$CAPTURE"
fi''',
            }.items():
                path = mock_bin / name
                path.write_text("#!/bin/bash\n" + body + "\n")
                path.chmod(0o755)
            runtime = root / "runtime"
            (runtime / "chrome-dir").mkdir(parents=True)
            shutil.copy2(mock_bin / "chrome", runtime / "chrome-dir/chrome")
            for relative in (
                "injector/bin/libchrome_hevc_trampoline_probe.so",
                "injector/bin/libchrome_hevc_delegate_scout.so",
                "delegate-injection/bin/libh265_delegate_bundle_probe.so",
            ):
                path = root / "h264-h265" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to("/bin/true")
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(("CHROME_", "LIBVA_", "LD_PRELOAD"))}
            env.update({
                "PATH": str(mock_bin) + os.pathsep + os.environ["PATH"],
                "CHROME": str(mock_bin / "chrome"),
                "CHROME_SOURCE": str(mock_bin / "chrome"),
                "CHROME_H264_VAAPI_RUNTIME_DIR": str(runtime),
                "CHROME_H265_VAAPI_RUNTIME": str(runtime),
                "CHROME_H265_AUTO_OFFSETS": "0",
                "CHROME_HEVC_PRELOAD": "/dev/null",
                "CAPTURE": str(root / "args"),
            })
            env.update(settings)
            subprocess.run(["bash", str(script), "about:blank"], env=env,
                           check=True, capture_output=True, text=True, timeout=10)
            return (root / "args").read_text().splitlines()

    def test_nvidia_flags(self):
        for launcher in LAUNCHERS:
            for settings in ({"LIBVA_DRIVER_NAME": "nvidia"},
                             {"MOCK_VAINFO": "VA-API NVDEC driver"}):
                with self.subTest(launcher=launcher, settings=settings):
                    args = self.run_launcher(launcher, settings)
                    features = next(arg for arg in args if arg.startswith("--enable-features="))
                    self.assertIn("VaapiOnNvidiaGPUs", features)
                    self.assertIn("VaapiIgnoreDriverChecks", features)
                    self.assertIn("--ignore-gpu-blocklist", args)
                    self.assertIn("about:blank", args)

    def test_intel_unchanged(self):
        for launcher in LAUNCHERS:
            with self.subTest(launcher=launcher):
                args = self.run_launcher(launcher, {"LIBVA_DRIVER_NAME": "iHD"})
                self.assertIn("--enable-features=AcceleratedVideoEncoder", args)
                self.assertNotIn("--ignore-gpu-blocklist", args)

    def test_feature_overrides(self):
        for launcher in LAUNCHERS:
            prefix = "CHROME_H264" if launcher.startswith("h264-only/") else "CHROME_H265"
            with self.subTest(launcher=launcher):
                args = self.run_launcher(launcher, {
                    "LIBVA_DRIVER_NAME": "nvidia", prefix + "_ENABLE_FEATURES": "CustomFeature",
                    prefix + "_IGNORE_GPU_BLOCKLIST": "0",
                })
                self.assertIn("--enable-features=CustomFeature", args)
                self.assertNotIn("--ignore-gpu-blocklist", args)
                args = self.run_launcher(launcher, {
                    "LIBVA_DRIVER_NAME": "nvidia", prefix + "_EXTRA_FEATURES": "ExtraFeature",
                })
                features = next(arg for arg in args if arg.startswith("--enable-features="))
                self.assertIn("VaapiOnNvidiaGPUs", features)
                self.assertTrue(features.endswith(",ExtraFeature"))

    def test_packed_header_policy_reaches_gpu(self):
        for launcher in LAUNCHERS[1:]:
            for value in ("0", "1"):
                with self.subTest(launcher=launcher, value=value):
                    settings = {} if value == "1" else {"CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS": "0"}
                    args = self.run_launcher(launcher, settings)
                    self.assertIn("packed=" + value, args)
                    gpu = next(arg for arg in args if arg.startswith("--gpu-launcher="))
                    self.assertIn("CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS=" + value, gpu)


if __name__ == "__main__":
    unittest.main()
