#!/usr/bin/env python3
"""Tests for scripts/nightly_manifest.py (stdlib unittest, no external deps).

Run from the repo root:  python -m unittest discover -s scripts/tests -v
"""

import os
import sys
import unittest
from datetime import datetime, timezone

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import nightly_manifest  # noqa: E402

SINCE = datetime(2026, 11, 5, 12, 0, tzinfo=timezone.utc)


def asset(name, updated_at="2026-11-05T13:00:00Z", digest="sha256:" + "ab" * 32, size=100):
    return {"name": name, "updated_at": updated_at, "digest": digest, "size": size}


def manifest(*assets):
    release = {"html_url": "https://example.invalid/release", "assets": list(assets)}
    return nightly_manifest.build_manifest(release, "f" * 40, 1762185494, "2026-11-03T15:58:14Z", SINCE, "2.5.0-Nanashi", "")


class ManifestTest(unittest.TestCase):
    def test_every_app_file_of_the_release_is_classified(self):
        names = [
            "OrcaSlicer_Linux_AppImage_Ubuntu2204_nightly.AppImage",
            "OrcaSlicer_Linux_AppImage_Ubuntu2404_nightly_aarch64.AppImage",
            "OrcaSlicer-Linux-flatpak_nightly_x86_64.flatpak",
            "OrcaSlicer-Linux-flatpak_nightly_aarch64.flatpak",
            "OrcaSlicer_Windows_Installer_nightly.exe",
            "OrcaSlicer_Windows_Installer_nightly_arm64.exe",
            "OrcaSlicer_Windows_portable_nightly.zip",
            "OrcaSlicer_Windows_portable_nightly_arm64.zip",
            "OrcaSlicer_Mac_universal_nightly.dmg",
            "OrcaSlicer_Mac_universal_nightly.app.tar.gz",
        ]
        m = manifest(*(asset(n) for n in names))
        by_name = {a["name"]: a for a in m["assets"]}
        self.assertEqual(sorted(by_name), sorted(names))
        self.assertEqual(by_name[names[0]]["distro"], "ubuntu2204")
        self.assertEqual((by_name[names[1]]["arch"], by_name[names[1]]["distro"]), ("arm64", "ubuntu2404"))
        self.assertEqual(by_name[names[5]]["kind"], "installer")
        self.assertEqual(by_name[names[9]]["kind"], "app")

    def test_files_that_are_not_the_app_are_left_out(self):
        m = manifest(asset("OrcaSlicer_profile_validator_Windows_nightly.exe"),
                     asset("generate_system_cache_Linux_Ubuntu2404_nightly"),
                     asset("orca_custom_preset_tests.zip"),
                     asset("latest.json"))
        self.assertEqual(m["assets"], [])

    def test_a_file_from_before_the_run_is_not_current(self):
        m = manifest(asset("OrcaSlicer_Windows_Installer_nightly.exe", updated_at="2026-11-05T11:59:59Z"),
                     asset("OrcaSlicer_Windows_portable_nightly.zip", updated_at="2026-11-05T12:00:00Z"))
        current = {a["name"]: a["current"] for a in m["assets"]}
        self.assertFalse(current["OrcaSlicer_Windows_Installer_nightly.exe"])
        self.assertTrue(current["OrcaSlicer_Windows_portable_nightly.zip"])

    def test_the_hash_and_size_come_from_the_release(self):
        (a,) = manifest(asset("OrcaSlicer_Mac_universal_nightly.dmg", digest="sha256:" + "0c" * 32, size=42))["assets"]
        self.assertEqual((a["sha256"], a["size"]), ("0c" * 32, 42))

    def test_a_file_without_a_digest_stops_the_manifest(self):
        with self.assertRaises(SystemExit):
            manifest(asset("OrcaSlicer_Mac_universal_nightly.dmg", digest=""))

    def test_the_manifest_names_the_commit_and_schema(self):
        m = manifest()
        self.assertEqual((m["schema"], m["commit"], m["commit_time"]), (1, "f" * 40, 1762185494))


if __name__ == "__main__":
    unittest.main()
