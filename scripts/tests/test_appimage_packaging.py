"""Exercise AppImage packaging with local download and appimagetool stand-ins.

Run: python3 -m unittest scripts.tests.test_appimage_packaging -v
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = REPO_ROOT / "src/dev-utils/platform/unix"


@unittest.skipUnless(shutil.which("bash") and os.name == "posix", "requires Bash and Unix tools")
class AppImagePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="orca-appimage-")
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source = root / "source tree"
        self.build = root / "separate build"
        self.package = self.build / "package"
        self.package.mkdir(parents=True)
        metadata = self.source / "scripts/flatpak/dev.namelessnanashi.OrcaSlicer.metainfo.xml"
        metadata.parent.mkdir(parents=True)
        metadata.write_text("<component><id>dev.namelessnanashi.OrcaSlicer</id></component>\n")
        self.metadata = metadata
        (self.package / "orca-slicer").write_text("#!/bin/sh\nexit 0\n")
        images = self.package / "resources/images"
        images.mkdir(parents=True)
        (images / "OrcaSlicer_192px.png").write_bytes(b"test icon")

        self.write_script(self.build / "build_appimage.sh", self.configure("build_appimage.sh.in"))
        bin_dir = root / "bin"
        bin_dir.mkdir()
        # Failed downloads leave partial bytes, just as wget -O does.
        self.write_script(bin_dir / "wget", """#!/usr/bin/env bash
set -eu
while [ "$1" != -O ]; do shift; done
output="$2"
if [ "${FAIL_DOWNLOAD:-0}" = 1 ]; then
    printf partial > "$output"
    exit 8
fi
cp "$FAKE_APPIMAGETOOL" "$output"
""")
        tool = root / "fake-appimagetool"
        self.write_script(tool, """#!/usr/bin/env bash
set -eu
if [ "${FAIL_TOOL:-0}" = 1 ]; then exit 42; fi
test -f usr/share/metainfo/dev.namelessnanashi.OrcaSlicer.metainfo.xml
printf appimage > "OrcaSlicer-$(uname -m).AppImage"
""")
        self.env = dict(os.environ, PATH=f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
                        FAKE_APPIMAGETOOL=str(tool))
        self.env.pop("container", None)

    @staticmethod
    def write_script(path, text):
        path.write_text(text)
        path.chmod(0o755)

    def configure(self, name):
        text = (TEMPLATES / name).read_text()
        for key, value in {"CMAKE_SOURCE_DIR": str(self.source), "SLIC3R_APP_KEY": "OrcaSlicer",
                           "SLIC3R_APP_CMD": "orca-slicer", "SoftFever_VERSION": "test"}.items():
            text = text.replace(f"@{key}@", value)
        return text

    def run_packager(self, **env):
        return subprocess.run(["bash", str(self.build / "build_appimage.sh")],
                              cwd=self.package, env=dict(self.env, **env),
                              capture_output=True, text=True)

    def test_success_copies_metadata_from_source_tree(self):
        result = self.run_packager()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.package / "OrcaSlicer_Linux_Vtest.AppImage").is_file())
        for suffix in ("metainfo.xml", "appdata.xml"):
            installed = self.package / f"usr/share/metainfo/dev.namelessnanashi.OrcaSlicer.{suffix}"
            self.assertEqual(installed.read_bytes(), self.metadata.read_bytes())

    def test_failed_download_preserves_previous_tool_and_package(self):
        previous = self.build / "appimagetool.AppImage"
        previous.write_text("previous tool")
        result = self.run_packager(FAIL_DOWNLOAD="1")
        self.assertEqual(result.returncode, 8, result.stderr)
        self.assertEqual(previous.read_text(), "previous tool")
        self.assertTrue((self.package / "orca-slicer").is_file())
        self.assertFalse((self.package / "AppRun").exists())

    def test_missing_metadata_stops_packaging(self):
        self.metadata.unlink()
        result = self.run_packager()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.package / "OrcaSlicer_Linux_Vtest.AppImage").exists())

    def test_wrapper_propagates_packager_failure(self):
        # The bundle stage needs compiled binaries; exercise the packaging stage
        # against the small prepared bundle instead.
        wrapper = self.configure("build_linux_image.sh.in")
        packaging_stage = wrapper[wrapper.index('if [[ -n "$BUILD_IMAGE" ]]'):]
        result = subprocess.run(["bash", "-c", "set -e\nBUILD_IMAGE=1\n" + packaging_stage],
                                cwd=self.build, env=dict(self.env, FAIL_TOOL="1"),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 42, result.stderr)
        self.assertNotIn("done", result.stdout)
        self.assertFalse((self.build / "OrcaSlicer_Linux_Vtest.AppImage").exists())


if __name__ == "__main__":
    unittest.main()
