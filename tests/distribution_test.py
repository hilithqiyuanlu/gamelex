import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import deploy
import uninstall
import package_trial
from release_info import release_version


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gamelex-distribution-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.sd = self.root / "sd"
        (self.sd / "atmosphere").mkdir(parents=True)
        (self.sd / "switch").mkdir()
        self.files = {"switch/.overlays/gamelex-probe.ovl": b"overlay", deploy.MODULE+"/exefs.nsp": b"module",
                      "switch/gamelex-device-probe/gamelex-device-probe.nro": b"diagnostic app",
                      "config/gamelex/config.example.json": b'{"api_key":""}'}
        for name, data in self.files.items():
            p = self.package/name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        self.manifest = {"schema_version":1,"release":release_version(),"protocol_version":1,"program_id":"42000000474C4558",
                         "files": {name:hashlib.sha256(data).hexdigest() for name,data in self.files.items()}}
        self.write_manifest()
        self.source = self.root/"source.zip"
        with zipfile.ZipFile(self.source,"w") as archive:
            archive.writestr("gamelex/source.cpp",b"public source")
        self.output = self.root/"trial.zip"
        self.sink = io.StringIO()

    def write_manifest(self):
        (self.package/"manifest.json").write_text(json.dumps(self.manifest))

    def installed(self):
        with contextlib.redirect_stdout(self.sink):
            deploy.install(self.sd,self.package,apply=True,enable=True)
        config=self.sd/"config/gamelex/config.json"
        config.write_bytes(b"PRIVATE-USER-CONFIG")
        unrelated=self.sd/"switch/.overlays/ovlmenu.ovl"
        unrelated.write_bytes(b"keep-menu")

    def test_trial_hashes_flags_source_and_tools(self):
        package_trial.build_trial(self.package,self.source,self.output)
        with zipfile.ZipFile(self.output) as archive:
            manifest=json.loads(archive.read("sd/manifest.json"))
            self.assertTrue(manifest["auto_start_included"])
            for name,sha in manifest["files"].items():
                self.assertEqual(hashlib.sha256(archive.read("sd/"+name)).hexdigest(),sha)
            self.assertIn("tools/deploy.py",archive.namelist())
            self.assertIn("tools/uninstall.py",archive.namelist())
            self.assertIn(f"gamelex-{release_version()}-source.zip",archive.namelist())
            self.assertNotIn("sd/config/gamelex/config.json",archive.namelist())
            for line in archive.read("SHA256SUMS.txt").decode().splitlines():
                sha,name=line.split("  ",1)
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(),sha)

    def test_private_source_and_secret_content_rejected(self):
        with zipfile.ZipFile(self.source,"w") as archive:
            archive.writestr("gamelex/config/config.local.json",b"{}")
        with self.assertRaises(ValueError):
            package_trial.build_trial(self.package,self.source,self.output)
        self.assertFalse(self.output.exists())
        with zipfile.ZipFile(self.source,"w") as archive:
            archive.writestr("gamelex/source.cpp",b"TEST-CREDENTIAL-SECRET")
        with patch.object(package_trial,"private_values",return_value=[b"TEST-CREDENTIAL-SECRET"]):
            with self.assertRaises(ValueError):
                package_trial.build_trial(self.package,self.source,self.output)
        self.assertFalse(self.output.exists())

    def test_wrong_release_and_checksum_rejected(self):
        self.manifest["release"]="0.0.0"
        self.write_manifest()
        with self.assertRaises(ValueError):
            package_trial.build_trial(self.package,self.source,self.output)
        self.manifest["release"]=release_version()
        self.write_manifest()
        (self.package/"switch/.overlays/gamelex-probe.ovl").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            package_trial.build_trial(self.package,self.source,self.output)

    def test_uninstall_preserves_data_and_has_restore_backup(self):
        self.installed()
        before={str(p.relative_to(self.sd)):p.read_bytes() for p in self.sd.rglob("*") if p.is_file()}
        with contextlib.redirect_stdout(self.sink):
            uninstall.uninstall(self.sd)
        self.assertEqual(before,{str(p.relative_to(self.sd)):p.read_bytes() for p in self.sd.rglob("*") if p.is_file()})
        with contextlib.redirect_stdout(self.sink):
            uninstall.uninstall(self.sd,apply=True)
        for relative in uninstall.FILES:
            self.assertFalse((self.sd/relative).exists())
            backups=list((self.sd/"config/gamelex/backups").glob("uninstall-*/"+relative))
            self.assertEqual(len(backups),1)
            self.assertEqual(backups[0].read_bytes(),before[relative])
        self.assertEqual((self.sd/"config/gamelex/config.json").read_bytes(),b"PRIVATE-USER-CONFIG")
        self.assertEqual((self.sd/"switch/.overlays/ovlmenu.ovl").read_bytes(),b"keep-menu")
        self.assertNotIn("PRIVATE-USER-CONFIG",self.sink.getvalue())

    def test_uninstall_symlink_and_removal_failure(self):
        self.installed()
        overlay=self.sd/"switch/.overlays/gamelex-probe.ovl"
        original=Path.unlink

        def fail(path,*args,**kwargs):
            if path==overlay:
                raise OSError("simulated removal failure")
            return original(path,*args,**kwargs)

        with patch.object(Path,"unlink",fail),contextlib.redirect_stdout(self.sink):
            with self.assertRaises(OSError):
                uninstall.uninstall(self.sd,apply=True)
        for relative in uninstall.FILES:
            self.assertTrue((self.sd/relative).is_file())
        overlay.unlink()
        outside=self.root/"outside"
        outside.write_bytes(b"outside")
        overlay.symlink_to(outside)
        with self.assertRaises(ValueError):
            uninstall.uninstall(self.sd,apply=True)
        self.assertEqual(outside.read_bytes(),b"outside")


if __name__=="__main__":
    unittest.main()
