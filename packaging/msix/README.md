# MSIX packaging (Microsoft Store channel)

Packages `build\Release` as an MSIX for the Microsoft Store — the Store signs
it with Microsoft's cert, so Store installs never see SmartScreen. The direct
zip/Inno channel is unaffected; the same exe serves both (it detects an MSIX
container at runtime and hands updates over to the Store).

## Files

- `AppxManifest.template.xml` — manifest; `{{...}}` placeholders filled by the
  build script. Declares `runFullTrust`, the `nativeoffice://` protocol, the
  `.noff` association, and Open-With support for docx/xlsx/pptx/csv/md/pdf/html.
- `make_assets.py` — regenerates `Assets\` from `resources\nativeoffice.ico`
  (needs Python + Pillow). Only rerun when the logo changes.
- `build-msix.ps1` — stages Release (same exclusions as the zip: `md4c.lib`,
  `_*`, plus pdbs and `qoffscreen.dll`), bundles the VC++ CRT, writes the
  manifest, packs with `makeappx`, signs dev builds.

## Dev / sideload build

    powershell -ExecutionPolicy Bypass -File build-msix.ps1

Produces `out\NativeOffice-<ver>-dev.msix` signed with a self-signed cert
(auto-created in CurrentUser\My on first run). To install it locally, trust
the exported cert once (admin PowerShell):

    Import-Certificate -FilePath out\NativeOffice-dev.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople

then double-click the .msix or `Add-AppxPackage out\NativeOffice-<ver>-dev.msix`.
Uninstall like any Store app. The dev cert never ships anywhere.

## Store build

Get the three identity values from Partner Center → Product management →
Product identity, then:

    powershell -ExecutionPolicy Bypass -File build-msix.ps1 -ForStore `
        -IdentityName "<Package/Identity/Name>" `
        -Publisher "<Package/Identity/Publisher, the CN=GUID one>" `
        -PublisherDisplay "<Publisher display name>"

Upload `out\NativeOffice-<ver>.msix` to the submission. Do NOT sign store
builds — the Store does.

## Per release

Nothing here needs editing: the version is read from `main.cpp`'s
`setApplicationVersion` and becomes `<ver>.0`. Build Release, run the script,
upload. The Store then rolls the update out to Store users itself — never
point Store users at the zip bootstrapper.
