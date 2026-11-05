#!/usr/bin/env python3
"""Write latest.json, the manifest of the nightly release.

Nightly builds read it to decide whether there is a newer build and which file
to install. See docs/HLSD/nightly_updates.md for the format.

The release's files are read from the GitHub API response for the release
(`gh api repos/<repo>/releases/tags/Nightly-Rolling`), which already carries
each file's size and SHA-256, so nothing is downloaded. A file updated before
--since was not rebuilt by this run and is marked as not current.
"""

import argparse
import json
import re
import sys
from datetime import datetime

# File name -> what it is. Files not listed here (test bundles, the profile
# validator, cache generators) are left out of the manifest.
ASSET_KINDS = [
    # One AppImage per arch, for every supported Ubuntu, so it names no base.
    (r"^OrcaSlicer_Linux_AppImage_nightly\.AppImage$",
     lambda _: dict(platform="linux", arch="x86_64", kind="appimage")),
    (r"^OrcaSlicer_Linux_AppImage_nightly_aarch64\.AppImage$",
     lambda _: dict(platform="linux", arch="arm64", kind="appimage")),
    # Earlier nightlies built one AppImage per Ubuntu base. Such files left on the
    # release are no longer rebuilt, so they are listed as not current.
    (r"^OrcaSlicer_Linux_AppImage_Ubuntu(\d{2})(\d{2})_nightly\.AppImage$",
     lambda m: dict(platform="linux", arch="x86_64", kind="appimage", distro=f"ubuntu{m[1]}{m[2]}")),
    (r"^OrcaSlicer_Linux_AppImage_Ubuntu(\d{2})(\d{2})_nightly_aarch64\.AppImage$",
     lambda m: dict(platform="linux", arch="arm64", kind="appimage", distro=f"ubuntu{m[1]}{m[2]}")),
    (r"^OrcaSlicer-Linux-flatpak_nightly_x86_64\.flatpak$",
     lambda _: dict(platform="linux", arch="x86_64", kind="flatpak")),
    (r"^OrcaSlicer-Linux-flatpak_nightly_aarch64\.flatpak$",
     lambda _: dict(platform="linux", arch="arm64", kind="flatpak")),
    (r"^OrcaSlicer_Windows_Installer_nightly\.exe$",
     lambda _: dict(platform="windows", arch="x86_64", kind="installer")),
    (r"^OrcaSlicer_Windows_Installer_nightly_arm64\.exe$",
     lambda _: dict(platform="windows", arch="arm64", kind="installer")),
    (r"^OrcaSlicer_Windows_portable_nightly\.zip$",
     lambda _: dict(platform="windows", arch="x86_64", kind="portable")),
    (r"^OrcaSlicer_Windows_portable_nightly_arm64\.zip$",
     lambda _: dict(platform="windows", arch="arm64", kind="portable")),
    (r"^OrcaSlicer_Mac_universal_nightly\.dmg$",
     lambda _: dict(platform="macos", arch="universal", kind="dmg")),
    (r"^OrcaSlicer_Mac_universal_nightly\.app\.tar\.gz$",
     lambda _: dict(platform="macos", arch="universal", kind="app")),
]


def parse_time(text):
    return datetime.fromisoformat(text.replace("Z", "+00:00"))


def classify(name):
    for pattern, describe in ASSET_KINDS:
        m = re.match(pattern, name)
        if m:
            return describe(m)
    return None


def build_manifest(release, commit, commit_time, commit_date, since, version, notes):
    assets = []
    for asset in release.get("assets", []):
        info = classify(asset["name"])
        if info is None:
            continue
        digest = asset.get("digest") or ""
        if not digest.startswith("sha256:"):
            sys.exit(f"{asset['name']}: the release reports no SHA-256 digest")
        info.update(
            name=asset["name"],
            sha256=digest[len("sha256:"):],
            size=asset["size"],
            current=parse_time(asset["updated_at"]) >= since,
        )
        assets.append(info)
    assets.sort(key=lambda a: a["name"])
    return {
        "schema": 1,
        "channel": "nightly",
        "version": version,
        "commit": commit,
        "commit_date": commit_date,
        "commit_time": commit_time,
        "release_url": release["html_url"],
        "notes": notes,
        "assets": assets,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--release", required=True, help="GitHub API JSON of the release")
    parser.add_argument("--commit", required=True, help="full hash of the commit the run built")
    parser.add_argument("--commit-time", required=True, type=int, help="committer time, seconds since the epoch")
    parser.add_argument("--commit-date", required=True, help="committer date, ISO 8601")
    parser.add_argument("--since", required=True, help="start of the run, ISO 8601; older files are not current")
    parser.add_argument("--version", required=True)
    parser.add_argument("--notes", default="", help="text shown in the update dialog")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.release, encoding="utf-8") as f:
        release = json.load(f)
    manifest = build_manifest(release, args.commit, args.commit_time, args.commit_date,
                              parse_time(args.since), args.version, args.notes)
    if not any(a["current"] for a in manifest["assets"]):
        sys.exit("no file on the release was uploaded by this run")
    # Written once, without a trailing newline: the signature covers these exact bytes.
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
