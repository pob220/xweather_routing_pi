import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "recovery", Path(__file__).parents[1] / "collect-reviewed-1.17.2.py")
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class ReviewedRecovery(unittest.TestCase):
    def source(self):
        return {"status": "success", "vcs_revision": recovery.REVISION,
                "workflows": {"workflow_id": recovery.WORKFLOW}}

    def test_exact_source(self):
        recovery.check_source(self.source())

    def test_reject_other_revision(self):
        job = self.source()
        job["vcs_revision"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "revision"):
            recovery.check_source(job)

    def test_reject_other_workflow(self):
        job = self.source()
        job["workflows"]["workflow_id"] = "other"
        with self.assertRaisesRegex(ValueError, "workflow"):
            recovery.check_source(job)

    def test_reject_failed_source(self):
        job = self.source()
        job["status"] = "failed"
        with self.assertRaisesRegex(ValueError, "succeeded"):
            recovery.check_source(job)

    def test_windows_ignores_nested_cpack_duplicates(self):
        items = [{"path": "windows-x86/" + p, "url": "https://example.org/artifact"}
                 for p in ("plugin.tar.gz", "plugin.xml", "_CPack/plugin.tar.gz")]
        self.assertEqual(len(recovery.select_artifacts("windows-x86", items)), 2)

    def test_reject_missing_pair(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            recovery.select_artifacts("trixie", [])

    def test_reject_insecure_url(self):
        items = [{"path": "windows-x86/" + p, "url": "http://example.org/artifact"}
                 for p in ("plugin.tar.gz", "plugin.xml")]
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            recovery.select_artifacts("windows-x86", items)
