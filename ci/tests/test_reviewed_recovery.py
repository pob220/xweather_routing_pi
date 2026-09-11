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

    def test_explicit_replacement_source(self):
        job = self.source()
        job['vcs_revision'] = 'a' * 40
        job['workflows']['workflow_id'] = 'recovery-workflow'
        recovery.check_source(job, 'a' * 40, 'recovery-workflow')
        with self.assertRaisesRegex(ValueError, 'revision'):
            recovery.check_source(job)

    def test_replacement_requires_exact_successful_job(self):
        job = {'name': 'ubuntu22.04-x86_64-recovery', 'status': 'success', 'job_number': 123}
        self.assertEqual(recovery.replacement_jammy([job], 'a' * 40, 'wf'),
                         (123, 'a' * 40, 'wf'))
        for jobs in ([], [job, job], [dict(job, status='failed')],
                     [dict(job, name='unrelated-build')]):
            with self.assertRaises(ValueError):
                recovery.replacement_jammy(jobs, 'a' * 40, 'wf')

    def test_replacement_rejects_missing_provenance(self):
        job = {'name': 'ubuntu22.04-x86_64-recovery', 'status': 'success', 'job_number': 123}
        for revision, workflow in (('main', 'wf'), ('a' * 40, '')):
            with self.assertRaises(ValueError):
                recovery.replacement_jammy([job], revision, workflow)

    def test_wait_ignores_only_explicitly_replaced_jammy(self):
        from unittest.mock import patch
        jobs = [{'job_number': n, 'status': 'failed' if target == 'jammy' else 'success'}
                for target, n in recovery.JOBS.items()]
        retained = {t: n for t, n in recovery.JOBS.items() if t != 'jammy'}
        with patch.object(recovery, 'get_json', return_value={'items': jobs}):
            recovery.wait_for_builds(0, retained)
            with self.assertRaisesRegex(RuntimeError, 'cannot be published'):
                recovery.wait_for_builds(0)

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
