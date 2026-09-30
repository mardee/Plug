import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "ci_plan", Path(__file__).resolve().parents[1] / "scripts" / "ci_plan.py"
)
ci_plan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci_plan)


class BuildPlanTests(unittest.TestCase):
    def test_feature_push_is_windows_vst3_only(self):
        jobs = ci_plan.build_matrix("push", "refs/heads/feat/prism-one-knob-ui")["include"]
        self.assertEqual(len(jobs), 1)
        self.assertEqual(jobs[0]["platform"], "windows")
        self.assertEqual(jobs[0]["targets"], "ozoPRISM_VST3")
        self.assertEqual(jobs[0]["tests"], "OFF")
        self.assertEqual(jobs[0]["artifact"], "ozoPRISM-Windows-x64-vst3")

    def test_main_master_and_release_tags_get_full_packages(self):
        for ref in ("refs/heads/main", "refs/heads/master", "refs/tags/v1.1.1"):
            with self.subTest(ref=ref):
                jobs = ci_plan.build_matrix("push", ref)["include"]
                self.assertEqual([j["platform"] for j in jobs], ["windows", "macos"])
                for job in jobs:
                    targets = job["targets"].split()
                    self.assertIn("ozoPRISM_VST3", targets)
                    self.assertIn("ozoPRISM_Standalone", targets)
                    self.assertIn("ozoPRISMTests", targets)
                    self.assertIn("ozoPRISMVST3Host", targets)
                    self.assertNotIn("ozoPRISM", targets)
                    self.assertNotIn("ozoPRISMSnapshot", targets)
                    self.assertEqual(job["package"], "full")
                    self.assertEqual(job["tests"], "ON")
                    self.assertEqual("ozoPRISM_AU" in targets, job["platform"] == "macos")

    def test_manual_platform_and_package_combinations(self):
        for platform in ("windows", "macos", "both"):
            for package in ("vst3", "full"):
                with self.subTest(platform=platform, package=package):
                    jobs = ci_plan.build_matrix(
                        "workflow_dispatch", "refs/heads/main", platform, package
                    )["include"]
                    self.assertEqual(len(jobs), 2 if platform == "both" else 1)
                    for job in jobs:
                        if platform != "both":
                            self.assertEqual(job["platform"], platform)
                        self.assertEqual(job["package"], package)
                        self.assertTrue(job["artifact"].endswith("-" + package))
                        self.assertEqual(job["tests"], "ON" if package == "full" else "OFF")

    def test_invalid_dispatch_inputs_fail(self):
        for platform, package in (("linux", "vst3"), ("windows", "exe")):
            with self.subTest(platform=platform, package=package):
                with self.assertRaises(ValueError):
                    ci_plan.build_matrix("workflow_dispatch", "refs/heads/main", platform, package)


if __name__ == "__main__":
    unittest.main()
