"""用于构建原生 NSIS 安装包的 Python API。

示例：

    from pathlib import Path

    from sindre_installer import InstallerConfig, build_installer

    result = build_installer(
        InstallerConfig(
            app_name="DemoApp",
            version="1.0.0",
            executable=Path("dist/DemoApp.exe"),
            output_dir=Path("release"),
            python_dir=Path("python"),
            python_project=Path("python"),
            source="tsinghua",
        )
    )
    print(result.installer_path)
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


_VERSION_RE = re.compile(r"^\d+(?:\.\d+){0,3}$")
_PYTHON_VERSION_RE = re.compile(r"^\d+\.\d+(?:\.\d+)?$")
_SAFE_NAME_RE = re.compile(r"^[^<>:\"/\\|?*\x00-\x1f]+$")
_CACHE_DIRS = {
    ".git",
    ".hg",
    ".svn",
    "__pycache__",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    ".tox",
    ".nox",
    ".venv",
    "venv",
}


@dataclass(frozen=True)
class SourceProfile:
    """Python 依赖源配置。

    Args:
        name: 配置名称。
        uv_index_url: uv 项目同步使用的索引地址。
        pip_index_url: uv pip 子命令使用的索引地址。
        python_mirror_url: uv 安装 Python 时使用的镜像地址，可选。
    """

    name: str
    uv_index_url: str
    pip_index_url: str
    python_mirror_url: str | None = None


SOURCE_PROFILES = {
    "official": SourceProfile(
        name="official",
        uv_index_url="https://pypi.org/simple",
        pip_index_url="https://pypi.org/simple",
    ),
    "tsinghua": SourceProfile(
        name="tsinghua",
        uv_index_url="https://pypi.tuna.tsinghua.edu.cn/simple",
        pip_index_url="https://pypi.tuna.tsinghua.edu.cn/simple",
    ),
}


@dataclass
class InstallerConfig:
    """描述一个通用 Windows 安装包。

    Args:
        app_name: 应用显示名称。
        version: 四段以内的数字版本号，例如 ``1.2.0``。
        executable: 主程序 EXE。
        output_dir: 安装包输出目录。
        payload_dir: 可选的应用目录，目录内容会被完整复制到安装目录。
        python_dir: 可选的 uv 构建 Python 目录，安装后放在 ``python`` 下。
        python_project: 可选的 pyproject 项目目录，安装后用于执行 ``uv sync``。
        requirements_file: 可选的 requirements 文件。
        python_version: 没有内置 Python 时由 uv 安装的版本。
        source: ``official``、``tsinghua`` 或 ``custom``。
        uv_index_url: 自定义 uv 索引地址。
        pip_index_url: 自定义 uv pip 索引地址。
        python_mirror_url: uv Python 发行版镜像地址。
        installer_icon: 安装器图标，缺省使用内置图标。
        license_file: 可选许可协议文本。
        payload_mode: ``auto``、``embedded`` 或 ``sidecar``。
        max_embedded_size: 自动模式下允许嵌入安装器的最大字节数。
    """

    app_name: str
    version: str
    executable: Path
    output_dir: Path

    payload_dir: Path | None = None
    python_dir: Path | None = None
    python_project: Path | None = None
    requirements_file: Path | None = None
    python_version: str | None = None

    source: str = "official"
    uv_index_url: str | None = None
    pip_index_url: str | None = None
    python_mirror_url: str | None = None

    installer_icon: Path | None = None
    license_file: Path | None = None

    payload_mode: str = "auto"
    max_embedded_size: int = 1_800_000_000


@dataclass
class BuildResult:
    """安装包构建结果。"""

    installer_path: Path
    payload_path: Path | None
    manifest_path: Path
    build_log: Path
    embedded_payload: bool


def _package_root() -> Path:
    return Path(__file__).resolve().parent


def _resource_path(*parts: str) -> Path:
    return _package_root().joinpath("libs", *parts)


def _nsis_path(path: Path) -> str:
    """将 Windows 路径转换为 NSIS 可读路径。"""

    return str(path.resolve()).replace("$", "$$").replace('"', '\\"')


def _nsis_text(value: str) -> str:
    return value.replace("$", "$$").replace('"', '\\"').replace("\r", "").replace("\n", " ")


def _ignore_copy(path: str, names: list[str]) -> set[str]:
    """过滤源目录中的版本控制、缓存和临时构建内容。"""

    ignored = {name for name in names if name in _CACHE_DIRS}
    ignored.update(name for name in names if name.endswith((".pyc", ".pyo", ".log", ".tmp")))
    return ignored


def _copy_tree_contents(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        if item.name in _CACHE_DIRS or item.suffix.lower() in {".pyc", ".pyo", ".log", ".tmp"}:
            continue
        target = destination / item.name
        if item.is_dir():
            shutil.copytree(item, target, copy_function=shutil.copy2, ignore=_ignore_copy)
        else:
            shutil.copy2(item, target)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _payload_files(payload: Path) -> list[dict[str, Any]]:
    files: list[dict[str, Any]] = []
    for path in sorted(payload.rglob("*")):
        if not path.is_file() or path.name == "manifest.json":
            continue
        files.append(
            {
                "path": path.relative_to(payload).as_posix(),
                "size": path.stat().st_size,
                "sha256": _sha256(path),
            }
        )
    return files


def _payload_size(payload: Path) -> int:
    return sum(path.stat().st_size for path in payload.rglob("*") if path.is_file())


def _resolve_profile(config: InstallerConfig) -> SourceProfile:
    if config.source in SOURCE_PROFILES:
        base = SOURCE_PROFILES[config.source]
        return SourceProfile(
            name=base.name,
            uv_index_url=config.uv_index_url or base.uv_index_url,
            pip_index_url=config.pip_index_url or base.pip_index_url,
            python_mirror_url=config.python_mirror_url or base.python_mirror_url,
        )
    if config.source == "custom":
        if not config.uv_index_url or not config.pip_index_url:
            raise ValueError("source='custom' 必须同时提供 uv_index_url 和 pip_index_url")
        return SourceProfile(
            name="custom",
            uv_index_url=config.uv_index_url,
            pip_index_url=config.pip_index_url,
            python_mirror_url=config.python_mirror_url,
        )
    raise ValueError("source 必须是 official、tsinghua 或 custom")


def validate_config(config: InstallerConfig) -> None:
    """校验安装包配置。

    Args:
        config: 待校验的安装包配置。

    Raises:
        FileNotFoundError: 输入文件或目录不存在。
        ValueError: 配置值不符合 NSIS 或打包器要求。
    """

    if not os.name == "nt":
        raise OSError("SindreInstaller 目前只能在 Windows 上调用 NSIS 构建")
    if not config.app_name or not _SAFE_NAME_RE.fullmatch(config.app_name):
        raise ValueError("app_name 不能为空，且不能包含 Windows 文件名非法字符")
    if not _VERSION_RE.fullmatch(config.version):
        raise ValueError("version 必须是数字版本号，例如 1.2.0")
    if config.executable.suffix.lower() != ".exe" or not config.executable.is_file():
        raise FileNotFoundError(f"找不到有效的 EXE：{config.executable}")
    for label, path in (
        ("payload_dir", config.payload_dir),
        ("python_dir", config.python_dir),
        ("python_project", config.python_project),
    ):
        if path is not None and not path.is_dir():
            raise FileNotFoundError(f"找不到 {label}：{path}")
    for label, path in (
        ("requirements_file", config.requirements_file),
        ("installer_icon", config.installer_icon),
        ("license_file", config.license_file),
    ):
        if path is not None and not path.is_file():
            raise FileNotFoundError(f"找不到 {label}：{path}")
    if config.payload_mode not in {"auto", "embedded", "sidecar"}:
        raise ValueError("payload_mode 必须是 auto、embedded 或 sidecar")
    if config.max_embedded_size <= 0:
        raise ValueError("max_embedded_size 必须大于 0")
    if config.python_version and not _PYTHON_VERSION_RE.fullmatch(config.python_version):
        raise ValueError("python_version 必须类似 3.12 或 3.12.1")
    if config.requirements_file and not (config.python_dir or config.python_version):
        raise ValueError("只有 requirements_file 时，必须提供 python_dir 或 python_version")
    _resolve_profile(config)


def _stage_payload(config: InstallerConfig, payload: Path, uv_path: Path) -> str:
    if config.payload_dir:
        _copy_tree_contents(config.payload_dir, payload)

    try:
        main_executable = config.executable.relative_to(config.payload_dir) if config.payload_dir else Path(config.executable.name)
    except ValueError:
        main_executable = Path(config.executable.name)

    target_exe = payload / main_executable
    if not target_exe.exists():
        target_exe.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(config.executable, target_exe)

    shutil.copy2(uv_path, payload / "uv.exe")

    python_target: Path | None = None
    if config.python_dir:
        python_target = payload / "python"
        _copy_tree_contents(config.python_dir, python_target)

    if config.python_project:
        if config.python_dir and config.python_project.resolve() == config.python_dir.resolve():
            project_target = python_target
        else:
            project_target = payload / "python_project"
            _copy_tree_contents(config.python_project, project_target)
        if project_target is not None:
            project_target.mkdir(parents=True, exist_ok=True)

    if config.requirements_file:
        requirements_target = (python_target or payload) / "requirements.txt"
        requirements_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(config.requirements_file, requirements_target)

    return main_executable.as_posix()


def _split_sidecar(payload: Path, destination: Path, chunk_size: int = 512 * 1024 * 1024) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / "payload.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=True) as stream:
        for path in sorted(payload.rglob("*")):
            if path.is_file():
                stream.write(path, path.relative_to(payload).as_posix())

    parts: list[dict[str, Any]] = []
    with archive.open("rb") as source:
        index = 1
        while True:
            chunk = source.read(chunk_size)
            if not chunk:
                break
            part_path = destination / f"payload.{index:03d}"
            part_path.write_bytes(chunk)
            parts.append(
                {
                    "name": part_path.name,
                    "size": part_path.stat().st_size,
                    "sha256": _sha256(part_path),
                }
            )
            index += 1
    archive_metadata = {
        "schema_version": 1,
        "archive": {
            "name": "payload.zip",
            "size": archive.stat().st_size,
            "sha256": _sha256(archive),
        },
        "parts": parts,
    }
    archive.unlink()
    shutil.copy2(_resource_path("templates", "extract_payload.ps1"), destination / "extract_payload.ps1")
    shutil.copy2(payload / "manifest.json", destination / "manifest.json")
    (destination / "payload.metadata.json").write_text(
        json.dumps(archive_metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (destination / ".sindre-installer-payload").write_text("1\n", encoding="utf-8")


def _render_script(
    config: InstallerConfig,
    profile: SourceProfile,
    stage: Path,
    installer_path: Path,
    main_executable: str,
    embedded: bool,
    sidecar_name: str,
    license_path: Path | None,
    icon_path: Path | None,
) -> str:
    template = _resource_path("templates", "installer.nsi.template").read_text(encoding="utf-8")
    product_key = re.sub(r"[^A-Za-z0-9_.-]+", "_", config.app_name)
    install_dir = f"$PROGRAMFILES\\{config.app_name}"

    icon_defines = ""
    if icon_path:
        uninstaller_icon = _resource_path("assets", "default-uninstall.ico")
        icon_defines = (
            f'!define MUI_ICON "{_nsis_path(icon_path)}"\n'
            f'!define MUI_UNICON "{_nsis_path(uninstaller_icon)}"'
        )
    finish_defines = (
        f'!define MUI_FINISHPAGE_RUN "$INSTDIR\\{main_executable.replace("/", "\\\\")}"\n'
        '!define MUI_FINISHPAGE_RUN_TEXT "安装完成后启动应用"'
    )
    license_page = f'!insertmacro MUI_PAGE_LICENSE "{_nsis_path(license_path)}"' if license_path else ""

    if embedded:
        payload_install = f'  File /r "{_nsis_path(stage / "payload" / "*.*")}"'
        sidecar_function = ""
    else:
        payload_install = "  Call ExtractSidecarPayload"
        sidecar_function = f'''Function ExtractSidecarPayload
  IfFileExists "$EXEDIR\\{sidecar_name}\\manifest.json" 0 sidecar_missing
  IfFileExists "$EXEDIR\\{sidecar_name}\\payload.metadata.json" 0 sidecar_missing
  StrCpy $SindreCommand '"$SYSDIR\\WindowsPowerShell\\v1.0\\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$EXEDIR\\{sidecar_name}\\extract_payload.ps1" -Destination "$INSTDIR"'
  Call RunUvCommand
  Return
sidecar_missing:
  MessageBox MB_ICONSTOP|MB_OK "找不到安装包旁路 payload：{sidecar_name}"
  Abort
FunctionEnd'''

    has_python = bool(config.python_dir or config.python_project or config.requirements_file or config.python_version)
    python_function = ""
    python_call = ""
    if has_python:
        python_lines = [
            "Function PreparePython",
            '  StrCmp $SindrePythonMirror "" python_mirror_done' if config.python_version else "",
            "  System::Call 'Kernel32::SetEnvironmentVariable(t, t) (\"UV_PYTHON_INSTALL_MIRROR\", $SindrePythonMirror).i'" if config.python_version else "",
            "python_mirror_done:" if config.python_version else "",
        ]
        if config.python_version:
            python_lines.extend(
                [
                    '  IfFileExists "$INSTDIR\\python\\python.exe" python_ready',
                    f'  StrCpy $SindreCommand \'"$INSTDIR\\uv.exe" python install {config.python_version}\'',
                    "  Call RunUvCommand",
                    f'  StrCpy $SindreCommand \'"$INSTDIR\\uv.exe" venv "$INSTDIR\\python\\.venv" --python {config.python_version}\'',
                    "  Call RunUvCommand",
                    "python_ready:",
                ]
            )
        if config.python_project:
            project_dir = "python" if config.python_dir and config.python_project.resolve() == config.python_dir.resolve() else "python_project"
            lock_flag = "--locked " if (config.python_project / "uv.lock").is_file() else ""
            python_lines.extend(
                [
                    f'  IfFileExists "$INSTDIR\\{project_dir}\\pyproject.toml" 0 python_requirements',
                    f'  StrCpy $SindreCommand \'"$INSTDIR\\uv.exe" sync --project "$INSTDIR\\{project_dir}" {lock_flag}--default-index "$SindreUvIndex"\'',
                    "  Call RunUvCommand",
                    "  Return",
                ]
            )
        python_lines.append("python_requirements:")
        if config.requirements_file:
            req_path = "python\\requirements.txt" if config.python_dir else "requirements.txt"
            python_arg = '"$INSTDIR\\python\\python.exe"' if config.python_dir else '"$INSTDIR\\python\\.venv\\Scripts\\python.exe"'
            python_lines.extend(
                [
                    f'  IfFileExists "$INSTDIR\\{req_path}" 0 python_done',
                    f'  StrCpy $SindreCommand \'"$INSTDIR\\uv.exe" pip sync --python {python_arg} "$INSTDIR\\{req_path}" --index-url "$SindrePipIndex"\'',
                    "  Call RunUvCommand",
                ]
            )
        python_lines.extend(["python_done:", "FunctionEnd"])
        python_function = "\n".join(line for line in python_lines if line != "")
        python_call = "  Call PreparePython"

    replacements = {
        "@@APP_NAME@@": _nsis_text(config.app_name),
        "@@VERSION@@": _nsis_text(config.version),
        "@@PRODUCT_KEY@@": product_key,
        "@@INSTALLER_PATH@@": _nsis_path(installer_path),
        "@@INSTALL_DIR@@": _nsis_text(install_dir),
        "@@MAIN_EXECUTABLE@@": _nsis_text(main_executable),
        "@@SOURCE_NAME@@": profile.name,
        "@@UV_INDEX_URL@@": _nsis_text(profile.uv_index_url),
        "@@PIP_INDEX_URL@@": _nsis_text(profile.pip_index_url),
        "@@PYTHON_MIRROR_URL@@": _nsis_text(profile.python_mirror_url or ""),
        "@@ICON_DEFINES@@": icon_defines,
        "@@FINISH_DEFINES@@": finish_defines,
        "@@LICENSE_PAGE@@": license_page,
        "@@SIDECAR_FUNCTION@@": sidecar_function,
        "@@PYTHON_FUNCTION@@": python_function,
        "@@PAYLOAD_INSTALL@@": payload_install,
        "@@PYTHON_CALL@@": python_call,
    }
    for token, value in replacements.items():
        template = template.replace(token, value)
    return template


def build_installer(config: InstallerConfig) -> BuildResult:
    """构建一个原生 NSIS 安装包。

    Args:
        config: 安装包配置。

    Returns:
        包含安装器、清单、日志和旁路 payload 路径的构建结果。

    Raises:
        RuntimeError: NSIS 编译失败或未生成安装器。
    """

    config = InstallerConfig(**{**config.__dict__, "executable": Path(config.executable), "output_dir": Path(config.output_dir)})
    validate_config(config)
    profile = _resolve_profile(config)
    nsis = _resource_path("nsis", "windows-x64", "makensis.exe")
    uv = _resource_path("uv", "windows-x64", "uv.exe")
    if not nsis.is_file():
        raise FileNotFoundError(f"包内缺少 makensis.exe：{nsis}")
    if not uv.is_file():
        raise FileNotFoundError(f"包内缺少 uv.exe：{uv}")

    config.output_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{config.app_name}-{config.version}-setup"
    installer_path = config.output_dir / f"{stem}.exe"
    manifest_path = config.output_dir / f"{stem}.manifest.json"
    payload_output = config.output_dir / f"{stem}-payload"
    if payload_output.exists() and (payload_output / ".sindre-installer-payload").is_file():
        shutil.rmtree(payload_output)
    elif payload_output.exists() and config.payload_mode in {"auto", "sidecar"}:
        raise FileExistsError(f"旁路 payload 目录已存在且不是本工具生成的目录：{payload_output}")

    with tempfile.TemporaryDirectory(prefix="sindre-installer-") as temporary:
        stage = Path(temporary)
        payload = stage / "payload"
        payload.mkdir()
        main_executable = _stage_payload(config, payload, uv)

        files = _payload_files(payload)
        embedded = config.payload_mode == "embedded" or (
            config.payload_mode == "auto" and _payload_size(payload) <= config.max_embedded_size
        )
        if config.payload_mode == "embedded" and _payload_size(payload) > config.max_embedded_size:
            raise ValueError("payload 超过 max_embedded_size，请使用 payload_mode='sidecar'")

        manifest = {
            "schema_version": 1,
            "app_name": config.app_name,
            "version": config.version,
            "main_executable": main_executable,
            "embedded_payload": embedded,
            "source": profile.name,
            "files": files,
        }
        (payload / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

        icon_path = config.installer_icon
        if icon_path is None:
            icon_path = _resource_path("assets", "default-install.ico")
        license_path = None
        if config.license_file:
            license_path = stage / "license.txt"
            shutil.copy2(config.license_file, license_path)

        if not embedded:
            _split_sidecar(payload, payload_output)

        script = _render_script(
            config,
            profile,
            stage,
            installer_path,
            main_executable,
            embedded,
            payload_output.name,
            license_path,
            icon_path,
        )
        script_path = stage / "installer.nsi"
        script_path.write_text(script, encoding="utf-8-sig")
        process = subprocess.run(
            [str(nsis), "/V2", "/NOCD", str(script_path)],
            cwd=stage,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )

        log_path = config.output_dir / f"{stem}.build.log"
        log_path.write_text(
            f"$ {nsis} /V2 /NOCD {script_path}\n\n{process.stdout}\n{process.stderr}",
            encoding="utf-8",
        )
        if process.returncode != 0 or not installer_path.is_file():
            raise RuntimeError(f"NSIS 编译失败，详见日志：{log_path}")

    return BuildResult(
        installer_path=installer_path,
        payload_path=payload_output if not embedded else None,
        manifest_path=manifest_path,
        build_log=log_path,
        embedded_payload=embedded,
    )


def pack_exe(executable: Path, output_dir: Path, **options: Any) -> BuildResult:
    """使用默认配置快速打包任意 EXE。"""

    executable = Path(executable)
    return build_installer(
        InstallerConfig(
            app_name=options.pop("app_name", executable.stem),
            version=options.pop("version", "1.0.0"),
            executable=executable,
            output_dir=Path(output_dir),
            **options,
        )
    )
