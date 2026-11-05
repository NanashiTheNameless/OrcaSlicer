[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$FilePath,
    [string]$SignToolPath = 'signtool.exe',
    [string]$TimestampUrl = ''
)

$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $PSScriptRoot
$thumbprint = (Get-Content -LiteralPath (Join-Path $kit 'public/certificate-sha1.txt') -Raw).Trim()
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint"
if (-not $certificate.HasPrivateKey) { throw 'Import the private signing identity first.' }
if ($certificate.NotAfter -le (Get-Date)) { throw 'The signing certificate has expired.' }
foreach ($file in $FilePath) {
    $resolved = (Resolve-Path -LiteralPath $file).ProviderPath
    if (-not [IO.File]::Exists($resolved)) { throw "Not a regular file: $resolved" }
    $signArgs = @('sign', '/s', 'My', '/sha1', $thumbprint, '/fd', 'SHA256')
    if ($TimestampUrl) { $signArgs += @('/tr', $TimestampUrl, '/td', 'SHA256') }
    $signArgs += $resolved
    & $SignToolPath @signArgs
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed for: $resolved" }
    $signature = Get-AuthenticodeSignature -LiteralPath $resolved
    if (-not $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $thumbprint) {
        throw "The resulting signature has an unexpected signer: $resolved"
    }
    Write-Output "Signed: $resolved (Windows trust status: $($signature.Status))"
}
