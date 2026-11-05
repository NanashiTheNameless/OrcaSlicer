#!/usr/bin/env python3
"""CI adapter for the shared release signer; uploads contain signatures only."""

import base64
import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def download_files(required, optional):
    files = []
    for patterns, must_exist in ((required, True), (optional, False)):
        for pattern in patterns.splitlines():
            if not pattern.strip():
                continue
            matches = sorted(Path(path).resolve(strict=True) for path in glob.glob(pattern.strip()))
            if must_exist and not matches:
                raise ValueError(f'Missing release download: {pattern}')
            files.extend(matches)
    if not files or any(not path.is_file() for path in files):
        raise ValueError('Expected finished download files.')
    if len(set(path.name for path in files)) != len(files):
        raise ValueError('Duplicate release download names.')
    return files


def main():
    files = download_files(os.environ['SIGNING_FILES'], os.environ.get('SIGNING_OPTIONAL_FILES', ''))
    encoded = os.environ.get('NN_SIGNING_KEY_PEM_BASE64')
    password = os.environ.get('NN_SIGNING_PASSWORD')
    if not encoded or not password:
        raise ValueError('Trusted builds require NN_SIGNING_KEY_PEM_BASE64 and NN_SIGNING_PASSWORD.')
    signer = str(ROOT / 'scripts/sign-release.py')
    # The temporary private material is separate from both checkout and output.
    with tempfile.TemporaryDirectory(prefix='orca-signing-') as temporary:
        folder = Path(temporary)
        key = folder / 'identity.key.pem'
        password_file = folder / 'password.txt'
        key.write_bytes(base64.b64decode(encoded, validate=True))
        password_file.write_text(password + '\n')
        key.chmod(0o600)
        password_file.chmod(0o600)
        subprocess.run([sys.executable, signer, '--key', str(key), '--password-file', str(password_file),
                        'sign', *map(str, files)], check=True)
        subprocess.run([sys.executable, signer, 'verify', *map(str, files)], check=True)
    output = Path(tempfile.mkdtemp(prefix='orca-signatures-', dir=os.environ['RUNNER_TEMP']))
    for path in files:
        signature = Path(str(path) + '.nn-signature')
        shutil.copyfile(signature, output / signature.name)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as stream:
        stream.write(f'directory={output.as_posix()}\n')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
