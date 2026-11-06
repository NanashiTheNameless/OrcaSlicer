#!/usr/bin/env python3
"""Sign finished OrcaSlicer app bundles inside out, before DMG/tar creation."""

import argparse
import base64
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MACHO_MAGIC = {
    b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
    b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
}


# Every command is non-interactive; a timeout turns any unexpected keychain or
# authorization prompt into a fast failure instead of a job that hangs for hours.
TIMEOUT = 600
# Paths per codesign invocation: one process per file costs minutes on large bundles.
BATCH = 200


def run(*args, check=True):
    # Do not echo commands: security import uses a password argument.
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


def sign_apps(apps, identity, entitlements, keychain=None):
    for app in apps:
        batches = signing_batches(app)
        print(f'Signing {sum(map(len, batches))} code objects in {app}', flush=True)
        for batch in batches:
            args = ['codesign', '--force', '--options', 'runtime', '--timestamp=none',
                    '--entitlements', str(entitlements), '--sign', identity]
            if keychain:
                args += ['--keychain', str(keychain)]
            run(*args, *map(str, batch))
        run('codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app))
        print(f'Signed and verified: {app}', flush=True)


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
        # Local builds use the identity already imported by the user.
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
        try:
            run('security', 'create-keychain', '-p', keychain_password, str(keychain))
            run('security', 'set-keychain-settings', '-lut', '21600', str(keychain))
            run('security', 'unlock-keychain', '-p', keychain_password, str(keychain))
            run('security', 'import', str(p12), '-P', password, '-k', str(keychain),
                '-T', '/usr/bin/codesign')
            run('security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:',
                '-s', '-k', keychain_password, str(keychain))
            # codesign signs with the self-signed identity without trusting it, so trust
            # settings stay untouched: changing admin trust needs a SecurityAgent
            # authorization even as root, which waits forever on a CI runner. Hence
            # also no -v here, which would hide the untrusted identity.
            identities = run('security', 'find-identity', '-p', 'codesigning', str(keychain))
            if identity.encode() not in identities.stdout:
                raise ValueError('Imported macOS identity does not match the pinned certificate.')
            # Hashes and names only; it shows whether macOS rejects the identity (for example as untrusted).
            print(identities.stdout.decode(errors='replace').rstrip(), flush=True)
            sign_apps(apps, identity, args.entitlements, keychain)
        finally:
            if keychain.exists():
                run('security', 'delete-keychain', str(keychain))


def cli():
    try:
        main()
    except subprocess.CalledProcessError as error:
        # CalledProcessError's string contains the full command (and passwords).
        print(f'{error.cmd[0]} failed during macOS signing (exit {error.returncode}).', file=sys.stderr)
        # codesign's arguments and output hold no secrets, and its output names the failing path.
        if error.cmd[0] == 'codesign' and error.stderr:
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
