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
    def run_ci_signer(self, failure=None, empty_trust=False):
        commands = []
        identity = (REPO / 'signing/public/certificate-sha1.txt').read_bytes().strip()

        def security(*args, **kwargs):
            commands.append(args)
            operation = args[3] if args[0] == 'sudo' else args[1]
            if args[0] == 'codesign':
                operation = 'codesign'
            if operation == 'trust-settings-export':
                if failure == operation or empty_trust:
                    message = b'sudo: a password is required' if failure else b'No Trust Settings were found'
                    return subprocess.CompletedProcess(args, 1, stdout=b'', stderr=message)
                Path(args[-1]).write_text('existing trust settings')
            if operation == 'add-trusted-cert' and '-o' in args:
                Path(args[-1]).write_text('empty trust settings')
            if operation == 'trust-settings-import':
                self.restored_snapshot = Path(args[-1])
                self.restored_contents = self.restored_snapshot.read_text()
                if failure == operation:
                    self.addCleanup(shutil.rmtree, self.restored_snapshot.parent)
            if operation == failure:
                raise subprocess.CalledProcessError(1, args, stderr=b'test failure')
            if operation == 'create-keychain':
                Path(args[-1]).touch()
            return subprocess.CompletedProcess(args, 0, stdout=identity, stderr=b'')

        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / 'OrcaSlicer.app'
            binary = app / 'Contents/MacOS/orca-slicer'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'fake Mach-O')
            env = {'NN_SIGNING_MACOS_P12_BASE64': base64.b64encode(b'test-p12').decode(),
                   'NN_SIGNING_PASSWORD': 'test-password'}
            with patch.dict(os.environ, env), patch.object(macos.sys, 'platform', 'darwin'), \
                    patch.object(macos.sys, 'argv', ['sign-macos-build.py', str(app)]), \
                    patch.object(macos, 'run', side_effect=security), \
                    patch.object(macos.sys, 'stderr', new_callable=io.StringIO) as stderr:
                if failure:
                    error = RuntimeError if failure == 'trust-settings-export' else subprocess.CalledProcessError
                    with self.assertRaises(error):
                        macos.main()
                else:
                    macos.main()
        keychain = next(Path(command[-1]) for command in commands if command[:2] == ('security', 'create-keychain'))
        self.assertFalse(keychain.parent.exists())
        self.assertEqual(commands[-1][:2], ('security', 'delete-keychain'))
        return commands, stderr.getvalue()

    def test_ci_trust_changes_are_noninteractive_and_restore_the_admin_domain(self):
        commands, _ = self.run_ci_signer()
        privileged = [command for command in commands if command[0] == 'sudo']
        self.assertEqual([command[3] for command in privileged],
                         ['trust-settings-export', 'add-trusted-cert', 'trust-settings-import'])
        for command in privileged:
            self.assertEqual(command[:3], ('sudo', '-n', '/usr/bin/security'))
            self.assertIn('-d', command)
        self.assertEqual(self.restored_contents, 'existing trust settings')
        self.assertFalse(self.restored_snapshot.parent.exists())

    def test_signing_failure_restores_trust_and_removes_the_temporary_keychain(self):
        commands, _ = self.run_ci_signer(failure='codesign')
        self.assertEqual(commands[-2][:4], ('sudo', '-n', '/usr/bin/security', 'trust-settings-import'))
        self.assertEqual(self.restored_contents, 'existing trust settings')
        self.assertFalse(self.restored_snapshot.parent.exists())
        self.assertFalse(any('remove-trusted-cert' in command for command in commands))

    def test_failed_trust_installation_still_restores_the_snapshot(self):
        commands, _ = self.run_ci_signer(failure='add-trusted-cert')
        self.assertEqual(commands[-2][:4], ('sudo', '-n', '/usr/bin/security', 'trust-settings-import'))
        self.assertEqual(self.restored_contents, 'existing trust settings')
        self.assertFalse(any(command[0] == 'codesign' for command in commands))

    def test_an_empty_admin_domain_is_restored_to_empty(self):
        commands, _ = self.run_ci_signer(empty_trust=True)
        self.assertTrue(any(command[:3] == ('/usr/bin/security', 'add-trusted-cert', '-o') for command in commands))
        self.assertEqual(self.restored_contents, 'empty trust settings')
        self.assertFalse(self.restored_snapshot.parent.exists())

    def test_restore_failure_retains_only_the_recovery_snapshot(self):
        _, message = self.run_ci_signer(failure='trust-settings-import')
        self.assertEqual(self.restored_snapshot.read_text(), 'existing trust settings')
        self.assertEqual(list(self.restored_snapshot.parent.iterdir()), [self.restored_snapshot])
        self.assertIn(str(self.restored_snapshot), message)

    def test_missing_passwordless_sudo_fails_before_any_trust_change(self):
        commands, _ = self.run_ci_signer(failure='trust-settings-export')
        self.assertFalse(any('add-trusted-cert' in command for command in commands))
        self.assertFalse(any('trust-settings-import' in command for command in commands))
        self.assertFalse(any(command[0] == 'codesign' for command in commands))

    def test_nested_code_is_signed_before_its_containing_bundles(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / 'OrcaSlicer.app'
            framework = app / 'Contents/Frameworks/Helper.framework'
            binary = framework / 'Versions/A/Helper'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'fake Mach-O')
            link = framework / 'Helper'
            link.symlink_to(binary)
            resource = app / 'Contents/Resources/info.txt'
            resource.parent.mkdir()
            resource.write_text('plain resource')
            objects = macos.code_objects(app)
            self.assertLess(objects.index(binary), objects.index(framework))
            self.assertLess(objects.index(framework), objects.index(app))
            self.assertNotIn(link, objects)
            self.assertNotIn(resource, objects)


if __name__ == '__main__':
    unittest.main()
