import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gamelex_deploy", ROOT / "scripts/deploy.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gamelex-deploy-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sd = self.root / "sd"
        (self.sd / "atmosphere").mkdir(parents=True)
        (self.sd / "switch").mkdir()
        self.package = self.root / "package"
        self.files = {
            "switch/.overlays/gamelex-probe.ovl": b"test-overlay",
            "config/gamelex/config.example.json": b'{"api_key":""}',
            deploy.MODULE + "/exefs.nsp": b"test-module",
        }
        hashes = {}
        for name, data in self.files.items():
            path = self.package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        self.manifest = {"schema_version": 1, "program_id": "42000000474C4558", "files": hashes}
        self.write_manifest()
        self.sink = io.StringIO()

    def write_manifest(self):
        (self.package / "manifest.json").write_text(json.dumps(self.manifest))

    def install(self, **kwargs):
        with contextlib.redirect_stdout(self.sink):
            deploy.install(self.sd, self.package, **kwargs)

    def test_dry_run_never_writes(self):
        before = sorted(str(p.relative_to(self.sd)) for p in self.sd.rglob("*"))
        self.install()
        self.assertEqual(before, sorted(str(p.relative_to(self.sd)) for p in self.sd.rglob("*")))

    def test_install_preserves_credentials_and_other_files(self):
        config = self.sd / "config/gamelex/config.json"
        config.parent.mkdir(parents=True)
        config.write_text('PRIVATE-EXISTING-KEY')
        unrelated = self.sd / "atmosphere/existing.bin"
        unrelated.write_bytes(b"keep")
        self.install(apply=True)
        self.assertEqual(config.read_text(), "PRIVATE-EXISTING-KEY")
        self.assertEqual(unrelated.read_bytes(), b"keep")
        self.assertFalse((self.sd / deploy.MODULE / "flags/boot2.flag").exists())
        self.assertNotIn("PRIVATE-EXISTING-KEY", self.sink.getvalue())

    def test_fresh_install_and_explicit_enable(self):
        self.install(apply=True, enable=True)
        self.assertEqual((self.sd / "config/gamelex/config.json").read_bytes(), b'{"api_key":""}')
        self.assertTrue((self.sd / deploy.MODULE / "flags/boot2.flag").is_file())

    def test_backup_before_replace(self):
        destination = self.sd / "switch/.overlays/gamelex-probe.ovl"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"old-overlay")
        self.install(apply=True)
        backups = list((self.sd / "config/gamelex/backups").rglob("gamelex-probe.ovl"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), b"old-overlay")
        self.assertEqual(destination.read_bytes(), b"test-overlay")

    def test_invalid_checksum_does_not_write(self):
        (self.package / "switch/.overlays/gamelex-probe.ovl").write_bytes(b"corrupted")
        with self.assertRaises(ValueError):
            self.install(apply=True)
        self.assertFalse((self.sd / "config").exists())

    def test_path_escape_and_symlink_are_rejected(self):
        self.manifest["files"]["../outside"] = "fake"
        self.write_manifest()
        with self.assertRaises(ValueError):
            self.install(apply=True)
        del self.manifest["files"]["../outside"]
        self.write_manifest()
        outside = self.root / "outside"
        outside.mkdir()
        (self.sd / "switch/.overlays").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.install(apply=True)
        self.assertEqual(list(outside.iterdir()), [])

    def test_copy_failure_rolls_back(self):
        overlay = self.sd / "switch/.overlays/gamelex-probe.ovl"
        overlay.parent.mkdir(parents=True)
        overlay.write_bytes(b"old-overlay")
        original = deploy.shutil.copyfile

        def fail_module(source, destination, *args, **kwargs):
            if str(destination).endswith("exefs.nsp.gamelex-tmp"):
                raise OSError("simulated SD write failure")
            return original(source, destination, *args, **kwargs)

        with patch.object(deploy.shutil, "copyfile", fail_module):
            with self.assertRaises(OSError):
                self.install(apply=True, enable=True)
        self.assertEqual(overlay.read_bytes(), b"old-overlay")
        self.assertFalse((self.sd / deploy.MODULE / "flags/boot2.flag").exists())
        self.assertFalse((self.sd / "config/gamelex/config.example.json").exists())

    def test_manifest_cannot_replace_config_or_other_overlays(self):
        for relative in ("config/gamelex/config.json", "switch/.overlays/ovlmenu.ovl", "switch/.overlays/gamelex-other.ovl", deploy.MODULE+"/other.bin"):
            self.manifest["files"][relative] = "fake"
            self.write_manifest()
            with self.assertRaises(ValueError):
                self.install(apply=True)
            del self.manifest["files"][relative]
        self.assertFalse((self.sd / "config").exists())

    def test_trial_flag_and_explicit_enable_do_not_conflict(self):
        relative = deploy.MODULE+"/flags/boot2.flag"
        source = self.package/relative
        source.parent.mkdir(parents=True)
        source.write_bytes(b"")
        self.manifest["files"][relative] = hashlib.sha256(b"").hexdigest()
        self.write_manifest()
        self.install(apply=True, enable=True)
        self.assertTrue((self.sd/relative).is_file())

    def test_backup_symlink_is_rejected_before_install(self):
        outside = self.root/"outside-backup"
        outside.mkdir()
        backup = self.sd/"config/gamelex/backups"
        backup.parent.mkdir(parents=True)
        backup.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.install(apply=True)
        self.assertFalse((self.sd/"switch/.overlays/gamelex-probe.ovl").exists())
        self.assertEqual(list(outside.iterdir()), [])

    def test_existing_temporary_file_is_never_overwritten(self):
        extra = self.sd/"switch/.overlays/gamelex-probe.ovl.gamelex-tmp"
        extra.parent.mkdir(parents=True)
        extra.write_bytes(b"keep")
        with self.assertRaises(ValueError):
            self.install(apply=True)
        self.assertEqual(extra.read_bytes(),b"keep")


if __name__ == "__main__":
    unittest.main()
