[CmdletBinding()]
param(
    [string]$FbxSdkRoot
)

$ErrorActionPreference = 'Stop'

try {
    $buildScript = Join-Path $PSScriptRoot 'build-release.ps1'
    $buildArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript)
    if ($FbxSdkRoot) { $buildArguments += @('-FbxSdkRoot', $FbxSdkRoot) }
    & powershell.exe @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Release build failed (exit $LASTEXITCODE)." }

    $buildDir = Join-Path $PSScriptRoot 'build'
    $cmake = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'project\(openfbx\s+VERSION\s+(\d+\.\d+\.\d+)\b') {
        throw 'Could not read the openfbx version from CMakeLists.txt.'
    }
    $packageName = "openfbx-$($Matches[1])-windows-x64"
    $files = @('openfbx.exe', 'open_fbx_icon.png', 'checker.png')
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath (Join-Path $buildDir $file) -PathType Leaf)) {
            throw "Release file missing: $file. Run build-release.cmd first."
        }
    }
    $checkerDir = Join-Path $buildDir 'colored_checkers'
    $checkers = @(Get-ChildItem -LiteralPath $checkerDir -File -Recurse)
    if ($checkers.Count -eq 0) { throw 'Release checker textures are missing. Run build-release.cmd first.' }

    $cache = Get-Content -LiteralPath (Join-Path $buildDir '.cmake/CMakeCache.txt') -Raw
    if ($cache -notmatch '(?m)^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$') {
        throw 'Could not locate the Visual Studio installation used for the Release build.'
    }
    $redistRoot = Join-Path $Matches[1].Trim() 'VC/Redist/MSVC'
    $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object { $_.Name -match '^14\.\d+\.\d+$' -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'x64/Microsoft.VC143.CRT') -PathType Container) } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $crtDir) { throw "Visual C++ x64 runtime files were not found in $redistRoot." }
    $crtFiles = @(Get-ChildItem -LiteralPath (Join-Path $crtDir.FullName 'x64/Microsoft.VC143.CRT') -Filter '*.dll' -File)
    if ($crtFiles.Count -eq 0) { throw 'Visual C++ x64 runtime directory contains no DLLs.' }

    $entries = @($files | ForEach-Object {
        @{ Source = Join-Path $buildDir $_; Name = $_ }
    })
    $entries += @(Get-ChildItem -LiteralPath $buildDir -Filter '*.dll' -File | ForEach-Object {
        @{ Source = $_.FullName; Name = $_.Name }
    })
    $entries += @($crtFiles | Where-Object { $_.Name -notin $entries.Name } | ForEach-Object {
        @{ Source = $_.FullName; Name = $_.Name }
    })
    $entries += @($checkers | ForEach-Object {
        @{ Source = $_.FullName; Name = $_.FullName.Substring($buildDir.Length + 1).Replace('\', '/') }
    })
    $entries += @{ Source = Join-Path $PSScriptRoot 'README.md'; Name = 'README.md' }

    $publishDir = Join-Path $PSScriptRoot 'publish'
    New-Item -ItemType Directory -Path $publishDir -Force | Out-Null
    $zipPath = Join-Path $publishDir "$packageName.zip"
    # Finish a temporary archive before replacing an existing package.
    $tempPath = Join-Path $publishDir ("{0}.{1}.tmp" -f $packageName, [guid]::NewGuid())
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    try {
        $archive = [System.IO.Compression.ZipFile]::Open($tempPath, 'Create')
        try {
            foreach ($entry in $entries) {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $entry.Source, "$packageName/$($entry.Name)",
                    [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        }
        finally { $archive.Dispose() }
        Move-Item -LiteralPath $tempPath -Destination $zipPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $tempPath) { Remove-Item -LiteralPath $tempPath -Force }
    }
    Write-Host "Release ZIP ready: $zipPath" -ForegroundColor Green
}
catch {
    Write-Host "Packaging failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
