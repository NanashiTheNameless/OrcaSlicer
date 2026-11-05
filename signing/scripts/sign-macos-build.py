#!/usr/bin/env python3
"""Sign finished OrcaSlicer app bundles inside out, before DMG/tar creation."""

import argparse
import base64
from contextlib import contextmanager
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MACHO_MAGIC = {
    b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
    b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
}


def run(*args, check=True):
    # Do not echo commands: security import uses a password argument.
    return subprocess.run(args, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def code_objects(app):
    targets = {app}
    for path in app.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as source:
            if source.read(4) not in MACHO_MAGIC:
                continue
        targets.add(path)
        for parent in path.parents:
            if parent == app:
                break
            if parent.suffix in ('.app', '.framework', '.xpc', '.bundle'):
                targets.add(parent)
    if len(targets) == 1:
        raise ValueError(f'No Mach-O code found in {app}')
    return sorted(targets, key=lambda p: (-len(p.parts), str(p)))


def sign_apps(apps, identity, entitlements, keychain=None):
    for app in apps:
        for target in code_objects(app):
            args = ['codesign', '--force', '--options', 'runtime', '--timestamp=none',
                    '--entitlements', str(entitlements), '--sign', identity]
            if keychain:
                args += ['--keychain', str(keychain)]
            run(*args, str(target))
        run('codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app))
        print(f'Signed and verified: {app}')


@contextmanager
def temporary_signing_trust(certificate, keychain):
    # User-domain changes require GUI authentication. Root can change admin
    # trust without prompts; sudo -n fails immediately if unavailable on CI.
    admin = ('sudo', '-n', '/usr/bin/security')
    folder = Path(tempfile.mkdtemp(prefix='orca-trust-backup-'))
    backup = folder / 'trust.plist'
    retain_backup = False
    try:
        exported = run(*admin, 'trust-settings-export', '-d', str(backup), check=False)
        if exported.returncode:
            if b'No Trust Settings were found' not in exported.stderr:
                raise RuntimeError('Cannot back up macOS admin trust settings; CI signing requires passwordless sudo.')
            # File-only operation: create an empty snapshot without changing trust.
            run('/usr/bin/security', 'add-trusted-cert', '-o', str(backup))
        try:
            run(*admin, 'add-trusted-cert', '-d', '-r', 'trustRoot', '-p', 'codeSign',
                '-k', str(keychain), certificate)
            yield
        finally:
            try:
                # Import restores the snapshot directly, including removal of our
                # temporary trust. No separate removal can prevent restoration.
                run(*admin, 'trust-settings-import', '-d', str(backup))
            except (OSError, subprocess.CalledProcessError):
                retain_backup = True
                print(f'Admin trust restoration failed; recovery snapshot retained at {backup}', file=sys.stderr)
                raise
    finally:
        if not retain_backup:
            shutil.rmtree(folder)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', nargs='+', type=Path)
    parser.add_argument('--entitlements', type=Path,
                        default=ROOT.parent / 'scripts/disable_validation.entitlements')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Native app signing requires macOS.')
    apps = [app.resolve(strict=True) for app in args.apps]
    if any(app.suffix != '.app' or not app.is_dir() for app in apps):
        parser.error('Pass finished .app bundles.')
    identity = (ROOT / 'public/certificate-sha1.txt').read_text().strip()
    encoded = os.environ.get('NN_SIGNING_MACOS_P12_BASE64')
    if not encoded:
        # Local builds use the identity already imported and trusted by the user.
        sign_apps(apps, identity, args.entitlements)
        return
    password = os.environ['NN_SIGNING_PASSWORD']
    with tempfile.TemporaryDirectory(prefix='orca-signing-') as folder:
        folder = Path(folder)
        p12 = folder / 'identity.p12'
        p12.write_bytes(base64.b64decode(encoded, validate=True))
        p12.chmod(0o600)
        keychain = folder / 'signing.keychain-db'
        keychain_password = secrets.token_hex(32)
        certificate = str(ROOT / 'public/identity.cert.pem')
        try:
            run('security', 'create-keychain', '-p', keychain_password, str(keychain))
            run('security', 'set-keychain-settings', '-lut', '21600', str(keychain))
            run('security', 'unlock-keychain', '-p', keychain_password, str(keychain))
            run('security', 'import', str(p12), '-P', password, '-k', str(keychain),
                '-T', '/usr/bin/codesign')
            run('security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:',
                '-s', '-k', keychain_password, str(keychain))
            with temporary_signing_trust(certificate, keychain):
                identities = run('security', 'find-identity', '-v', '-p', 'codesigning', str(keychain))
                if identity.encode() not in identities.stdout:
                    raise ValueError('Imported macOS identity does not match the pinned certificate.')
                sign_apps(apps, identity, args.entitlements, keychain)
        finally:
            if keychain.exists():
                run('security', 'delete-keychain', str(keychain))


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        # CalledProcessError's string contains the full command (and passwords).
        print(f'{error.cmd[0]} failed during macOS signing (exit {error.returncode}).', file=sys.stderr)
        sys.exit(1)
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
