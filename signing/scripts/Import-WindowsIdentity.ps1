[CmdletBinding()]
param([string]$SecretDirectory = '')

$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $PSScriptRoot
if (-not $SecretDirectory) { $SecretDirectory = Join-Path $kit 'SECRET' }
$passwordFile = Join-Path $SecretDirectory 'password.txt'
$pfx = Join-Path $SecretDirectory 'identity.pfx'
$expected = (Get-Content -LiteralPath (Join-Path $kit 'public/certificate-sha1.txt') -Raw).Trim()
$password = ConvertTo-SecureString -String ([IO.File]::ReadAllText($passwordFile).Trim()) -AsPlainText -Force
$bundle = Get-PfxData -FilePath $pfx -Password $password
if (-not ($bundle.EndEntityCertificates | Where-Object { $_.Thumbprint -eq $expected })) {
    throw 'The PFX does not contain the expected public certificate.'
}
$certificate = Import-PfxCertificate -FilePath $pfx -Password $password -CertStoreLocation 'Cert:\CurrentUser\My'
if (-not ($certificate | Where-Object { $_.Thumbprint -eq $expected -and $_.HasPrivateKey })) {
    throw 'Imported identity does not match the expected public certificate.'
}
Write-Output "Imported signing identity: $expected"
Write-Output 'Imported into Personal only. No root or publisher trust was installed.'
