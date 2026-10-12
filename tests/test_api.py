"""SindreInstaller API 的无网络单元测试。"""

from pathlib import Path
import unittest

from sindre_installer import InstallerConfig, SourceProfile, validate_config
from sindre_installer.api import _apply_config_defaults, _effective_run_mode, _resolve_profile


ROOT = Path(__file__).resolve().parents[1]
TEST_EXE = ROOT / "tools" / "nsis" / "NSIS.exe"


class ApiTests(unittest.TestCase):
    """验证配置和源选择行为。"""

    def test_official_profile(self) -> None:
        profile = _resolve_profile(
            InstallerConfig("Demo", "1.0.0", TEST_EXE, Path("F:/tmp/test-output"))
        )
        self.assertIsInstance(profile, SourceProfile)
        self.assertEqual(profile.uv_index_url, "https://pypi.org/simple")

    def test_tsinghua_profile(self) -> None:
        profile = _resolve_profile(
            InstallerConfig("Demo", "1.0.0", TEST_EXE, Path("F:/tmp/test-output"), source="tsinghua")
        )
        self.assertIn("tuna.tsinghua.edu.cn", profile.pip_index_url)

    def test_custom_profile_requires_both_indexes(self) -> None:
        with self.assertRaises(ValueError):
            _resolve_profile(
                InstallerConfig(
                    "Demo",
                    "1.0.0",
                    TEST_EXE,
                    Path("F:/tmp/test-output"),
                    source="custom",
                    uv_index_url="https://example.com/uv/simple",
                )
            )

    def test_config_defaults_are_inferred(self) -> None:
        config = _apply_config_defaults(
            InstallerConfig(payload_dir=Path("F:/Demo/Release"))
        )
        self.assertEqual(config.app_name, "Release")
        self.assertEqual(config.version, "1.0.0")
        self.assertEqual(config.output_dir, Path("dist"))

    def test_run_mode_follows_startup_suffix(self) -> None:
        self.assertEqual(
            _effective_run_mode(InstallerConfig("Demo", "1.0.0", TEST_EXE, Path("F:/tmp/test-output"))),
            "exe",
        )
        self.assertEqual(
            _effective_run_mode(
                InstallerConfig(
                    "Demo",
                    "1.0.0",
                    Path("F:/tmp/main.py"),
                    Path("F:/tmp/test-output"),
                )
            ),
            "uv",
        )
        self.assertEqual(
            _effective_run_mode(
                InstallerConfig("Demo", "1.0.0", None, Path("F:/tmp/test-output"), run_mode="uv")
            ),
            "uv",
        )

    def test_validate_rejects_invalid_version(self) -> None:
        with self.assertRaises(ValueError):
            validate_config(InstallerConfig("Demo", "1.0.0-beta", TEST_EXE, Path("F:/tmp/test-output")))

    def test_validate_rejects_requirements_without_python(self) -> None:
        requirements = ROOT / "README.md"
        with self.assertRaises(ValueError):
            validate_config(
                InstallerConfig(
                    "Demo",
                    "1.0.0",
                    TEST_EXE,
                    Path("F:/tmp/test-output"),
                    requirements_file=requirements,
                )
            )


if __name__ == "__main__":
    unittest.main()
