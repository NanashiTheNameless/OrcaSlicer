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
    def run_ci_signer(self, failure=None):
        commands = []
        identity = (REPO / 'signing/public/certificate-sha1.txt').read_bytes().strip()

        def tool(*args, **kwargs):
            commands.append(args)
            operation = 'codesign' if args[0] == 'codesign' else args[1]
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
                    patch.object(macos, 'run', side_effect=tool), \
                    patch.object(macos.sys, 'stdout', new_callable=io.StringIO):
                if failure:
                    with self.assertRaises(subprocess.CalledProcessError):
                        macos.main()
                else:
                    macos.main()
        keychain = next(Path(command[-1]) for command in commands if command[:2] == ('security', 'create-keychain'))
        self.assertFalse(keychain.parent.exists())
        self.assertEqual(commands[-1][:2], ('security', 'delete-keychain'))
        return commands

    def test_ci_signing_never_changes_trust_or_needs_privileges(self):
        # Trust changes wait for a SecurityAgent prompt on CI runners and hang the job.
        commands = self.run_ci_signer()
        self.assertFalse(any(command[0] == 'sudo' for command in commands))
        self.assertFalse(any('trust' in part for command in commands for part in command))
        lookup = next(command for command in commands if command[:2] == ('security', 'find-identity'))
        self.assertNotIn('-v', lookup)
        self.assertTrue(any(command[0] == 'codesign' and '--verify' in command for command in commands))

    def test_signing_failure_removes_the_temporary_keychain(self):
        self.run_ci_signer(failure='codesign')

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
