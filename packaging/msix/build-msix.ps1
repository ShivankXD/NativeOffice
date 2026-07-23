# build-msix.ps1 -- package build\Release as an MSIX.
#
#   Dev/sideload build (self-signed, installable locally after trusting the cert):
#       powershell -ExecutionPolicy Bypass -File build-msix.ps1
#
#   Store build (real identity from Partner Center; Store signs it, so no cert):
#       powershell -ExecutionPolicy Bypass -File build-msix.ps1 -ForStore `
#           -IdentityName "12345ShivankXD.NativeOffice" `
#           -Publisher "CN=A1B2C3D4-...-GUID-FROM-PARTNER-CENTER" `
#           -PublisherDisplay "ShivankXD"
#
# The app version is read from src/app/main.cpp (setApplicationVersion) and
# becomes <version>.0 -- MSIX needs four parts and the Store requires .0 last.
param(
    [string]$IdentityName     = "ShivankXD.NativeOffice",
    [string]$Publisher        = "CN=NativeOffice Dev",
    [string]$PublisherDisplay = "ShivankXD",
    [switch]$ForStore,
    [string]$SdkBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64"
)
$ErrorActionPreference = "Stop"
$root    = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent   # D:\NativeOffice
$release = Join-Path $root "build\Release"
$outDir  = Join-Path $PSScriptRoot "out"
$staging = Join-Path $outDir "staging"

# -- 0. Tools ----------------------------------------------------------------
$makeappx = Join-Path $SdkBin "makeappx.exe"
$signtool = Join-Path $SdkBin "signtool.exe"
if (-not (Test-Path $makeappx)) { throw "makeappx.exe not found -- install the Windows 10/11 SDK or pass -SdkBin" }

# -- 1. Version from main.cpp ------------------------------------------------
$main = Get-Content (Join-Path $root "src\app\main.cpp") -Raw
if ($main -notmatch 'setApplicationVersion\("(\d+\.\d+\.\d+)"\)') { throw "Could not read version from main.cpp" }
$appVer  = $Matches[1]
$msixVer = "$appVer.0"
Write-Host "App version: $appVer  ->  MSIX version: $msixVer"

# -- 2. Stage the Release tree -----------------------------------------------
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force $staging | Out-Null
# Same exclusions as the release zip: md4c.lib, underscore temp files. Also
# strip pdb/ilk and (belt and braces) the offscreen QPA plugin, which must
# never ship (no font database -> tofu text) and has crept into Release before.
robocopy $release $staging /E /XF md4c.lib "_*" "*.pdb" "*.ilk" qoffscreen.dll /NJH /NJS /NDL /NFL | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# -- 3. VC++ CRT (the zip channel relies on machine-wide vc_redist; the MSIX
#      must be self-contained) -----------------------------------------------
$crt = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -ErrorAction SilentlyContinue |
       Sort-Object FullName -Descending | Select-Object -First 1
if ($null -eq $crt) { throw "VC++ redist CRT folder not found under Visual Studio 2022" }
Copy-Item (Join-Path $crt.FullName "*.dll") $staging
Write-Host "CRT bundled from: $($crt.FullName)"

# -- 4. Safety checks (mirror the zip packaging checks) ----------------------
$exes = @(Get-ChildItem $staging -Recurse -Filter "*.exe")
if ($exes.Count -ne 1 -or $exes[0].Name -ne "NativeOffice.exe" -or $exes[0].DirectoryName -ne $staging) {
    throw "Expected exactly one NativeOffice.exe at staging root, found: $($exes.FullName -join ', ')"
}
if (Get-ChildItem $staging -Recurse -Filter "qoffscreen.dll") { throw "qoffscreen.dll leaked into staging" }
$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $staging "NativeOffice.exe")))
if ($ascii -notmatch [regex]::Escape("Version $appVer")) { throw "Staged exe does not contain 'Version $appVer' -- stale build?" }

# -- 5. Manifest + assets ----------------------------------------------------
$assets = Join-Path $PSScriptRoot "Assets"
if (-not (Test-Path (Join-Path $assets "StoreLogo.png"))) {
    Write-Host "Assets missing -- generating..."
    python (Join-Path $PSScriptRoot "make_assets.py")
    if ($LASTEXITCODE -ne 0) { throw "make_assets.py failed" }
}
New-Item -ItemType Directory -Force (Join-Path $staging "Assets") | Out-Null
Copy-Item (Join-Path $assets "*.png") (Join-Path $staging "Assets")

$manifest = Get-Content (Join-Path $PSScriptRoot "AppxManifest.template.xml") -Raw
$manifest = $manifest.Replace("{{IDENTITY_NAME}}", $IdentityName).
                      Replace("{{PUBLISHER}}", $Publisher).
                      Replace("{{PUBLISHER_DISPLAY}}", $PublisherDisplay).
                      Replace("{{VERSION}}", $msixVer)
[IO.File]::WriteAllText((Join-Path $staging "AppxManifest.xml"), $manifest, (New-Object Text.UTF8Encoding $false))

# -- 6. Pack -----------------------------------------------------------------
$suffix = ""; if (-not $ForStore) { $suffix = "-dev" }
$msix = Join-Path $outDir ("NativeOffice-{0}{1}.msix" -f $appVer, $suffix)
if (Test-Path $msix) { Remove-Item $msix -Force }
& $makeappx pack /d $staging /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

# -- 7. Sign (dev builds only -- the Store signs store builds itself) ---------
if (-not $ForStore) {
    $cn = $Publisher
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $cn } | Select-Object -First 1
    if ($null -eq $cert) {
        Write-Host "Creating self-signed dev certificate '$cn'..."
        $cert = New-SelfSignedCertificate -Type Custom -Subject $cn `
            -KeyUsage DigitalSignature -FriendlyName "NativeOffice MSIX dev" `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    }
    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $msix
    if ($LASTEXITCODE -ne 0) { throw "signtool failed" }
    $cer = Join-Path $outDir "NativeOffice-dev.cer"
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    Write-Host ""
    Write-Host "Signed dev package: $msix"
    Write-Host "To install it, first trust the cert ONCE (admin PowerShell):"
    Write-Host "    Import-Certificate -FilePath `"$cer`" -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
    Write-Host "then double-click the .msix, or:  Add-AppxPackage `"$msix`""
} else {
    Write-Host ""
    Write-Host "Store package (unsigned -- the Store signs it): $msix"
}
