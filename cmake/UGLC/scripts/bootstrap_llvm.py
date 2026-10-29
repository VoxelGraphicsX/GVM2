#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Iterable
from urllib.parse import unquote, urlparse
from urllib.request import Request, urlopen


class BootstrapError(RuntimeError):
    pass


def log(message: str) -> None:
    print(message, file=sys.stderr)


def log_step(message: str) -> None:
    log(f"[uglc-llvm] {message}")


def format_bytes(size_bytes: int) -> str:
    size = float(size_bytes)
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    unit = units[0]
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            break
        size /= 1024.0
    if unit == "B":
        return f"{int(size)} {unit}"
    return f"{size:.1f} {unit}"


def normalize_system(value: str) -> str:
    normalized = value.strip().lower()
    aliases = {
        "darwin": "darwin",
        "macos": "darwin",
        "macosx": "darwin",
        "linux": "linux",
        "windows": "windows",
        "win32": "windows",
        "msys": "windows",
        "cygwin": "windows"
    }
    if normalized not in aliases:
        raise BootstrapError(f"Unsupported operating system '{value}'.")
    return aliases[normalized]


def normalize_arch(value: str) -> str:
    normalized = value.strip().lower()
    aliases = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "x64": "x86_64",
        "arm64": "arm64",
        "aarch64": "arm64",
        "arm64e": "arm64",
        "arm64ec": "arm64"
    }
    if normalized not in aliases:
        raise BootstrapError(f"Unsupported architecture '{value}'.")
    return aliases[normalized]


def detect_host_system() -> str:
    return normalize_system(platform.system())


def detect_host_arch() -> str:
    return normalize_arch(platform.machine())


def load_manifest(manifest_path: Path) -> dict:
    try:
        return json.loads(manifest_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise BootstrapError(f"LLVM manifest not found: {manifest_path}") from exc
    except json.JSONDecodeError as exc:
        raise BootstrapError(f"LLVM manifest is not valid JSON: {manifest_path}: {exc}") from exc


def resolve_bundle(manifest: dict, version: str, system: str, arch: str) -> dict:
    versions = manifest.get("versions", {})
    version_entry = versions.get(version)
    if version_entry is None:
        known_versions = ", ".join(sorted(versions)) or "<none>"
        raise BootstrapError(f"LLVM version '{version}' is not defined in the manifest. Known versions: {known_versions}")

    bundle = version_entry.get(system, {}).get(arch)
    if bundle is None:
        supported_pairs = []
        for supported_system, arch_map in version_entry.items():
            for supported_arch in arch_map:
                supported_pairs.append(f"{supported_system}/{supported_arch}")
        supported_pairs.sort()
        raise BootstrapError(
            f"LLVM {version} does not define a prebuilt bundle for {system}/{arch}. "
            f"Supported host pairs: {', '.join(supported_pairs)}"
        )

    resolved = dict(bundle)
    resolved["version"] = version
    resolved["system"] = system
    resolved["arch"] = arch
    return resolved


def install_root_for(bundle: dict, cache_dir: Path) -> Path:
    return cache_dir / "toolchains" / f"llvm-{bundle['version']}-{bundle['system']}-{bundle['arch']}"


def archive_path_for(bundle: dict, cache_dir: Path) -> Path:
    url_path = unquote(urlparse(bundle["url"]).path)
    archive_name = Path(url_path).name
    if not archive_name:
        raise BootstrapError(f"Could not infer archive name from URL: {bundle['url']}")
    return cache_dir / "downloads" / archive_name


def validate_root(root: Path, system: str) -> None:
    required = [
        root / "lib" / "cmake" / "llvm" / "LLVMConfig.cmake",
        root / "lib" / "cmake" / "clang" / "ClangConfig.cmake"
    ]
    missing = [path for path in required if not path.exists()]

    if system == "windows":
        compiler_ok = (root / "bin" / "clang-cl.exe").exists() or (
            (root / "bin" / "clang.exe").exists() and (root / "bin" / "clang++.exe").exists()
        )
        if not compiler_ok:
            missing.append(root / "bin" / "clang-cl.exe")
    else:
        for compiler_name in ("clang", "clang++"):
            compiler_path = root / "bin" / compiler_name
            if not compiler_path.exists():
                missing.append(compiler_path)

    if missing:
        missing_list = ", ".join(str(path) for path in missing)
        raise BootstrapError(f"LLVM bundle at {root} is incomplete. Missing: {missing_list}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def download_archive(bundle: dict, archive_path: Path) -> None:
    ensure_parent(archive_path)
    temp_path = archive_path.with_suffix(archive_path.suffix + ".part")
    request = Request(bundle["url"], headers={"User-Agent": "UGLC LLVM bootstrap"})
    try:
        with urlopen(request) as response, temp_path.open("wb") as output:
            content_length = response.headers.get("Content-Length", "")
            expected_size = int(content_length) if content_length.isdigit() else None
            if content_length.isdigit():
                log_step(
                    f"Downloading {archive_path.name} ({format_bytes(expected_size)}) from {bundle['url']}"
                )
            else:
                log_step(f"Downloading {archive_path.name} from {bundle['url']}")

            downloaded_size = 0
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                output.write(chunk)
                downloaded_size += len(chunk)

            if expected_size is not None and downloaded_size != expected_size:
                raise BootstrapError(
                    f"Incomplete download for {archive_path.name}: "
                    f"expected {format_bytes(expected_size)}, got {format_bytes(downloaded_size)}"
                )

        temp_path.replace(archive_path)
    except Exception:
        temp_path.unlink(missing_ok=True)
        raise

    log_step(f"Downloaded archive to {archive_path}")


def verify_archive(bundle: dict, archive_path: Path) -> None:
    expected_sha = bundle.get("sha256", "")
    if not expected_sha:
        log_step(f"No SHA256 recorded for {archive_path.name}; skipping checksum verification")
        return

    if expected_sha.lower().startswith("sha256:"):
        expected_sha = expected_sha.split(":", 1)[1]

    log_step(f"Verifying SHA256 for {archive_path.name}")
    actual_sha = sha256_file(archive_path)
    if actual_sha.lower() != expected_sha.lower():
        archive_path.unlink(missing_ok=True)
        raise BootstrapError(
            f"Checksum mismatch for {archive_path.name}: expected {expected_sha}, got {actual_sha}. "
            f"Removed invalid archive from {archive_path}."
        )
    log_step(f"Verified SHA256 for {archive_path.name}")


def _safe_extract_tar(archive_path: Path, destination: Path) -> None:
    with tarfile.open(archive_path, "r:*") as archive:
        destination_root = destination.resolve()
        for member in archive.getmembers():
            member_path = (destination / member.name).resolve()
            try:
                member_path.relative_to(destination_root)
            except ValueError as exc:
                raise BootstrapError(f"Archive {archive_path} contains an unsafe path: {member.name}") from exc
        archive.extractall(destination)


def _safe_extract_zip(archive_path: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        destination_root = destination.resolve()
        for member_name in archive.namelist():
            member_path = (destination / member_name).resolve()
            try:
                member_path.relative_to(destination_root)
            except ValueError as exc:
                raise BootstrapError(f"Archive {archive_path} contains an unsafe path: {member_name}") from exc
        archive.extractall(destination)


def extract_archive(archive_path: Path, destination: Path) -> None:
    lower_name = archive_path.name.lower()
    if lower_name.endswith((".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".tar")):
        _safe_extract_tar(archive_path, destination)
        return
    if lower_name.endswith(".zip"):
        _safe_extract_zip(archive_path, destination)
        return
    raise BootstrapError(f"Unsupported archive format for {archive_path.name}")


def materialize_install_root(bundle: dict, cache_dir: Path) -> Path:
    install_root = install_root_for(bundle, cache_dir)
    if install_root.exists():
        try:
            validate_root(install_root, bundle["system"])
            log_step(f"Reusing cached LLVM toolchain at {install_root}")
            return install_root
        except BootstrapError:
            log_step(f"Removing incomplete cached LLVM toolchain at {install_root}")
            shutil.rmtree(install_root, ignore_errors=True)

    archive_path = archive_path_for(bundle, cache_dir)
    if archive_path.exists():
        log_step(f"Reusing downloaded archive {archive_path.name} ({format_bytes(archive_path.stat().st_size)})")
    else:
        download_archive(bundle, archive_path)
    verify_archive(bundle, archive_path)

    temp_root = Path(tempfile.mkdtemp(prefix="uglc-llvm-", dir=str(cache_dir)))
    try:
        extract_root = temp_root / "extract"
        extract_root.mkdir(parents=True, exist_ok=True)
        log_step(f"Extracting {archive_path.name} into temporary staging directory")
        extract_archive(archive_path, extract_root)

        strip_prefix = bundle.get("strip_prefix", "")
        staged_root = extract_root / strip_prefix if strip_prefix else extract_root
        if not staged_root.exists():
            raise BootstrapError(
                f"Expected extracted directory '{strip_prefix}' inside {archive_path.name}, but it was not found."
            )

        validate_root(staged_root, bundle["system"])
        install_root.parent.mkdir(parents=True, exist_ok=True)
        log_step(f"Installing LLVM {bundle['version']} into {install_root}")
        staged_root.rename(install_root)
        log_step(f"Installed LLVM {bundle['version']} to {install_root}")
        return install_root
    except Exception:
        if install_root.exists():
            shutil.rmtree(install_root, ignore_errors=True)
        raise
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


class CacheLock:
    def __init__(self, lock_path: Path, timeout_seconds: float) -> None:
        self.lock_path = lock_path
        self.timeout_seconds = timeout_seconds

    @staticmethod
    def _process_exists(pid: int) -> bool:
        if pid <= 0:
            return False
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True

    def _remove_stale_lock(self) -> bool:
        try:
            lock_text = self.lock_path.read_text(encoding="utf-8").strip()
        except FileNotFoundError:
            return True
        except OSError:
            return False

        try:
            lock_pid = int(lock_text)
        except ValueError:
            lock_pid = -1

        if self._process_exists(lock_pid):
            return False

        try:
            self.lock_path.unlink()
            log_step(f"Removed stale LLVM cache lock {self.lock_path}")
            return True
        except FileNotFoundError:
            return True
        except OSError:
            return False

    def __enter__(self) -> "CacheLock":
        self.lock_path.parent.mkdir(parents=True, exist_ok=True)
        deadline = time.monotonic() + self.timeout_seconds
        while True:
            try:
                file_descriptor = os.open(str(self.lock_path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(file_descriptor, str(os.getpid()).encode("utf-8"))
                os.close(file_descriptor)
                return self
            except FileExistsError:
                if self._remove_stale_lock():
                    continue
                if time.monotonic() >= deadline:
                    raise BootstrapError(f"Timed out waiting for LLVM cache lock: {self.lock_path}")
                time.sleep(0.25)

    def __exit__(self, exc_type, exc, tb) -> None:
        try:
            self.lock_path.unlink()
        except FileNotFoundError:
            pass


def bootstrap(bundle: dict, cache_dir: Path, timeout_seconds: float) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    install_root = install_root_for(bundle, cache_dir)
    if install_root.exists():
        try:
            validate_root(install_root, bundle["system"])
            log_step(f"Reusing cached LLVM toolchain at {install_root}")
            return install_root
        except BootstrapError:
            log_step(f"Removing incomplete cached LLVM toolchain at {install_root}")
            shutil.rmtree(install_root, ignore_errors=True)

    lock_path = cache_dir / "locks" / f"llvm-{bundle['version']}-{bundle['system']}-{bundle['arch']}.lock"
    log_step(f"Resolving LLVM {bundle['version']} bundle for {bundle['system']}/{bundle['arch']}")
    with CacheLock(lock_path, timeout_seconds):
        if install_root.exists():
            try:
                validate_root(install_root, bundle["system"])
                log_step(f"Reusing cached LLVM toolchain at {install_root}")
                return install_root
            except BootstrapError:
                log_step(f"Removing incomplete cached LLVM toolchain at {install_root}")
                shutil.rmtree(install_root, ignore_errors=True)
        return materialize_install_root(bundle, cache_dir)


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download and install a prebuilt LLVM toolchain for UGLC.")
    parser.add_argument("--manifest", required=True, help="Path to the LLVM bundle manifest JSON file.")
    parser.add_argument("--version", required=True, help="LLVM version to resolve from the manifest.")
    parser.add_argument("--cache-dir", required=True, help="Cache directory used for LLVM archives and installs.")
    parser.add_argument("--system", default="", help="Override host operating system for testing.")
    parser.add_argument("--arch", default="", help="Override host architecture for testing.")
    parser.add_argument("--timeout-seconds", type=float, default=600.0, help="Maximum time to wait for the cache lock.")
    parser.add_argument("--dry-run", action="store_true", help="Resolve the bundle and print the install root without downloading it.")
    parser.add_argument("--print-root", action="store_true", help="Print the resolved install root to stdout.")
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    manifest_path = Path(args.manifest).expanduser().resolve()
    cache_dir = Path(args.cache_dir).expanduser().resolve()

    system = normalize_system(args.system) if args.system else detect_host_system()
    arch = normalize_arch(args.arch) if args.arch else detect_host_arch()

    manifest = load_manifest(manifest_path)
    bundle = resolve_bundle(manifest, args.version, system, arch)

    if args.dry_run:
        install_root = install_root_for(bundle, cache_dir)
    else:
        install_root = bootstrap(bundle, cache_dir, args.timeout_seconds)

    if args.print_root:
        print(install_root)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except BootstrapError as exc:
        log(f"error: {exc}")
        raise SystemExit(1)
