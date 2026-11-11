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

Download `MipsyncHub_Windows_x64.zip` from the 1.0.0f1 release, close the old Hub,
and extract the new Hub into a separate directory or replace the old Hub files.
Run `MipsyncHub.exe`. Existing projects, editor installations and settings are
retained. Calendar-era Hubs filter out the new numbering scheme and do not
offer this first update automatically. Later 1.x Hub releases use the regular
update flow.

Projects keep their selected editor version. Select 1.0.0f1 explicitly when
upgrading a project, and keep a project backup before migration.

## Publishing

Run `scripts/release_windows.ps1 -Version <major.minor.patchfbuild> -SkipPublish` to
build and stage the Editor and Hub ZIPs, manifest and notes. Validate the staged
files, commit the matching source and manifest, and publish the release tag at
that commit. Retain previous calendar-era entries in the manifest.
