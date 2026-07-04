#!/usr/bin/env python3
"""
Run clang-tidy --fix (generates compile_commands.json from Bazel, then fixes).
External libs live in third_party/ or external/; we only skip those paths.
.clang-tidy excludes build artifacts and third_party/external from diagnostics.
"""
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def read_required_llvm_major(root: Path) -> str:
    version_file = root / ".llvm-version"
    if not version_file.exists():
        raise FileNotFoundError("Could not find .llvm-version in repository root.")
    version = version_file.read_text(encoding="utf-8").strip()
    if re.fullmatch(r"\d+", version) is None:
        raise ValueError("Invalid LLVM version in .llvm-version.")
    return version


def clang_tidy_major_version(binary: str) -> str | None:
    try:
        result = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    match = re.search(r"LLVM version\s+(\d+)", f"{result.stdout}\n{result.stderr}")
    return match.group(1) if match else None


def resolve_clang_tidy_binary(required_llvm_major: str) -> str | None:
    candidates: list[str] = []
    override = os.environ.get("CLANG_TIDY")
    if override:
        candidates.append(override)
    for candidate in (
        shutil.which(f"clang-tidy-{required_llvm_major}"),
        shutil.which("clang-tidy"),
    ):
        if candidate:
            candidates.append(candidate)
    for candidate in (
        Path(f"/opt/llvm-{required_llvm_major}/bin/clang-tidy"),
        Path(f"/usr/lib/llvm-{required_llvm_major}/bin/clang-tidy"),
    ):
        if candidate.exists():
            candidates.append(str(candidate))

    seen: set[str] = set()
    errors: list[str] = []
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        major = clang_tidy_major_version(candidate)
        if major == required_llvm_major:
            return candidate
        if major is not None:
            errors.append(f"{candidate} (LLVM {major})")

    if errors:
        print(
            "clang-tidy LLVM "
            f"{required_llvm_major} is required, but only found: {', '.join(errors)}",
            file=sys.stderr,
        )
    else:
        print(
            f"clang-tidy LLVM {required_llvm_major} is required, but no matching binary was found.",
            file=sys.stderr,
        )
    return None


def main() -> int:
    root = Path.cwd()
    if not (root / "MODULE.bazel").exists() and not (root / "WORKSPACE").exists():
        print("Run from repository root.", file=sys.stderr)
        return 1
    try:
        required_llvm_major = read_required_llvm_major(root)
    except (FileNotFoundError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1

    argv = [a.strip() for a in sys.argv[1:] if a.strip()]
    if "--fix" in argv:
        argv = [a for a in argv if a != "--fix"]

    gen = root / "scripts" / "generate_compile_commands.py"
    if not gen.exists():
        print("scripts/generate_compile_commands.py not found.", file=sys.stderr)
        return 1
    if subprocess.run([sys.executable, str(gen)], cwd=root).returncode != 0:
        return 1
    db_path = root / "compile_commands.json"
    if not db_path.exists():
        print("compile_commands.json not created.", file=sys.stderr)
        return 1

    def _load_db():
        content = db_path.read_text(encoding="utf-8")
        return json.loads(content)

    def ensure_db():
        try:
            return _load_db()
        except json.JSONDecodeError as e:
            print(f"Invalid JSON in {db_path}: {e}", file=sys.stderr)
            return None

    db = ensure_db()
    if db is None:
        return 1
    db_files = {Path(entry["file"]).resolve() for entry in db}
    suffixes = {".cpp", ".cxx", ".cc", ".c", ".hpp", ".hxx", ".h"}

    def skip_external_tu(p: Path) -> bool:
        s = str(p)
        return "/third_party/" in s or "/external/" in s

    if argv:
        files = [
            (root / f).resolve()
            for f in argv
            if Path(f).suffix in suffixes and not skip_external_tu(Path(f)) and (root / f).resolve() in db_files
        ]
    else:
        files = [Path(e["file"]) for e in db if not skip_external_tu(Path(e["file"]))]
    if not files:
        return 0
    clang_tidy = resolve_clang_tidy_binary(required_llvm_major)
    if clang_tidy is None:
        return 1
    return subprocess.run(
        [clang_tidy, "--fix", "-quiet", "-p", str(root)] + [str(p) for p in files],
        cwd=root,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
