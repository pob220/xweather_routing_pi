import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location(
    "prepare", Path(__file__).parents[1] / "prepare-alpha-artifacts.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class AlphaArtifacts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def pair(self, name="trixie", plugin="xWeatherRouting", version="1.17.1.0",
             library="libxweather_routing_pi.so", flatpak=False, extras=()):
        directory = self.root / "artifacts" / name / "package"
        directory.mkdir(parents=True)
        archive = directory / f"xweather_routing_pi-{version}-{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as out:
            data = b"fixture"
            for filename in (library, *extras):
                item = tarfile.TarInfo(f"plugin/lib/{filename}")
                item.size = len(data)
                out.addfile(item, io.BytesIO(data))
        metadata = directory / (f"xweather_routing_pi-{version}-metadata-{name}.xml"
                                if flatpak else archive.name[:-7] + ".xml")
        root = ET.Element("plugin", version="1")
        for key, text in {
            "name": plugin, "version": version, "api-version": "1.21",
            "source": "https://github.com/pob220/xweather_routing_pi",
            "target": name, "target-version": "13", "target-arch": "x86_64",
            "tarball-url": "https://dl.cloudsmith.io/public/--pkg_repo--/raw/--name--",
        }.items():
            ET.SubElement(root, key).text = text
        ET.ElementTree(root).write(metadata)
        return directory, archive, metadata

    def test_standalone_pair(self):
        directory, archive, _ = self.pair()
        self.assertEqual(prepare.inspect_pair(directory)[0], archive)

    def test_flatpak_pair(self):
        directory, _, _ = self.pair(flatpak=True)
        prepare.inspect_pair(directory)

    def test_reject_standard_name(self):
        directory, _, _ = self.pair(plugin="WeatherRouting")
        with self.assertRaisesRegex(ValueError, "Wrong plugin name"):
            prepare.inspect_pair(directory)

    def test_reject_standard_library(self):
        directory, _, _ = self.pair(library="libweather_routing_pi.so")
        with self.assertRaises(ValueError):
            prepare.inspect_pair(directory)

    def test_reject_windows_test_dependencies(self):
        directory, _, _ = self.pair(library="xweather_routing_pi.dll",
                                    extras=("gtest.dll", "gmock.dll"))
        with self.assertRaisesRegex(ValueError, "test library included"):
            prepare.inspect_pair(directory)

    def test_reject_multiple_archives(self):
        directory, archive, _ = self.pair()
        (directory / "stale.tar.gz").write_bytes(archive.read_bytes())
        with self.assertRaisesRegex(ValueError, "exactly one archive"):
            prepare.inspect_pair(directory)

    def test_reject_ambiguous_flatpak(self):
        directory, _, metadata = self.pair(flatpak=True)
        (directory / "xweather_routing_pi-1.17.1.0-other.xml").write_bytes(metadata.read_bytes())
        with self.assertRaisesRegex(ValueError, "unique same-version"):
            prepare.inspect_pair(directory)

    def test_reject_version_mismatch(self):
        directory, _, metadata = self.pair()
        root = ET.parse(metadata)
        root.find("version").text = "1.17.0.0"
        root.write(metadata)
        with self.assertRaisesRegex(ValueError, "version mismatch"):
            prepare.inspect_pair(directory)

    def test_reject_path_traversal(self):
        directory, _, _ = self.pair(library="../../../libxweather_routing_pi.so")
        with self.assertRaisesRegex(ValueError, "Unsafe archive"):
            prepare.inspect_pair(directory)

    def test_incomplete_matrix_writes_nothing(self):
        self.pair()
        output = self.root / "release"
        with self.assertRaisesRegex(ValueError, "matrix"):
            prepare.prepare(self.root / "artifacts", output, "abcdef1", "1")
        self.assertFalse(output.exists())

    def test_complete_matrix(self):
        for name in prepare.TARGETS:
            self.pair(name, flatpak=name.startswith("flatpak"))
        output = self.root / "release"
        prepare.prepare(self.root / "artifacts", output, "abcdef1", "23")
        uploads = json.loads((output / "uploads.json").read_text())
        self.assertEqual(len(uploads), 18)
        self.assertEqual(len({u["name"] for u in uploads}), 18)
        self.assertTrue(all(u["version"] == "1.17.1.0+23.abcdef1" for u in uploads))
        for metadata in output.glob("*.xml"):
            url = ET.parse(metadata).findtext("tarball-url").strip()
            self.assertIn("/pob220/xweather-routing-alpha/", url)
            self.assertNotIn("--", url)

    def test_mixed_versions_write_nothing(self):
        for name in prepare.TARGETS:
            self.pair(name, version="1.16.0.0" if name == "trixie" else "1.17.1.0")
        output = self.root / "release"
        with self.assertRaisesRegex(ValueError, "Mixed versions"):
            prepare.prepare(self.root / "artifacts", output, "abcdef1", "1")
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
