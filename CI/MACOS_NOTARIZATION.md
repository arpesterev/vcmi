# macOS signing and notarization in this fork

The `Platform Builds (manual)` workflow builds `mac-intel` and `mac-arm`
DMGs. Signing and notarization belong to **this fork's Apple Developer
team**, not to the upstream VCMI repository.

## Optional setup

Add these repository **Actions secrets** under Settings > Secrets and
variables > Actions (never commit or paste certificate keys into a PR):

| Secret | Contents |
| --- | --- |
| `MACOS_KEYCHAIN_PASSWORD` | Temporary CI keychain password |
| `MACOS_SIGNING_CERTIFICATE_BASE64` | Base64 of a password-protected P12 containing a **Developer ID Application** signing certificate and private key |
| `MACOS_SIGNING_CERTIFICATE_PASSWORD` | P12 export password |
| `MACOS_NOTARIZATION_APPLE_ID` | Apple ID authorized to notarize for your team |
| `MACOS_NOTARIZATION_APPLE_ID_PASSWORD` | Apple ID **app-specific password**, not your account password |
| `MACOS_NOTARIZATION_TEAM_ID` | Apple Developer Team ID |

You need Apple Developer Program membership and a Developer ID Application
certificate owned by your team. The existing macOS packaging scripts sign
the bundled executables with the Hardened Runtime and a timestamp when a
Developer ID certificate is configured.

## Manual workflow

1. Open Actions > **Platform Builds (manual)** > **Run workflow**.
2. Select `develop` and choose the `macos` platform.
3. Check **notarize_macos** only if all six repository secrets are configured.
4. Run the workflow. The two DMGs will be available as GitHub Actions
   artifacts, including a stapled Apple notarization ticket if accepted.

By default **notarize_macos is false** and no binaries are submitted to Apple.
If the checkbox is enabled with missing or incorrect signing credentials, the
macOS build fails with an explicit error. Signing certificates are never
inherited from the upstream repository when you fork it.

Other publishing, GitHub Releases, Homebrew notifications, update service
uploads and external deployment remain disabled.

Apple documentation:

- [Notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [Customizing the notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
