"""SindreInstaller 的公开 Python API。"""

from .api import (
    BuildResult,
    InstallerConfig,
    SourceProfile,
    build_installer,
    pack_exe,
    validate_config,
)

__all__ = [
    "BuildResult",
    "InstallerConfig",
    "SourceProfile",
    "build_installer",
    "pack_exe",
    "validate_config",
]
