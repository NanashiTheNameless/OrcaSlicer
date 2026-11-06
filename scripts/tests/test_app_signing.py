"""Signing checks use disposable test identities, never publisher credentials."""

import base64
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import unittest.mock
from unittest.mock import patch


REPO = Path(__file__).resolve().parents[2]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, REPO / 'signing/scripts' / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


downloads = load_script('sign-downloads.py')
macos = load_script('sign-macos-build.py')


class DownloadSigningTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.kit = self.folder / 'kit'
        (self.kit / 'public').mkdir(parents=True)
        (self.kit / 'scripts').mkdir()
        shutil.copyfile(REPO / 'signing/scripts/sign-release.py', self.kit / 'scripts/sign-release.py')
        self.key = self.folder / 'test.key.pem'
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                        '-aes-256-cbc', '-pass', 'pass:test-password', '-out', str(self.key)],
                       check=True, capture_output=True)
        self.public_key = self.kit / 'public/release-public-key.pem'
        subprocess.run(['openssl', 'pkey', '-in', str(self.key), '-passin', 'pass:test-password',
                        '-pubout', '-out', str(self.public_key)], check=True, capture_output=True)
        self.artifact = self.folder / 'download with spaces.AppImage'
        self.artifact.write_bytes(b'finished release bytes')
        self.env = {
            'SIGNING_FILES': str(self.artifact),
            'SIGNING_OPTIONAL_FILES': str(self.folder / 'absent-helper'),
            'NN_SIGNING_KEY_PEM_BASE64': base64.b64encode(self.key.read_bytes()).decode(),
            'NN_SIGNING_PASSWORD': 'test-password',
            'RUNNER_TEMP': str(self.folder),
            'GITHUB_OUTPUT': str(self.folder / 'outputs'),
        }

    def sign(self):
        with patch.dict(os.environ, self.env), patch.object(downloads, 'ROOT', self.kit):
            downloads.main()

    def verify(self):
        return subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(self.public_key),
                               '-signature', str(self.artifact) + '.nn-signature', str(self.artifact)],
                              capture_output=True).returncode

    def test_final_bytes_verify_and_tampering_fails(self):
        self.sign()
        self.assertEqual(self.verify(), 0)
        output = Path((self.folder / 'outputs').read_text().strip().split('=', 1)[1])
        self.assertEqual([path.name for path in output.iterdir()], [self.artifact.name + '.nn-signature'])
        self.artifact.write_bytes(b'modified release bytes')
        self.assertNotEqual(self.verify(), 0)

    def test_existing_signature_cannot_be_overwritten(self):
        self.sign()
        original = Path(str(self.artifact) + '.nn-signature').read_bytes()
        with self.assertRaises(subprocess.CalledProcessError):
            self.sign()
        self.assertEqual(Path(str(self.artifact) + '.nn-signature').read_bytes(), original)

    def test_wrong_password_does_not_publish_a_signature(self):
        self.env['NN_SIGNING_PASSWORD'] = 'wrong-password'
        with self.assertRaises(subprocess.CalledProcessError):
            self.sign()
        self.assertFalse(Path(str(self.artifact) + '.nn-signature').exists())
        self.assertFalse((self.folder / 'outputs').exists())

    def test_missing_required_download_fails(self):
        with self.assertRaises(ValueError):
            downloads.download_files(str(self.folder / 'missing.AppImage'), '')

    def test_wrong_publisher_key_does_not_publish_signatures(self):
        shutil.copyfile(REPO / 'signing/public/release-public-key.pem', self.public_key)
        with self.assertRaises(subprocess.CalledProcessError):
            self.sign()
        self.assertFalse((self.folder / 'outputs').exists())


class MacosSigningOrderTest(unittest.TestCase):
    def run_ci_signer(self, failure=None, leaf=None):
        commands = []
        self.secrets = {}
        identity = (REPO / 'signing/public/certificate-sha1.txt').read_text().strip()
        # A stand-in leaf certificate whose SHA-1 is the pinned identity cannot be built, so
        # check_signer's hash is patched to compare the fake leaf bytes instead.
        leaf = leaf or b'pinned leaf'

        def tool(*args, **kwargs):
            commands.append(args)
            if args[0] == 'rcodesign':
                p12 = Path(args[args.index('--p12-file') + 1])
                password = Path(args[args.index('--p12-password-file') + 1])
                self.secrets = {'p12': p12, 'p12 bytes': p12.read_bytes(), 'password': password.read_text()}
            if args[0] == failure:
                raise subprocess.CalledProcessError(1, args, stderr=b'test failure')
            for part in args:
                if part.startswith('--extract-certificates='):
                    Path(part.split('=', 1)[1] + '0').write_bytes(leaf)
            return subprocess.CompletedProcess(args, 0, stdout=b'', stderr=b'')

        def sha1(data):
            digest = unittest.mock.Mock()
            digest.hexdigest.return_value = identity.lower() if data == b'pinned leaf' else '00' * 20
            return digest

        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / 'OrcaSlicer.app'
            binary = app / 'Contents/MacOS/orca-slicer'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'fake Mach-O')
            env = {'NN_SIGNING_MACOS_P12_BASE64': base64.b64encode(b'test-p12').decode(),
                   'NN_SIGNING_PASSWORD': 'test-password'}
            with patch.dict(os.environ, env), patch.object(macos.sys, 'platform', 'darwin'), \
                    patch.object(macos.sys, 'argv', ['sign-macos-build.py', str(app)]), \
                    patch.object(macos, 'run', side_effect=tool), \
                    patch.object(macos.hashlib, 'sha1', side_effect=sha1), \
                    patch.object(macos.sys, 'stdout', new_callable=io.StringIO):
                if failure:
                    with self.assertRaises(subprocess.CalledProcessError):
                        macos.main()
                elif leaf != b'pinned leaf':
                    with self.assertRaises(ValueError):
                        macos.main()
                else:
                    macos.main()
        # The decoded .p12 and the password file are gone once signing ends, however it ends.
        if self.secrets:
            self.assertFalse(self.secrets['p12'].parent.exists())
        return commands

    def test_ci_signing_uses_the_p12_without_keychain_or_trust(self):
        # Apple's codesign refuses the untrusted self-signed identity, and trusting it waits for a
        # SecurityAgent prompt on CI runners, so CI signs from the .p12 with rcodesign.
        commands = self.run_ci_signer()
        self.assertFalse(any(command[0] in ('sudo', 'security') for command in commands))
        sign = next(command for command in commands if command[0] == 'rcodesign')
        self.assertEqual(sign[1], 'sign')
        self.assertEqual(sign[sign.index('--timestamp-url') + 1], 'none')
        self.assertEqual(sign[sign.index('--code-signature-flags') + 1], 'runtime')
        self.assertIn('--entitlements-xml-file', sign)
        self.assertNotIn('test-password', sign)
        self.assertEqual(self.secrets['p12 bytes'], b'test-p12')
        self.assertEqual(self.secrets['password'].splitlines()[0], 'test-password')
        self.assertTrue(any(command[0] == 'codesign' and '--verify' in command for command in commands))

    def test_a_signature_from_another_certificate_fails(self):
        self.run_ci_signer(leaf=b'some other certificate')

    def test_signing_failure_removes_the_decoded_secrets(self):
        self.run_ci_signer(failure='rcodesign')

    def report(self, command):
        error = subprocess.CalledProcessError(1, command, stderr=b'error: the reason')
        with patch.object(macos, 'main', side_effect=error), \
                patch.object(macos.sys, 'stderr', new_callable=io.StringIO) as stderr, \
                self.assertRaises(SystemExit):
            macos.cli()
        return stderr.getvalue()

    def test_codesign_errors_are_shown_but_security_output_is_not(self):
        self.assertIn('error: the reason', self.report(('codesign', '--sign', 'identity', 'app')))
        self.assertIn('error: the reason', self.report(('/Users/runner/.cargo/bin/rcodesign', 'sign', 'app')))
        message = self.report(('security', 'import', 'identity.p12', '-P', 'secret-password'))
        self.assertNotIn('the reason', message)
        self.assertNotIn('secret-password', message)

    def test_commands_cannot_wait_for_input(self):
        with patch.object(macos.subprocess, 'run') as run:
            macos.run('security', 'list-keychains')
        self.assertIs(run.call_args.kwargs['stdin'], subprocess.DEVNULL)
        self.assertTrue(run.call_args.kwargs['timeout'])

    def test_nested_code_is_signed_before_its_containing_bundles(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / 'OrcaSlicer.app'
            framework = app / 'Contents/Frameworks/Helper.framework'
            binary = framework / 'Versions/A/Helper'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'fake Mach-O')
            link = framework / 'Helper'
            link.symlink_to(binary)
            plugin = framework / 'Versions/A/PlugIns/Nested.bundle'
            nested = plugin / 'Contents/MacOS/Nested'
            nested.parent.mkdir(parents=True)
            nested.write_bytes(b'\xcf\xfa\xed\xfe' + b'fake Mach-O')
            library = app / 'Contents/Frameworks/libshallow.dylib'
            library.write_bytes(b'\xca\xfe\xba\xbe' + b'fake universal Mach-O')
            resource = app / 'Contents/Resources/info.txt'
            resource.parent.mkdir()
            resource.write_text('plain resource')
            objects = macos.code_objects(app)
            self.assertLess(objects.index(binary), objects.index(framework))
            self.assertLess(objects.index(framework), objects.index(app))
            self.assertNotIn(link, objects)
            self.assertNotIn(resource, objects)
            batches = macos.signing_batches(app)
            self.assertEqual(sorted(batches[0]), sorted([binary, nested, library]))
            self.assertEqual(batches[1:], [[plugin], [framework], [app]])
            with patch.object(macos, 'BATCH', 2):
                self.assertEqual([len(batch) for batch in macos.signing_batches(app)], [2, 1, 1, 1, 1])


if __name__ == '__main__':
    unittest.main()
