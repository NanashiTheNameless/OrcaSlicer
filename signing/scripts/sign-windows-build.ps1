[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [ValidateSet('Binaries', 'Installer')][string]$Stage = 'Binaries',
    [string]$SecretDirectory = ''
)

$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $PSScriptRoot
$thumbprint = (Get-Content "$kit/public/certificate-sha1.txt" -Raw).Trim()
$certPath = "Cert:\CurrentUser\My\$thumbprint"
$temporary = $null
$imported = $false
# Select the SDK tool for the host, including Windows ARM64 runners.
$hostArch = switch ($env:PROCESSOR_ARCHITECTURE) { 'ARM64' { 'arm64' } 'x86' { 'x86' } default { 'x64' } }
$tool = Get-ChildItem "${env:ProgramFiles(x86)}/Windows Kits/10/bin/10.*/$hostArch/signtool.exe" |
    Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $tool) { throw 'Install the Windows SDK SignTool first.' }

try {
    if ($env:NN_SIGNING_PFX_BASE64) {
        if (Test-Path $certPath) { throw 'CI requires a runner without this signing identity already installed.' }
        if (-not $env:NN_SIGNING_PASSWORD) { throw 'NN_SIGNING_PASSWORD is required.' }
        $temporary = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
        New-Item -ItemType Directory $temporary | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $temporary 'identity.pfx'), [Convert]::FromBase64String($env:NN_SIGNING_PFX_BASE64))
        [IO.File]::WriteAllText((Join-Path $temporary 'password.txt'), $env:NN_SIGNING_PASSWORD)
        $SecretDirectory = $temporary
    }
    if (-not (Test-Path $certPath)) {
        if (-not $SecretDirectory) { throw 'Import the signing identity first, or pass -SecretDirectory.' }
        # Mark before import so a partly successful import is cleaned up too.
        $imported = $true
        & "$PSScriptRoot/Import-WindowsIdentity.ps1" -SecretDirectory $SecretDirectory
    }
    if ($Stage -eq 'Binaries') {
        # CPack installs from src/Release again. Sign those originals and the
        # existing install tree used by the portable ZIP and MSIX.
        $files = @()
        foreach ($name in @('orca-slicer.exe', 'OrcaSlicer.dll')) {
            foreach ($directory in @('src/Release', 'OrcaSlicer')) {
                $path = Join-Path $BuildDir "$directory/$name"
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing application binary: $path" }
                $files += $path
            }
        }
        $validator = Join-Path $BuildDir 'src/Release/OrcaSlicer_profile_validator.exe'
        if (Test-Path -LiteralPath $validator) { $files += $validator }
        & "$PSScriptRoot/Sign-Windows.ps1" -SignToolPath $tool -FilePath $files
    } else {
        $installers = @(Get-ChildItem -LiteralPath $BuildDir -Filter 'OrcaSlicer_Windows_Installer_*.exe')
        if ($installers.Count -ne 1) { throw 'Expected exactly one finished NSIS installer.' }
        & "$PSScriptRoot/Sign-Windows.ps1" -SignToolPath $tool -FilePath $installers.FullName
        # CPack wrote this checksum before Authenticode changed the file.
        $installer = $installers[0]
        $digest = (Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText("$($installer.FullName).sha256", "$digest  $($installer.Name)`n")
    }
} finally {
    try {
        if ($imported -and (Test-Path $certPath)) { Remove-Item $certPath -DeleteKey -Force }
    } finally {
        if ($temporary -and (Test-Path $temporary)) { Remove-Item -LiteralPath $temporary -Recurse -Force }
    }
}
