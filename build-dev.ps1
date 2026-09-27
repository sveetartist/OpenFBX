[CmdletBinding()]
param(
    [string]$FbxSdkRoot,
    [switch]$Run,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    & "$PSScriptRoot/build-release.ps1" -FbxSdkRoot $FbxSdkRoot
    $buildArguments = @('--build', '--preset', 'release', '--parallel')
    if (-not $Test) { $buildArguments += @('--target', 'openfbx') }
    & cmake @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Development build failed (exit $LASTEXITCODE)." }
    if ($Test) {
        & ctest --preset dev
        if ($LASTEXITCODE -ne 0) { throw "Development tests failed (exit $LASTEXITCODE)." }
    }
    $appPath = Join-Path $PSScriptRoot 'build/openfbx.exe'
    Write-Host "Development app ready: $appPath" -ForegroundColor Green
    if ($Run) { Start-Process -FilePath $appPath -WorkingDirectory (Split-Path -Parent $appPath) }
}
catch {
    Write-Host "Development build failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally { Pop-Location }
