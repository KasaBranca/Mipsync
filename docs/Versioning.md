# Mipsync release versions

Mipsync 1.0.0f1 starts the `MAJOR.MINOR.PATCH[abf]BUILD` release series.
Patch releases fix issues, minor releases add compatible features, and major
releases identify changes that require migration. The preceding calendar-era
versions, such as 2026.10.18, remain available in the Hub install history.
The Hub ranks formal releases above calendar-era releases, then compares
versions numerically within each series.

The suffix identifies the release stage: `a` is alpha, `b` beta and `f` final.
The final number identifies the build, compared numerically (`f10` follows
`f2`). For example, `1.1.0a1` precedes `1.1.0b1`, which precedes `1.1.0f1`.
Use `1.0.1f1` for a patch release and `1.1.0f1` for a feature release.
Public version strings are stored in `release/version.txt`. Numeric CMake
versions and Cargo-compatible build metadata are internal packaging details.

## Upgrading a calendar-era Hub

Calendar-era Hubs only understand the `YYYY.M.D` feed version and ignore the
`[abf]BUILD` suffix, so the 1.0.0f1 feed advertises
`hub.version = v2026.11.11` while `hub.downloadUrl` still points at the
1.0.0f1 Hub asset. Existing Hubs therefore offer this first upgrade through the
regular update flow. Once on 1.0.0f1, the formal Hub treats the calendar feed
version as older and stays quiet. Later 1.x Hub releases use the standard
formal feed version and normal update flow.

Publish that bridge feed with
`scripts/release_windows.ps1 -Version 1.0.0f1 -HubManifestVersion 2026.11.11`.
Omit `-HubManifestVersion` for every later release. Existing projects, editor
installations and settings are retained across the bridge.

Projects keep their selected editor version. Select 1.0.0f1 explicitly when
upgrading a project, and keep a project backup before migration.

## Publishing

Run `scripts/release_windows.ps1 -Version <major.minor.patchfbuild> -SkipPublish` to
build and stage the Editor ZIP, portable Hub ZIP, NSIS Hub installer, manifest
and notes. The script publishes the installer as `MipsyncHub_Setup_x64.exe`, the
asset name the website downloads. Keep the portable ZIP because the in-app
updater consumes it. Validate the staged files, commit the matching source and
manifest, and publish the release tag at that commit. Retain previous
calendar-era entries in the manifest.
