#!/usr/bin/env python3
"""Package only the requested ozo PRISM Release deliverables (Python 3.9+)."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
from typing import Optional, Sequence
import zipfile


PRODUCT = "ozo PRISM"
PLATFORMS = {"windows": ("Windows", "x64"), "macos": ("macOS", "Universal")}
MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",  # Mach-O 32
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",  # Mach-O 64
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",  # Universal 32
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",  # Universal 64
}


class PackagingError(Exception):
    pass


def validate_executable(executable: Path, platform: str) -> None:
    if not executable.is_file() or executable.stat().st_size == 0:
        raise PackagingError(f"Missing or empty executable: {executable}")
    with executable.open("rb") as binary:
        header = binary.read(64)
        if platform == "macos":
            if len(header) < 8 or header[:4] not in MACHO_MAGICS:
                raise PackagingError(f"Invalid Mach-O header: {executable}")
        else:
            if len(header) != 64 or header[:2] != b"MZ":
                raise PackagingError(f"Invalid PE DOS header: {executable}")
            offset = struct.unpack_from("<I", header, 0x3C)[0]
            if offset < 64 or offset > executable.stat().st_size - 24:
                raise PackagingError(f"Invalid PE header offset: {executable}")
            binary.seek(offset)
            coff = binary.read(24)
            if len(coff) != 24 or coff[:4] != b"PE\0\0":
                raise PackagingError(f"Invalid PE signature: {executable}")
            if struct.unpack_from("<H", coff, 4)[0] != 0x8664:
                raise PackagingError(f"Expected Windows x64 PE executable: {executable}")
            optional_size = struct.unpack_from("<H", coff, 20)[0]
            optional = binary.read(optional_size)
            if optional_size < 112 or len(optional) != optional_size or optional[:2] != b"\x0b\x02":
                raise PackagingError(f"Invalid PE32+ optional header: {executable}")
    if platform == "macos":
        subprocess.run(["lipo", str(executable), "-verify_arch", "arm64", "x86_64"], check=True)


def write_zip(staging: Path, destination: Path) -> None:
    """Store relative names, Unix modes and link targets without following links."""
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        def add(path: Path) -> None:
            metadata = path.lstat()
            is_directory = stat.S_ISDIR(metadata.st_mode)
            name = path.relative_to(staging).as_posix() + ("/" if is_directory else "")
            modified = time.localtime(metadata.st_mtime)[:6]
            if modified[0] < 1980:
                modified = (1980, 1, 1, 0, 0, 0)
            elif modified[0] > 2107:
                modified = (2107, 12, 31, 23, 59, 58)
            info = zipfile.ZipInfo(name, modified)
            info.create_system = 3
            info.external_attr = (metadata.st_mode & 0xFFFF) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            if stat.S_ISLNK(metadata.st_mode):
                archive.writestr(info, os.fsencode(os.readlink(path)))
            elif is_directory:
                info.external_attr |= 0x10
                archive.writestr(info, b"")
                for child in sorted(path.iterdir()):
                    add(child)
            elif stat.S_ISREG(metadata.st_mode):
                info.file_size = metadata.st_size
                with path.open("rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target)
            else:
                raise PackagingError(f"Unsupported bundle entry: {path}")

        for entry in sorted(staging.iterdir()):
            add(entry)


def package_plugins(platform: str, package: str, build_dir: Path,
                    output_dir: Path, commit: Optional[str]) -> Path:
    in_ci = any(os.environ.get(name, "").strip().lower() not in ("", "0", "false", "no", "off")
                for name in ("CI", "GITHUB_ACTIONS"))
    if in_ci and not commit:
        raise PackagingError("--commit is required in CI")
    if commit is not None and not re.fullmatch(r"[0-9a-fA-F]{7,64}", commit):
        raise PackagingError("--commit must be a hexadecimal commit SHA (7-64 digits)")
    install = Path.cwd() / "INSTALL.txt"
    if not install.is_file():
        raise PackagingError(f"Missing installation instructions: {install}")

    release = build_dir / "ozoPRISM_artefacts" / "Release"
    sources = [release / "VST3" / f"{PRODUCT}.vst3"]
    if package == "full":
        if platform == "macos":
            sources.append(release / "AU" / f"{PRODUCT}.component")
        sources.append(release / "Standalone" / f"{PRODUCT}.{'app' if platform == 'macos' else 'exe'}")
    for source in sources:
        if source.suffix == ".exe":
            executable = source
        else:
            if not source.is_dir() or source.is_symlink():
                raise PackagingError(f"Missing bundle directory (must not be a symlink): {source}")
            executable = source / "Contents" / (
                Path("MacOS") / PRODUCT if platform == "macos"
                else Path("x86_64-win") / f"{PRODUCT}.vst3"
            )
        if output_dir.resolve().is_relative_to(source.resolve()):
            raise PackagingError(f"Output directory must not be inside a deliverable: {source}")
        validate_executable(executable, platform)

    label, architecture = PLATFORMS[platform]
    filename = f"ozoPRISM-{label}-{architecture}-{package}.zip"
    output_dir.mkdir(parents=True, exist_ok=True)
    # Only this owned temporary directory is ever removed. Existing outputs survive failures.
    with tempfile.TemporaryDirectory(prefix=".ozoPRISM-package-", dir=output_dir) as temporary:
        working = Path(temporary)
        staging = working / "staging"
        staging.mkdir()
        for source in sources:
            destination = staging / source.name
            if platform == "macos":
                subprocess.run(["ditto", str(source), str(destination)], check=True)
            elif source.is_dir():
                shutil.copytree(source, destination, symlinks=True)
            else:
                shutil.copy2(source, destination)
        shutil.copy2(install, staging / "INSTALL.txt")
        (staging / "BUILD-INFO.txt").write_bytes(
            (f"platform: {platform}\narchitecture: {architecture}\npackage: {package}\n"
             f"commit: {commit or 'unknown'}\n").encode("utf-8")
        )
        if platform == "macos":
            for source in sources:
                subprocess.run(["codesign", "--force", "--deep", "--sign", "-",
                                str(staging / source.name)], check=True)
            for source in sources:
                subprocess.run(["codesign", "--verify", "--deep", "--strict",
                                str(staging / source.name)], check=True)
        archive = working / filename
        write_zip(staging, archive)
        digest = hashlib.sha256()
        with archive.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        checksum = working / f"{filename}.sha256"
        checksum.write_text(f"{digest.hexdigest()}  {filename}\n", encoding="ascii")
        archive.replace(output_dir / filename)
        checksum.replace(output_dir / checksum.name)
    return output_dir / filename


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=PLATFORMS)
    parser.add_argument("--package", required=True, choices=("vst3", "full"))
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    parser.add_argument("--commit", help="Commit SHA; required when CI or GITHUB_ACTIONS is enabled")
    args = parser.parse_args(argv)
    try:
        archive = package_plugins(args.platform, args.package, args.build_dir, args.output_dir, args.commit)
    except (PackagingError, OSError, subprocess.CalledProcessError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        return 1
    print(archive)
    print(f"{archive}.sha256")
    return 0


if __name__ == "__main__":
    sys.exit(main())
