#!/usr/bin/env python3
"""Package NIGHTSHIFT bundles without including caches or changing installed plugins."""
import argparse
import hashlib
import json
import plistlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

from package_plugins import write_zip, validate_executable

PRODUCT = "ozo NIGHTSHIFT"
ROOT = Path(__file__).resolve().parents[1]


def verify_bundle(bundle, platform):
    if not bundle.is_dir() or bundle.is_symlink():
        raise ValueError(f"Missing bundle directory: {bundle}")
    if platform == "windows":
        executable = bundle / "Contents/x86_64-win" / f"{PRODUCT}.vst3"
        validate_executable(executable, "windows")
    else:
        info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
        if info.get("CFBundleIdentifier") != "cn.ozo.mididelay":
            raise ValueError(f"Wrong plugin identity: {bundle}")
        executable = bundle / "Contents/MacOS" / PRODUCT
        header = executable.read_bytes()[:8]
        if header[:4] != b"\xcf\xfa\xed\xfe" or struct.unpack("<I", header[4:8])[0] != 0x0100000C:
            raise ValueError(f"Expected Apple Silicon Mach-O: {executable}")
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    if executable.stat().st_size < 1024:
        raise ValueError(f"Empty executable: {executable}")
    return hashlib.sha256(executable.read_bytes()).hexdigest()


def package(platform, release_dir, output_dir, commit):
    label = "Windows-x64-VST3" if platform == "windows" else "macOS-arm64-AU-VST3"
    name = f"ozo-NIGHTSHIFT-0.3.0-{label}"
    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / f"{name}.zip"
    if destination.exists():
        raise ValueError(f"Refusing to overwrite existing package: {destination}")
    formats = [("VST3", "vst3")]
    if platform == "macos":
        formats.append(("AU", "component"))
    hashes = {}
    with tempfile.TemporaryDirectory(prefix="nightshift-package-") as temporary:
        staging = Path(temporary)
        folder = staging / name
        folder.mkdir()
        for format_name, suffix in formats:
            source = release_dir / format_name / f"{PRODUCT}.{suffix}"
            hashes[source.name] = verify_bundle(source, platform)
            target = folder / format_name / source.name
            target.parent.mkdir(exist_ok=True)
            if platform == "macos":
                subprocess.run(["ditto", str(source), str(target)], check=True)
            else:
                shutil.copytree(source, target, symlinks=True)
            if verify_bundle(target, platform) != hashes[source.name]:
                raise ValueError("Staging changed plugin binary")
        shutil.copy2(ROOT / "plugins/glint/INSTALL.txt", folder / "INSTALL.txt")
        shutil.copy2(ROOT / "plugins/glint/RELEASE_NOTES.md", folder / "RELEASE_NOTES.md")
        (folder / "BUILD-INFO.json").write_text(json.dumps({
            "product": PRODUCT, "version": "0.3.0", "platform": platform,
            "architecture": "x64" if platform == "windows" else "arm64",
            "sourceCommit": commit, "executablesSHA256": hashes,
            "validation": "Windows PE x64 header/bundle checks" if platform == "windows"
                          else "Mac bundle identity, arm64 binary and ad-hoc signature checks",
        }, indent=2) + "\n", encoding="utf-8")
        archive = staging / f"{name}.zip"
        # Keep the temporary archive outside the directory walked by write_zip.
        tree = staging / "archive-content"
        tree.mkdir()
        folder.rename(tree / name)
        write_zip(tree, archive)
        with zipfile.ZipFile(archive) as z:
            if z.testzip() is not None:
                raise ValueError("Archive CRC verification failed")
        shutil.copy2(archive, destination)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(destination.suffix + ".sha256").write_text(
        f"{digest}  {destination.name}\n", encoding="utf-8")
    print(f"Verified package: {destination} ({destination.stat().st_size:,} bytes)")
    print(f"SHA256: {digest}")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True, choices=["windows", "macos"])
    parser.add_argument("--release-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    package(args.platform, args.release_dir, args.output_dir, args.commit)
