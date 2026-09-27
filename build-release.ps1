[CmdletBinding()]
param(
    [string]$FbxSdkRoot,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    $cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
    if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
        throw 'CMake was not found. Install CMake and Visual Studio 2022 with Desktop development with C++.'
    }

    $configureArguments = @('--preset', 'release')
    if (-not $FbxSdkRoot -and -not (Test-Path -LiteralPath 'build/.cmake/CMakeCache.txt')) {
        if ($env:FBXSDK_ROOT) { $FbxSdkRoot = $env:FBXSDK_ROOT }
        elseif ($env:FBXSDK_DIR) { $FbxSdkRoot = $env:FBXSDK_DIR }
        else {
            $sdkParent = Join-Path $env:ProgramFiles 'Autodesk/FBX/FBX SDK'
            if (Test-Path -LiteralPath $sdkParent) {
                $sdk = Get-ChildItem -LiteralPath $sdkParent -Directory |
                    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'include/fbxsdk.h') } |
                    Sort-Object Name -Descending | Select-Object -First 1
                if ($sdk) { $FbxSdkRoot = $sdk.FullName }
            }
        }
        if (-not $FbxSdkRoot) {
            throw 'FBX SDK was not found. Run build-release.ps1 -FbxSdkRoot "C:/path/to/FBX SDK/version".'
        }
    }
    if ($FbxSdkRoot) {
        $sdkHeader = Join-Path $FbxSdkRoot 'include/fbxsdk.h'
        if (-not (Test-Path -LiteralPath $sdkHeader -PathType Leaf)) {
            throw "FBX SDK header not found: $sdkHeader"
        }
        $configureArguments += "-DFBXSDK_ROOT=$FbxSdkRoot"
    }

    Write-Host 'Configuring Release build...'
    & $cmakePath @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }
    Write-Host 'Building Release app... Close openfbx first if the executable is in use.'
    & $cmakePath --build --preset release --target openfbx --parallel
    if ($LASTEXITCODE -ne 0) { throw "Release build failed (exit $LASTEXITCODE)." }

    $appPath = Join-Path $PSScriptRoot 'build/openfbx.exe'
    if (-not (Test-Path -LiteralPath $appPath -PathType Leaf)) { throw "Build output missing: $appPath" }
    Write-Host "Release app ready: $appPath" -ForegroundColor Green
    if ($Run) { Start-Process -FilePath $appPath -WorkingDirectory (Split-Path -Parent $appPath) }
}
catch {
    Write-Host "Build failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
