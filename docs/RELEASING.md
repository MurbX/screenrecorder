# Releasing ScreenRec

Releases are built and published by GitHub Actions (`.github/workflows/release.yml`) when a version tag
is pushed. Installed copies of ScreenRec check the latest GitHub Release once a day (Settings →
General → Updates) and install it in place with one click.

## Cut a release

1. Update `VERSION` (e.g. `1.1.0`). Use [semantic versioning](https://semver.org/):
   - patch (`1.0.1`): bug fixes only
   - minor (`1.1.0`): new features, nothing removed
   - major (`2.0.0`): breaking changes (e.g. settings or file format changes)
2. In `CHANGELOG.md`, move the `[Unreleased]` entries under a new `## [1.1.0] - YYYY-MM-DD` heading
   and update the compare links at the bottom. These notes become the release notes.
3. Commit: `git commit -am "Release 1.1.0"`, then open a PR or push to `main`. Wait for CI to pass.
4. Tag and push: `git tag v1.1.0 && git push origin v1.1.0`.
5. The Release workflow checks that the tag matches `VERSION`, builds the app, bundles the pinned FFmpeg,
   builds `ScreenRec-Setup-1.1.0.exe`, `ScreenRec-1.1.0-portable.zip` and `SHA256SUMS.txt`, and
   publishes the GitHub Release.

## Build a release locally

```powershell
powershell -ExecutionPolicy Bypass -File packaging\package.ps1
```

Output goes to `dist\`. Requires Visual Studio (C++ workload) and Inno Setup 6
(`winget install JRSoftware.InnoSetup`). Pass `-FFmpegDir <folder>` to reuse an extracted FFmpeg build
instead of downloading it.

## How upgrades work

- The installer's `AppId` (in `installer/ScreenRec.iss`) is permanent. **Never change it**, or new versions
  install side by side instead of upgrading.
- The installer closes a running ScreenRec, replaces the files and relaunches it. Settings
  (`%APPDATA%\ScreenRec\settings.ini`) and recordings are never touched.
- In-app updates download the `ScreenRec-Setup-*.exe` asset of the latest release and run it with
  `/SILENT`. Keep that asset name pattern.
- Default installs are per-user (`%LOCALAPPDATA%\Programs\ScreenRec`), so updates need no admin prompt.

## Updating the bundled FFmpeg

`packaging/package.ps1` pins the FFmpeg build by version, URL and SHA-256. To update it, change all three
together (the checksum is listed on the build's release page or computed with `Get-FileHash`), then
test recording, screenshots and the encoders (NVENC / Quick Sync / AMF / x264) before releasing.

## Code signing (recommended)

Unsigned installers trigger a Windows SmartScreen warning ("Windows protected your PC") until they
build up reputation. To sign releases, get a code-signing certificate (or use a service such as
Azure Trusted Signing / SignPath for open-source projects) and add a signing step for `ScreenRec.exe`
and the installer in `release.yml`.
