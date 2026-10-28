#!/usr/bin/env python3
"""Package an unsigned Laser MAME backend, runtimes, and matching symbols."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


def sha256(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def copy_windows_runtimes(binary, destination, runtime_dir):
    # Follow imports recursively; Windows system DLLs are supplied by the OS.
    available = {path.name.lower(): path for path in runtime_dir.glob('*.dll')}
    pending = [binary]
    copied = set()
    while pending:
        current = pending.pop()
        imports = subprocess.check_output(['objdump', '-p', str(current)], text=True)
        for name in re.findall(r'DLL Name:\s*(\S+)', imports):
            key = name.lower()
            if key in available and key not in copied:
                dependency = available[key]
                shutil.copy2(dependency, destination / dependency.name)
                copied.add(key)
                pending.append(dependency)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['macos-arm64', 'windows-x64'], required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD'], cwd=root, check=True, stdout=subprocess.DEVNULL)
    binary = root / ('laser_mame' if args.platform == 'macos-arm64' else 'laser-mame.exe')
    if not binary.is_file():
        raise FileNotFoundError(binary)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='laser-backend-') as directory:
        staging = Path(directory)
        bin_dir = staging / 'bin'
        bin_dir.mkdir()
        shutil.copy2(binary, bin_dir / binary.name)
        if args.platform == 'macos-arm64':
            dependencies = subprocess.check_output(['otool', '-L', str(binary)], text=True)
            for line in dependencies.splitlines()[1:]:
                dependency = line.strip().split(' ', 1)[0]
                if not dependency.startswith(('/usr/lib/', '/System/Library/')):
                    raise RuntimeError(f'Backend depends on a non-system library: {dependency}')
            symbols = staging / 'symbols'
            symbols.mkdir()
            subprocess.run(['dsymutil', str(binary), '-o', str(symbols / 'laser_mame.dSYM')], check=True)
            dwarf = symbols / 'laser_mame.dSYM/Contents/Resources/DWARF/laser_mame'
            if not dwarf.is_file() or not dwarf.stat().st_size:
                raise RuntimeError('Backend dSYM is missing')
        else:
            if args.runtime_dir is None:
                parser.error('--runtime-dir is required for Windows')
            copy_windows_runtimes(binary, bin_dir, args.runtime_dir)
            sections = subprocess.check_output(['objdump', '-h', str(binary)], text=True)
            if '.debug_info' not in sections:
                raise RuntimeError('Windows backend must retain its embedded DWARF symbols')
        licenses = staging / 'licenses'
        licenses.mkdir()
        shutil.copy2(root / 'COPYING', licenses / 'MAME-COPYING.txt')
        shutil.copytree(root / 'docs/legal', licenses / 'legal')
        manifest = {
            'schema': 1,
            'repository': 'sebleedelisle/mame',
            'source_sha': source_sha,
            'platform': args.platform,
            'build_variant': 'release-symbols-v1',
            'macos_minimum': '12.0' if args.platform == 'macos-arm64' else None,
            'files': {path.relative_to(staging).as_posix(): sha256(path)
                      for path in sorted(staging.rglob('*')) if path.is_file()},
        }
        (staging / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        archive = args.output_dir / f'laser-backend-{args.platform}.tar.gz'
        with tarfile.open(archive, 'w:gz') as output:
            for path in sorted(staging.iterdir()):
                output.add(path, arcname=path.name)
        archive.with_name(archive.name + '.sha256').write_text(f'{sha256(archive)}  {archive.name}\n')
        print(archive)


if __name__ == '__main__':
    main()
