#!/usr/bin/env python3
"""Packages Sly 2 VR for Windows: the full zip, or an update zip of the files changed since an earlier one.

  python3 tools/package_vr.py --build BUILD --mingw LLVM_MINGW --version 0.1.0 [--out DIR] [--notes TEXT]
                              [--update-from PREVIOUS.zip]

BUILD is the windows-cross build with -DSLY2_VR=ON (its bin/Sly2-Release.exe). The package holds bin/ (the exe and
every DLL it needs that is not part of Windows: found by following the DLLs' imports through the build, the
repository's Windows FFmpeg and LLVM/MinGW's runtime), scripts/, the .bat files (CRLF), README-FIRST.txt, licenses/,
version.txt and manifest.json (every file's SHA-256, as Update.bat checks). --update-from writes
Sly2-VR-update-<version>.zip instead, with only the files that differ from that zip's manifest.
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys, tempfile, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PKG = ROOT / 'packaging/vr'
FFMPEG_BIN = ROOT / 'Libraries/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared/bin'


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def imports(objdump, pe):
    out = subprocess.run([objdump, '-p', str(pe)], check=True, capture_output=True, text=True).stdout
    return [l.split('DLL Name:')[1].strip() for l in out.splitlines() if 'DLL Name:' in l]


def collect_dlls(objdump, exe, search):
    """The DLLs exe needs from the search folders (the others are Windows' own), following their imports."""
    index = {}
    for d in search:
        if d.is_dir():
            for f in d.iterdir():
                if f.suffix.lower() == '.dll':
                    index.setdefault(f.name.lower(), f)
    found, todo, missing = {}, [exe], set()
    while todo:
        pe = todo.pop()
        for name in imports(objdump, pe):
            key = name.lower()
            if key in found:
                continue
            if key in index:
                found[key] = index[key]
                todo.append(index[key])
            elif not (key.startswith('api-ms-win-') or key in SYSTEM):
                missing.add(name)
    if missing:
        sys.exit('DLLs not found (and not Windows\' own): ' + ', '.join(sorted(missing)))
    return sorted(found.values(), key=lambda p: p.name.lower())


SYSTEM = {n.lower() for n in ['ADVAPI32.dll', 'GDI32.dll', 'KERNEL32.dll', 'OPENGL32.dll', 'SHELL32.dll', 'USER32.dll',
                              'WINMM.dll', 'ole32.dll', 'OLEAUT32.dll', 'IMM32.dll', 'VERSION.dll', 'SETUPAPI.dll',
                              'bcrypt.dll', 'ws2_32.dll', 'secur32.dll', 'shlwapi.dll', 'dxgi.dll', 'd3d11.dll',
                              'dwmapi.dll', 'uxtheme.dll', 'cfgmgr32.dll', 'hid.dll', 'dinput8.dll', 'xinput1_4.dll',
                              'ucrtbase.dll', 'ntdll.dll', 'comdlg32.dll', 'mfplat.dll', 'mfreadwrite.dll', 'propsys.dll',
                              'DNSAPI.dll', 'DWrite.dll', 'IPHLPAPI.DLL', 'MSIMG32.dll', 'RPCRT4.dll', 'USERENV.dll',
                              'USP10.dll', 'bcryptprimitives.dll', 'd2d1.dll']}


def stage(args, dest):
    llvm = pathlib.Path(args.mingw)
    objdump = llvm / ('bin/llvm-objdump.exe' if os.name == 'nt' else 'bin/llvm-objdump')   # (LLVM/MinGW on Windows or a Mac)
    exe = pathlib.Path(args.build) / 'bin/Sly2-Release.exe'
    if not exe.is_file():
        sys.exit(f'no {exe}: build Sly2-Release with -DSLY2_VR=ON first')
    (dest / 'bin').mkdir(parents=True)
    shutil.copy2(exe, dest / 'bin')
    for dll in collect_dlls(objdump, exe, [exe.parent, FFMPEG_BIN, llvm / 'x86_64-w64-mingw32/bin']):
        shutil.copy2(dll, dest / 'bin')
    # scripts (CRLF for cmd and Notepad; PowerShell takes either)
    for src in sorted(PKG.rglob('*')):
        if src.is_dir():
            continue
        rel = src.relative_to(PKG)
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        if src.suffix.lower() in ('.bat', '.txt', '.ps1'):
            text = src.read_text(encoding='utf-8').replace('\r\n', '\n').replace('\n', '\r\n')
            out.write_bytes(text.encode('utf-8'))
        else:
            shutil.copy2(src, out)
    (dest / 'version.txt').write_bytes((args.version + '\r\n').encode())
    # licences: the repository's notices, the OpenXR loader's and LLVM's runtime
    lic = dest / 'licenses'
    shutil.copytree(ROOT / 'licenses', lic)
    shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', lic / 'THIRD_PARTY_NOTICES.md')
    for name, src in (('OpenXR-SDK-LICENSE.txt', args.openxr_license), ('LLVM-MinGW-LICENSE.txt', llvm / 'LICENSE.TXT')):
        if src and pathlib.Path(src).is_file():
            shutil.copy2(src, lic / name)
    files = []
    for f in sorted(dest.rglob('*')):
        if f.is_file():
            files.append({'path': f.relative_to(dest).as_posix().replace('/', '\\'), 'sha256': sha256(f)})
    manifest = {'package': 'Sly2-VR', 'version': args.version, 'notes': args.notes or '', 'files': files}
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=1))
    return manifest


def write_zip(src_dir, zip_path, prefix, names=None):
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(src_dir.rglob('*')):
            if not f.is_file():
                continue
            rel = f.relative_to(src_dir).as_posix()
            if names is not None and rel not in names:
                continue
            z.write(f, (prefix + '/' + rel) if prefix else rel)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--build', required=True)
    p.add_argument('--mingw', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--out', default=str(pathlib.Path.home() / 'Downloads'))
    p.add_argument('--notes', default='')
    p.add_argument('--openxr-license', default='')
    p.add_argument('--update-from', default='')
    args = p.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        dest = pathlib.Path(tmp) / f'Sly2-VR-{args.version}'
        manifest = stage(args, dest)
        if not args.update_from:
            zp = out / f'Sly2-VR-{args.version}.zip'
            write_zip(dest, zp, dest.name)
            print(f'{zp} ({zp.stat().st_size / 1e6:.1f} MB, {len(manifest["files"])} files)')
            return 0
        with zipfile.ZipFile(args.update_from) as z:
            name = next(n for n in z.namelist() if n.endswith('manifest.json'))
            old = {f['path']: f['sha256'] for f in json.loads(z.read(name))['files']}
        # (the installed copy's own files only: never what the player made)
        changed = [f for f in manifest['files'] if old.get(f['path']) != f['sha256'] and f['path'] != 'manifest.json']
        if not changed:
            sys.exit('nothing changed since ' + args.update_from)
        upd = {'package': 'Sly2-VR', 'version': args.version, 'notes': args.notes, 'files': changed}
        (dest / 'manifest.json').write_text(json.dumps(upd, indent=1))
        names = {f['path'].replace('\\', '/') for f in changed} | {'manifest.json'}
        zp = out / f'Sly2-VR-update-{args.version}.zip'
        write_zip(dest, zp, '', names)
        print(f'{zp} ({zp.stat().st_size / 1e6:.1f} MB): ' + ', '.join(f['path'] for f in changed))
    return 0


if __name__ == '__main__':
    sys.exit(main())
