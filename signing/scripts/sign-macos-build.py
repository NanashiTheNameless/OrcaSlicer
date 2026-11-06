#!/usr/bin/env python3
"""Sign finished OrcaSlicer app bundles inside out, before DMG/tar creation."""

import argparse
import base64
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MACHO_MAGIC = {
    b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
    b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
}


# Every command is non-interactive; a timeout turns any unexpected prompt into a fast
# failure instead of a job that hangs for hours.
TIMEOUT = 600
# Paths per codesign invocation: one process per file costs minutes on large bundles.
BATCH = 200


def run(*args, check=True):
    return subprocess.run(args, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          stdin=subprocess.DEVNULL, timeout=TIMEOUT)


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


def signing_batches(app):
    """Inside-out codesign batches: every loose Mach-O file, then each bundle deepest first."""
    objects = code_objects(app)
    files = [path for path in objects if path.is_file()]
    bundles = [path for path in objects if not path.is_file()]
    batches = [files[i:i + BATCH] for i in range(0, len(files), BATCH)]
    # Sibling bundles at one depth do not contain each other, so they share a batch.
    for depth in sorted({len(path.parts) for path in bundles}, reverse=True):
        batches.append([path for path in bundles if len(path.parts) == depth])
    return batches


def sign_apps(apps, identity, entitlements):
    for app in apps:
        batches = signing_batches(app)
        print(f'Signing {sum(map(len, batches))} code objects in {app}', flush=True)
        for batch in batches:
            args = ['codesign', '--force', '--options', 'runtime', '--timestamp=none',
                    '--entitlements', str(entitlements), '--sign', identity]
            run(*args, *map(str, batch))
        run('codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app))
        print(f'Signed and verified: {app}', flush=True)


def sign_apps_with_p12(apps, p12, password_file, entitlements, rcodesign):
    # Apple's codesign only signs with an identity macOS trusts, and trusting the self-signed
    # certificate on a runner needs an authorization prompt nobody can answer. rcodesign signs
    # from the .p12 itself: no keychain and no trust settings. It signs the bundle's nested
    # Mach-O files and bundles too; hardened runtime and entitlements go on the main executable.
    for app in apps:
        print(f'Signing {app}', flush=True)
        run(rcodesign, 'sign', '--p12-file', str(p12), '--p12-password-file', str(password_file),
            '--code-signature-flags', 'runtime', '--entitlements-xml-file', str(entitlements),
            '--timestamp-url', 'none', str(app))
        run('codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app))
        print(f'Signed and verified: {app}', flush=True)


def check_signer(app, identity, folder):
    """Fail unless the app's leaf signing certificate is the pinned one."""
    prefix = folder / f'{app.stem}-certificate'
    run('codesign', '--display', f'--extract-certificates={prefix}', str(app))
    leaf = Path(f'{prefix}0').read_bytes()
    if hashlib.sha1(leaf).hexdigest().upper() != identity.upper():
        raise ValueError(f'{app} is not signed by the pinned certificate.')


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
    rcodesign = os.environ.get('RCODESIGN', 'rcodesign')
    with tempfile.TemporaryDirectory(prefix='orca-signing-') as folder:
        folder = Path(folder)
        p12 = folder / 'identity.p12'
        p12.write_bytes(base64.b64decode(encoded, validate=True))
        p12.chmod(0o600)
        password_file = folder / 'password.txt'
        password_file.write_text(password + '\n')
        password_file.chmod(0o600)
        sign_apps_with_p12(apps, p12, password_file, args.entitlements, rcodesign)
        for app in apps:
            check_signer(app, identity, folder)


def cli():
    try:
        main()
    except subprocess.CalledProcessError as error:
        # CalledProcessError's string contains the full command (and passwords).
        print(f'{error.cmd[0]} failed during macOS signing (exit {error.returncode}).', file=sys.stderr)
        # Signing tools get only paths (the password is in a file), and their output names the
        # failing path.
        if Path(error.cmd[0]).name in ('codesign', 'rcodesign') and error.stderr:
            print(error.stderr.decode(errors='replace').rstrip(), file=sys.stderr)
        sys.exit(1)
    except subprocess.TimeoutExpired as error:
        print(f'{error.cmd[0]} did not finish within {TIMEOUT} s during macOS signing.', file=sys.stderr)
        sys.exit(1)
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    cli()
