"""Offline packaging tests: no plugin builds or real macOS tools required."""

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import plistlib
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "package_plugins.py"
SPEC = importlib.util.spec_from_file_location("package_plugins", SCRIPT)
packager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packager)
COMMIT = "0123456789abcdef" * 2 + "01234567"
PRODUCT = "ozo PRISM"
VST_BINARY = f"{PRODUCT}.vst3/Contents/x86_64-win/{PRODUCT}.vst3"
MODULE_INFO = f"{PRODUCT}.vst3/Contents/Resources/moduleinfo.json"
MACHO = b"\xca\xfe\xba\xbe" + struct.pack(">I", 2) + bytes(56)


def pe_binary(machine=0x8664, magic=0x20B):
    """Minimal DOS, PE/COFF and PE32+ optional headers; never executed."""
    data = bytearray(0x80 + 24 + 240)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, machine, 1, 0, 0, 0, 240, 0x2022)
    struct.pack_into("<H", data, 0x98, magic)
    return bytes(data)


class PackagePluginsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="ozo packaging tests ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        previous = Path.cwd()
        os.chdir(self.root)
        self.addCleanup(os.chdir, previous)
        environment = mock.patch.dict(os.environ, {"CI": "false", "GITHUB_ACTIONS": "false"})
        environment.start()
        self.addCleanup(environment.stop)
        self.build = self.root / "build with spaces"
        self.output = self.root / "dist with spaces"
        self.release = self.build / "ozoPRISM_artefacts" / "Release"
        self.install = "Install ozo PRISM — 安装说明\n".encode("utf-8")
        (self.root / "INSTALL.txt").write_bytes(self.install)

    def write(self, path, contents):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def windows_inputs(self, full=False, module_info=True):
        files = {VST_BINARY: pe_binary()}
        if module_info:
            files[MODULE_INFO] = b'{"Name": "ozo PRISM"}\n'
        for name, contents in files.items():
            self.write(self.release / "VST3" / name, contents)
        if full:
            files[f"{PRODUCT}.exe"] = pe_binary()
            self.write(self.release / "Standalone" / f"{PRODUCT}.exe", pe_binary())
        return files

    def mac_inputs(self, full=False):
        files = {}
        bundles = [("VST3", "vst3")]
        if full:
            bundles += [("AU", "component"), ("Standalone", "app")]
        for folder, extension in bundles:
            bundle = f"{PRODUCT}.{extension}"
            executable = f"{bundle}/Contents/MacOS/{PRODUCT}"
            plist = f"{bundle}/Contents/Info.plist"
            self.write(self.release / folder / executable, MACHO).chmod(0o755)
            self.write(self.release / folder / plist, b"fake plist\n")
            files[executable] = MACHO
            files[plist] = b"fake plist\n"
            files[f"{bundle}/Contents/_CodeSignature/CodeResources"] = b"ad-hoc signature\n"
        return files

    def mac_command(self, command, **kwargs):
        self.assertEqual(kwargs, {"check": True})
        if command[0] == "lipo":
            self.assertEqual(command[2:], ["-verify_arch", "arm64", "x86_64"])
            self.assertTrue(Path(command[1]).is_file())
        elif command[0] == "ditto":
            self.assertEqual(len(command), 3)
            shutil.copytree(command[1], command[2], symlinks=True)
        elif command[0] == "codesign":
            bundle = Path(command[-1])
            self.assertTrue(bundle.is_relative_to(self.output))
            if command[1] == "--force":
                self.assertEqual(command[1:-1], ["--force", "--deep", "--sign", "-"])
                self.write(bundle / "Contents/_CodeSignature/CodeResources", b"ad-hoc signature\n")
            else:
                self.assertEqual(command[1:-1], ["--verify", "--deep", "--strict"])
                self.assertTrue((bundle / "Contents/_CodeSignature/CodeResources").is_file())
        else:
            self.fail(f"Unexpected external command: {command}")
        return subprocess.CompletedProcess(command, 0)

    def invoke(self, platform="windows", package="vst3", commit=COMMIT):
        argv = ["--platform", platform, "--package", package,
                "--build-dir", str(self.build), "--output-dir", str(self.output)]
        if commit is not None:
            argv += ["--commit", commit]
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            status = packager.main(argv)
        return status, stdout.getvalue(), stderr.getvalue()

    def assert_package(self, platform, package, files, commit=COMMIT):
        label, architecture = {"windows": ("Windows", "x64"), "macos": ("macOS", "Universal")}[platform]
        archive = self.output / f"ozoPRISM-{label}-{architecture}-{package}.zip"
        expected = dict(files)
        expected["INSTALL.txt"] = self.install
        expected["BUILD-INFO.txt"] = (
            f"platform: {platform}\narchitecture: {architecture}\npackage: {package}\ncommit: {commit or 'unknown'}\n"
        ).encode("utf-8")
        names = set(expected)
        for name in expected:
            parts = name.split("/")
            names.update("/".join(parts[:index]) + "/" for index in range(1, len(parts)))
        with zipfile.ZipFile(archive) as zipped:
            self.assertIsNone(zipped.testzip())
            self.assertEqual(set(zipped.namelist()), names)
            self.assertEqual(len(zipped.namelist()), len(names))
            for name, contents in expected.items():
                self.assertEqual(zipped.read(name), contents, name)
        checksum = archive.with_name(archive.name + ".sha256")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.assertEqual(checksum.read_text("ascii"), f"{digest}  {archive.name}\n")
        self.assertEqual(set(self.output.iterdir()), {archive, checksum})
        return archive

    def assert_failed(self, result, message):
        status, stdout, stderr = result
        self.assertEqual(status, 1, stderr)
        self.assertEqual(stdout, "")
        self.assertIn(message, stderr)
        self.assertEqual(list(self.output.iterdir()) if self.output.exists() else [], [])

    def add_decoys(self):
        for relative in (
            "ozoPRISMTests_artefacts/Release/ozoPRISMTests.exe",
            "ozoPRISMSnapshot_artefacts/Release/ozoPRISMSnapshot",
            "ozoPRISMVST3Host_artefacts/Release/ozoPRISMVST3Host",
            "ozoPRISM_artefacts/Debug/VST3/ozo PRISM.vst3/debug.bin",
            "ozoPRISM_artefacts/Release/VST3/another plugin.vst3/extra.bin",
            "ozoPRISM_artefacts/Release/Standalone/another app.exe",
            "ozoPRISM_artefacts/Release/VST3/ozo PRISM.pdb",
        ):
            self.write(self.build / relative, b"must not be packaged")

    def test_windows_vst3_exact_contents_spaces_and_checksum(self):
        files = self.windows_inputs(full=True)
        files.pop(f"{PRODUCT}.exe")
        self.add_decoys()
        with mock.patch.dict(os.environ, {"CI": "true", "SECRET_TOKEN": "not-for-build-info"}), \
                mock.patch.object(packager.subprocess, "run") as external:
            status, stdout, stderr = self.invoke()
        self.assertEqual(status, 0, stderr)
        archive = self.assert_package("windows", "vst3", files)
        self.assertEqual(stdout, f"{archive}\n{archive}.sha256\n")
        external.assert_not_called()

    def test_windows_full_exact_contents_and_checksum(self):
        files = self.windows_inputs(full=True)
        self.add_decoys()
        status, _, stderr = self.invoke(package="full")
        self.assertEqual(status, 0, stderr)
        self.assert_package("windows", "full", files)

    def test_windows_module_info_is_optional(self):
        files = self.windows_inputs(module_info=False)
        status, _, stderr = self.invoke()
        self.assertEqual(status, 0, stderr)
        self.assert_package("windows", "vst3", files)

    def test_macos_fast_and_full_exact_contents_signatures_and_checksum(self):
        all_files = self.mac_inputs(full=True)
        self.add_decoys()
        for package in ("vst3", "full"):
            with self.subTest(package=package):
                self.output = self.root / f"mac {package} output"
                files = {name: contents for name, contents in all_files.items()
                         if package == "full" or name.startswith(f"{PRODUCT}.vst3/")}
                with mock.patch.object(packager.subprocess, "run", side_effect=self.mac_command) as external:
                    status, _, stderr = self.invoke("macos", package)
                self.assertEqual(status, 0, stderr)
                self.assert_package("macos", package, files)
                commands = [call.args[0] for call in external.call_args_list]
                count = 1 if package == "vst3" else 3
                self.assertEqual([command[0] for command in commands],
                                 ["lipo"] * count + ["ditto"] * count + ["codesign"] * (2 * count))
                signed = [command[-1] for command in commands if command[:2] == ["codesign", "--force"]]
                verified = [command[-1] for command in commands if command[:2] == ["codesign", "--verify"]]
                self.assertEqual(signed, verified)
                self.assertEqual({Path(name).name for name in signed},
                                 {name.split("/")[0] for name in files})
                self.assertFalse(list(self.build.rglob("_CodeSignature")), "Must not sign build inputs")

    @unittest.skipIf(os.name == "nt", "Unix mode and symlink preservation require a Unix filesystem")
    def test_macos_zip_preserves_symlinks_and_permissions(self):
        files = self.mac_inputs()
        bundle = self.release / "VST3" / f"{PRODUCT}.vst3"
        (bundle / "Contents/MacOS" / PRODUCT).chmod(0o751)
        (bundle / "Contents/MacOS").chmod(0o750)
        (bundle / "Contents/Info.plist").chmod(0o640)
        links = {"Contents/Current": "MacOS", "Contents/binary-link": f"MacOS/{PRODUCT}",
                 "Contents/dangling": "missing target"}
        for name, target in links.items():
            (bundle / name).symlink_to(target)
            files[f"{bundle.name}/{name}"] = os.fsencode(target)
        with mock.patch.object(packager.subprocess, "run", side_effect=self.mac_command):
            status, _, stderr = self.invoke("macos")
        self.assertEqual(status, 0, stderr)
        archive = self.assert_package("macos", "vst3", files)
        with zipfile.ZipFile(archive) as zipped:
            for name in links:
                entry = zipped.getinfo(f"{bundle.name}/{name}")
                self.assertEqual(entry.create_system, 3)
                self.assertTrue(stat.S_ISLNK(entry.external_attr >> 16))
            for name, mode in ((f"Contents/MacOS/{PRODUCT}", 0o751), ("Contents/MacOS/", 0o750),
                               ("Contents/Info.plist", 0o640)):
                self.assertEqual(stat.S_IMODE(zipped.getinfo(f"{bundle.name}/{name}").external_attr >> 16), mode)

    def test_windows_rejects_missing_empty_malformed_and_wrong_arch_executables(self):
        self.windows_inputs()
        executable = self.release / "VST3" / VST_BINARY
        bad_signature = bytearray(pe_binary())
        bad_signature[0x80:0x84] = b"NOPE"
        bad_offset = bytearray(pe_binary())
        struct.pack_into("<I", bad_offset, 0x3C, 0xFFFFFFF0)
        cases = (
            (None, "Missing or empty executable"), (b"", "Missing or empty executable"),
            (b"not a binary", "Invalid PE DOS header"), (MACHO, "Invalid PE DOS header"),
            (b"MZ", "Invalid PE DOS header"), (bytes(bad_signature), "Invalid PE signature"),
            (bytes(bad_offset), "Invalid PE header offset"),
            (pe_binary()[:140], "Invalid PE header offset"),
            (pe_binary(0x14C), "Expected Windows x64"), (pe_binary(0xAA64), "Expected Windows x64"),
            (pe_binary(magic=0x10B), "Invalid PE32+ optional header"),
            (pe_binary()[:-1], "Invalid PE32+ optional header"),
        )
        for data, message in cases:
            with self.subTest(data=data, message=message):
                if executable.exists():
                    executable.unlink()
                if data is not None:
                    executable.write_bytes(data)
                self.assert_failed(self.invoke(), message)

    def test_macos_rejects_missing_empty_and_non_macho_executables(self):
        self.mac_inputs()
        executable = self.release / "VST3" / f"{PRODUCT}.vst3/Contents/MacOS/{PRODUCT}"
        for contents in (None, b"", b"\xca\xfe\xba\xbe", pe_binary(), b"bad header" * 8):
            with self.subTest(contents=contents):
                if executable.exists():
                    executable.unlink()
                if contents is not None:
                    executable.write_bytes(contents)
                with mock.patch.object(packager.subprocess, "run") as external:
                    self.assert_failed(self.invoke("macos"), "Missing or empty" if not contents else "Invalid Mach-O")
                external.assert_not_called()

    def test_rejects_missing_or_non_directory_bundle(self):
        bundle = self.release / "VST3" / f"{PRODUCT}.vst3"
        self.assert_failed(self.invoke(), "Missing bundle directory")
        self.write(bundle, pe_binary())
        self.assert_failed(self.invoke(), "Missing bundle directory")

    def test_rejects_directory_in_place_of_executable(self):
        executable = self.release / "VST3" / VST_BINARY
        executable.mkdir(parents=True)
        self.assert_failed(self.invoke(), "Missing or empty executable")

    def test_full_requires_all_additional_deliverables(self):
        self.windows_inputs()
        self.assert_failed(self.invoke(package="full"), "Missing or empty executable")
        standalone = self.release / "Standalone" / f"{PRODUCT}.exe"
        self.write(standalone, pe_binary(0xAA64))
        self.assert_failed(self.invoke(package="full"), "Expected Windows x64")
        standalone.write_bytes(b"")
        self.assert_failed(self.invoke(package="full"), "Missing or empty executable")
        # Replace the VST3 fixture with macOS files; unused Windows files still must not be selected.
        self.mac_inputs()
        with mock.patch.object(packager.subprocess, "run", side_effect=self.mac_command):
            self.assert_failed(self.invoke("macos", "full"), "Missing bundle directory")
            component = self.release / "AU" / f"{PRODUCT}.component/Contents/MacOS/{PRODUCT}"
            self.write(component, MACHO)
            self.assert_failed(self.invoke("macos", "full"), "Missing bundle directory")
            application = self.release / "Standalone" / f"{PRODUCT}.app/Contents/MacOS/{PRODUCT}"
            self.write(application, b"")
            self.assert_failed(self.invoke("macos", "full"), "Missing or empty executable")
            application.write_bytes(MACHO)
            component.write_bytes(b"not Mach-O")
            self.assert_failed(self.invoke("macos", "full"), "Invalid Mach-O header")

    def test_macos_requires_both_architectures_on_every_bundle(self):
        self.mac_inputs(full=True)
        for missing in ("arm64", "x86_64"):
            for extension in ("vst3", "component", "app"):
                with self.subTest(missing=missing, extension=extension):
                    def command(args, **kwargs):
                        if args[0] == "lipo" and f".{extension}/" in Path(args[1]).as_posix():
                            self.assertEqual(args[2:], ["-verify_arch", "arm64", "x86_64"])
                            raise subprocess.CalledProcessError(1, args, stderr=f"missing {missing}")
                        return self.mac_command(args, **kwargs)
                    with mock.patch.object(packager.subprocess, "run", side_effect=command):
                        self.assert_failed(self.invoke("macos", "full"), "lipo")

    def test_mac_tool_failures_preserve_inputs_and_existing_outputs(self):
        self.mac_inputs(full=True)
        existing = {
            "unrelated folder/keep.txt": b"do not delete",
            "ozoPRISM-macOS-Universal-full.zip": b"previous archive",
            "ozoPRISM-macOS-Universal-full.zip.sha256": b"previous checksum",
        }
        for name, contents in existing.items():
            self.write(self.output / name, contents)
        for fail_at in (("lipo",), ("ditto",), ("codesign", "--force"),
                        ("codesign", "--verify")):
            with self.subTest(fail_at=fail_at):
                def command(args, **kwargs):
                    if tuple(args[:len(fail_at)]) == fail_at:
                        raise subprocess.CalledProcessError(1, args)
                    return self.mac_command(args, **kwargs)
                with mock.patch.object(packager.subprocess, "run", side_effect=command):
                    status, stdout, stderr = self.invoke("macos", "full")
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn("Packaging failed:", stderr)
                self.assertEqual({path.relative_to(self.output).as_posix(): path.read_bytes()
                                  for path in self.output.rglob("*") if path.is_file()}, existing)
                self.assertEqual({path.name for path in self.output.iterdir()},
                                 {name.split("/")[0] for name in existing})
                self.assertFalse(list(self.build.rglob("_CodeSignature")))

    def test_missing_mac_tool_fails_without_fallback(self):
        self.mac_inputs()
        with mock.patch.object(packager.subprocess, "run", side_effect=FileNotFoundError("lipo unavailable")):
            self.assert_failed(self.invoke("macos"), "lipo unavailable")

    def test_archive_failure_does_not_replace_outputs_or_delete_unknown_files(self):
        self.windows_inputs()
        existing = {"keep directory/keep.txt": b"unrelated",
                    "ozoPRISM-Windows-x64-vst3.zip": b"previous archive",
                    "ozoPRISM-Windows-x64-vst3.zip.sha256": b"previous checksum"}
        for name, contents in existing.items():
            self.write(self.output / name, contents)

        def failed_zip(staging, destination):
            destination.write_bytes(b"partial archive")
            raise OSError("simulated archive failure")

        with mock.patch.object(packager, "write_zip", side_effect=failed_zip):
            status, stdout, stderr = self.invoke()
        self.assertEqual(status, 1)
        self.assertEqual(stdout, "")
        self.assertIn("simulated archive failure", stderr)
        self.assertEqual({path.relative_to(self.output).as_posix(): path.read_bytes()
                          for path in self.output.rglob("*") if path.is_file()}, existing)
        self.assertEqual({path.name for path in self.output.iterdir()},
                         {name.split("/")[0] for name in existing})
        self.assertEqual((self.release / "VST3" / VST_BINARY).read_bytes(), pe_binary())

    def test_repackaging_preserves_unknown_output_and_build_files(self):
        files = self.windows_inputs()
        self.add_decoys()
        for contents in (pe_binary(), pe_binary() + b"new build"):
            with self.subTest(size=len(contents)):
                self.write(self.output / "unrelated directory/keep.txt", b"keep")
                self.write(self.release / "VST3" / VST_BINARY, contents)
                files[VST_BINARY] = contents
                status, _, stderr = self.invoke()
                self.assertEqual(status, 0, stderr)
                self.assertEqual((self.output / "unrelated directory/keep.txt").read_bytes(), b"keep")
                self.assertEqual((self.build / "ozoPRISMTests_artefacts/Release/ozoPRISMTests.exe").read_bytes(),
                                 b"must not be packaged")
                (self.output / "unrelated directory/keep.txt").unlink()
                (self.output / "unrelated directory").rmdir()
                self.assert_package("windows", "vst3", files)

    def test_missing_install_fails(self):
        self.windows_inputs()
        (self.root / "INSTALL.txt").unlink()
        self.assert_failed(self.invoke(), "Missing installation instructions")

    def test_commit_required_in_ci_and_only_explicit_sha_is_accepted(self):
        self.windows_inputs()
        for variable in ("CI", "GITHUB_ACTIONS"):
            with self.subTest(variable=variable), mock.patch.dict(os.environ, {variable: "true"}):
                self.assert_failed(self.invoke(commit=None), "--commit is required in CI")
        for commit in ("", "not-a-sha", "123456", "f" * 65, COMMIT + "\nSECRET=value"):
            with self.subTest(commit=commit):
                self.assert_failed(self.invoke(commit=commit), "hexadecimal commit SHA")

    def test_output_cannot_be_nested_inside_bundle(self):
        self.windows_inputs()
        self.output = self.release / "VST3" / f"{PRODUCT}.vst3" / "nested output"
        self.assert_failed(self.invoke(), "Output directory must not be inside a deliverable")

    @unittest.skipUnless(sys.platform == "darwin", "Requires real Apple binary tools")
    def test_real_macos_universal_packaging_and_signature_after_extraction(self):
        source = self.write(self.root / "probe.c", b"int main(void) { return 0; }\n")
        executable = self.release / "VST3" / f"{PRODUCT}.vst3/Contents/MacOS/{PRODUCT}"
        executable.parent.mkdir(parents=True)
        subprocess.run(["xcrun", "clang", "-arch", "arm64", "-arch", "x86_64",
                        str(source), "-o", str(executable)], check=True)
        self.write(executable.parent.parent / "Info.plist", plistlib.dumps({
            "CFBundleExecutable": PRODUCT, "CFBundleIdentifier": "cn.ozo.packaging-test",
            "CFBundleName": PRODUCT, "CFBundlePackageType": "BNDL",
            "CFBundleVersion": "1.0",
        }))
        status, _, stderr = self.invoke("macos")
        self.assertEqual(status, 0, stderr)
        archive = self.output / "ozoPRISM-macOS-Universal-vst3.zip"
        extracted = self.root / "extracted"
        subprocess.run(["ditto", "-x", "-k", str(archive), str(extracted)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict",
                        str(extracted / f"{PRODUCT}.vst3")], check=True)
        subprocess.run(["lipo", str(executable), "-thin", "arm64", "-output",
                        str(self.root / "thin")], check=True)
        shutil.copy2(self.root / "thin", executable)
        with self.assertRaises(subprocess.CalledProcessError):
            packager.validate_executable(executable, "macos")

    def test_real_cli_defaults_and_optional_local_commit(self):
        self.build = self.root / "build"
        self.output = self.root / "dist"
        self.release = self.build / "ozoPRISM_artefacts" / "Release"
        files = self.windows_inputs()
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--platform", "windows", "--package", "vst3"],
                                cwd=self.root, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_package("windows", "vst3", files, commit=None)
        self.assertIn("dist/ozoPRISM-Windows-x64-vst3.zip", result.stdout.replace("\\", "/"))


if __name__ == "__main__":
    unittest.main()
