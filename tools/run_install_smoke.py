#!/usr/bin/env python3
"""Stage-install NodalKit and build the downstream install-smoke sample."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True, env=env)


def meson_command() -> str:
    meson = shutil.which("meson")
    if meson is None:
        raise SystemExit("meson is required for install-smoke")
    return meson


def setup_or_reconfigure_build(
    meson: str, build_dir: Path, source_dir: Path, prefix: Path
) -> None:
    setup_args = [
        meson,
        "setup",
        str(build_dir),
        str(source_dir),
        f"--prefix={prefix}",
        "--libdir=lib",
    ]
    if (build_dir / "meson-info" / "meson-info.json").exists():
        run(setup_args[:2] + ["--reconfigure"] + setup_args[2:])
    else:
        run(setup_args)


def setup_downstream_build(
    meson: str, build_dir: Path, source_dir: Path, env: dict[str, str]
) -> None:
    if (build_dir / "meson-info" / "meson-info.json").exists():
        run([meson, "setup", "--wipe", str(build_dir), str(source_dir)], env=env)
    else:
        run([meson, "setup", str(build_dir), str(source_dir)], env=env)


def parse_pc(pc_path: Path) -> tuple[dict[str, str], dict[str, str]]:
    variables: dict[str, str] = {}
    fields: dict[str, str] = {}

    for raw_line in pc_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line and ":" not in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            variables[key.strip()] = expand_pc_value(value.strip(), variables)
            continue
        if ":" in line:
            key, value = line.split(":", 1)
            fields[key.strip()] = expand_pc_value(value.strip(), variables)

    return variables, fields


def expand_pc_value(value: str, variables: dict[str, str]) -> str:
    expanded = value
    changed = True
    while changed:
        changed = False
        for key, replacement in variables.items():
            token = "${" + key + "}"
            if token in expanded:
                expanded = expanded.replace(token, replacement)
                changed = True
    return expanded


def validate_install(prefix: Path, host_system: str) -> Path:
    include_header = prefix / "include" / "nk" / "platform" / "application.h"
    if not include_header.exists():
        raise SystemExit(f"missing installed header: {include_header}")

    lib_dir = prefix / "lib"
    libraries = list(lib_dir.glob("*NodalKit*"))
    if not libraries:
        raise SystemExit(f"missing installed NodalKit library under {lib_dir}")

    pc_path = lib_dir / "pkgconfig" / "nodalkit.pc"
    if not pc_path.exists():
        raise SystemExit(f"missing pkg-config metadata: {pc_path}")

    _, fields = parse_pc(pc_path)
    libs = fields.get("Libs", "")
    cflags = fields.get("Cflags", "")
    required_libs = ["-lNodalKit"]
    if host_system == "windows":
        required_libs += [
            "-luser32",
            "-lgdi32",
            "-ladvapi32",
            "-ldwmapi",
            "-lole32",
            "-lshcore",
            "-ldwrite",
            "-ld3d11",
            "-ld3dcompiler",
            "-ldxgi",
        ]
    missing_libs = [item for item in required_libs if item not in libs]
    if missing_libs:
        raise SystemExit(f"nodalkit.pc missing link flags: {', '.join(missing_libs)}")
    if "-I" not in cflags or "include" not in cflags:
        raise SystemExit("nodalkit.pc missing installed include flags")

    return pc_path


def create_pkg_config_shim(shim_dir: Path, pc_path: Path) -> None:
    shim_dir.mkdir(parents=True, exist_ok=True)
    shim = shim_dir / "pkg_config_shim.py"
    shim.write_text(
        f"""\
from pathlib import Path
import sys

PC_PATH = Path({str(pc_path)!r})

def expand(value, variables):
    changed = True
    while changed:
        changed = False
        for key, replacement in variables.items():
            token = '${{' + key + '}}'
            if token in value:
                value = value.replace(token, replacement)
                changed = True
    return value

variables = {{}}
fields = {{}}
for raw_line in PC_PATH.read_text(encoding='utf-8').splitlines():
    line = raw_line.strip()
    if not line or line.startswith('#'):
        continue
    if '=' in line and ':' not in line.split('=', 1)[0]:
        key, value = line.split('=', 1)
        variables[key.strip()] = expand(value.strip(), variables)
        continue
    if ':' in line:
        key, value = line.split(':', 1)
        fields[key.strip()] = expand(value.strip(), variables)

args = [arg for arg in sys.argv[1:] if arg not in (
    '--print-errors',
    '--short-errors',
    '--silence-errors',
)]
if '--version' in args:
    print('0.29.2')
    raise SystemExit(0)

packages = [arg for arg in args if not arg.startswith('-')]
if packages and any(package != 'nodalkit' for package in packages):
    raise SystemExit(1)

if '--exists' in args:
    raise SystemExit(0)
if '--modversion' in args:
    print(fields.get('Version', ''))
    raise SystemExit(0)
for arg in args:
    if arg.startswith('--variable='):
        print(variables.get(arg.split('=', 1)[1], ''))
        raise SystemExit(0)

parts = []
if '--cflags' in args:
    parts.append(fields.get('Cflags', ''))
if '--libs' in args:
    parts.append(fields.get('Libs', ''))
    if '--static' in args and fields.get('Libs.private'):
        parts.append(fields['Libs.private'])
print(' '.join(part for part in parts if part))
""",
        encoding="utf-8",
    )

    executable = Path(sys.executable)
    if executable.name.lower().startswith("meson"):
        runner = f'"{sys.executable}" runpython'
    else:
        runner = f'"{sys.executable}"'

    if os.name == "nt":
        (shim_dir / "pkg-config.bat").write_text(
            f"@echo off\r\n{runner} \"{shim}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper = shim_dir / "pkg-config"
        wrapper.write_text(
            f"#!/bin/sh\nexec {runner} {str(shim)!r} \"$@\"\n",
            encoding="utf-8",
        )
        wrapper.chmod(0o755)


def host_system_from_intro(build_dir: Path) -> str:
    machine_file = build_dir / "meson-info" / "intro-machines.json"
    text = machine_file.read_text(encoding="utf-8")
    if '"system": "windows"' in text:
        return "windows"
    if '"system": "darwin"' in text:
        return "darwin"
    if '"system": "linux"' in text:
        return "linux"
    return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    smoke_source_dir = source_dir / "tests" / "install_smoke"
    work_dir = args.work_dir.resolve()
    sdk_build_dir = work_dir / "sdk-build"
    prefix = work_dir / "prefix"
    downstream_build_dir = work_dir / "downstream-build"
    shim_dir = work_dir / "pkg-config-shim"

    meson = meson_command()
    work_dir.mkdir(parents=True, exist_ok=True)

    setup_or_reconfigure_build(meson, sdk_build_dir, source_dir, prefix)
    run([meson, "compile", "-C", str(sdk_build_dir)])
    run([meson, "install", "-C", str(sdk_build_dir), "--skip-subprojects=catch2"])

    host_system = host_system_from_intro(sdk_build_dir)
    pc_path = validate_install(prefix, host_system)

    env = os.environ.copy()
    env["PKG_CONFIG_PATH"] = str(pc_path.parent)
    if shutil.which("pkg-config") is None and shutil.which("pkgconf") is None:
        create_pkg_config_shim(shim_dir, pc_path)
        env["PATH"] = str(shim_dir) + os.pathsep + env.get("PATH", "")

    setup_downstream_build(meson, downstream_build_dir, smoke_source_dir, env)
    run([meson, "compile", "-C", str(downstream_build_dir)], env=env)
    run([meson, "test", "-C", str(downstream_build_dir), "--print-errorlogs"], env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
