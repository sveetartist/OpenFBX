[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

try {
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

    $entries = @($files | ForEach-Object {
        @{ Source = Join-Path $buildDir $_; Name = $_ }
    })
    $entries += @(Get-ChildItem -LiteralPath $buildDir -Filter '*.dll' -File | ForEach-Object {
        @{ Source = $_.FullName; Name = $_.Name }
    })
    $entries += @($checkers | ForEach-Object {
        @{ Source = $_.FullName; Name = $_.FullName.Substring($buildDir.Length + 1).Replace('\', '/') }
    })
    $entries += @{ Source = Join-Path $PSScriptRoot 'README-release.md'; Name = 'README.md' }

    $releaseDir = Join-Path $PSScriptRoot 'releases'
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    $zipPath = Join-Path $releaseDir "$packageName.zip"
    # Finish a temporary archive before replacing an existing package.
    $tempPath = Join-Path $releaseDir ("{0}.{1}.tmp" -f $packageName, [guid]::NewGuid())
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
    Write-Host "GitHub release asset ready: $zipPath" -ForegroundColor Green
}
catch {
    Write-Host "Packaging failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
