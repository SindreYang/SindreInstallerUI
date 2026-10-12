"""SindreInstaller API 的无网络单元测试。"""

from pathlib import Path
import unittest

from sindre_installer import InstallerConfig, SourceProfile, validate_config
from sindre_installer.api import _resolve_profile


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
