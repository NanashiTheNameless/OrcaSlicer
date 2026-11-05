#!/usr/bin/env python3
"""Sign or verify exact release-file bytes using OpenSSL and a pinned RSA key."""

import argparse
import getpass
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
SUFFIX = '.nn-signature'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openssl', default='openssl', help='OpenSSL executable')
    parser.add_argument('--key', type=Path, default=ROOT / 'SECRET/identity.key.pem')
    parser.add_argument('--public-key', type=Path, default=ROOT / 'public/release-public-key.pem')
    parser.add_argument('--password-file', type=Path, default=ROOT / 'SECRET/password.txt')
    parser.add_argument('--prompt', action='store_true', help='Prompt instead of reading the password file')
    parser.add_argument('operation', choices=('sign', 'verify'))
    parser.add_argument('files', nargs='+', type=Path)
    args = parser.parse_args()
    files = [path.resolve(strict=True) for path in args.files]
    if any(not path.is_file() for path in files):
        parser.error('Every input must be a regular file.')
    if len(set(files)) != len(files):
        parser.error('Duplicate input files are not allowed.')
    if args.operation == 'sign':
        for path in files:
            if path.name.endswith(SUFFIX):
                parser.error('Pass release files, not detached signature files.')
            if Path(str(path) + SUFFIX).exists():
                parser.error('A signature already exists for ' + str(path))
        password = getpass.getpass('Signing key password: ') if args.prompt else args.password_file.read_text().strip()
        if not password or '\n' in password or '\r' in password:
            parser.error('The password must be a single nonempty line.')
        for path in files:
            result = subprocess.run(
                [args.openssl, 'dgst', '-sha256', '-sign', str(args.key.resolve(strict=True)),
                 '-passin', 'stdin', str(path)],
                input=(password + '\n').encode(), stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, check=True,
            )
            signature = Path(str(path) + SUFFIX)
            with signature.open('xb') as output:
                output.write(result.stdout)
            print('Signed: ' + str(path))
    else:
        for path in files:
            signature = Path(str(path) + SUFFIX).resolve(strict=True)
            subprocess.run(
                [args.openssl, 'dgst', '-sha256', '-verify', str(args.public_key.resolve(strict=True)),
                 '-signature', str(signature), str(path)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
            )
            print('Verified: ' + str(path))


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError:
        print('Signing or verification failed. Check the key, password, file, and signature.', file=sys.stderr)
        sys.exit(1)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
