# App signing

Trusted pushes/dispatches on this fork's `main` and `release/*` sign Windows
executables/installers, macOS app bundles, and finished downloads on every OS
(including Flatpak and Store MSIX). PR builds stay unsigned. Missing signing
secrets fail trusted builds. The existing nightly updater key stays separate.

Set these repository Actions secrets from the signing kit:

| Secret | Contents |
| --- | --- |
| `NN_SIGNING_KEY_PEM_BASE64` | Base64 of `identity.key.pem` |
| `NN_SIGNING_PFX_BASE64` | Base64 of `identity.pfx` |
| `NN_SIGNING_MACOS_P12_BASE64` | Base64 of `identity-macos.p12` |
| `NN_SIGNING_PASSWORD` | `password.txt` without its newline |

Keep private files outside the checkout. Never print secrets into logs. Use
ephemeral signing runners; native signing imports temporary identities and
removes them afterwards. macOS CI requires passwordless sudo to set temporary
admin-domain code-signing trust, then restores the prior settings. Store MSIX
keeps its Store Publisher and receives a detached signature; Microsoft signs Store submissions.

Local Windows signing: `./signing/scripts/sign-windows-build.ps1 -BuildDir build`
before packaging, then repeat with `-Stage Installer` after CPack. Local macOS:
`python3 signing/scripts/sign-macos-build.py path/to/OrcaSlicer.app` after importing
and trusting the identity using the kit's macOS guide. For detached local signing,
use the kit's `scripts/sign-release.py` after all packaging is complete. Windows
uses an already imported identity, or an explicit `-SecretDirectory` argument.

Each finished download has a binary RSA/SHA-256 `.nn-signature`. Obtain the public
key through a trusted channel, confirm `public/PUBLIC-KEY-SHA256.txt`, then verify:

```sh
openssl dgst -sha256 -verify release-public-key.pem -signature DOWNLOAD.nn-signature DOWNLOAD
```

Success prints `Verified OK`. This self-signed identity does not provide public
Windows/Apple trust or Apple notarization. It does not replace package-repository
GPG signatures or the app's updater signatures.
