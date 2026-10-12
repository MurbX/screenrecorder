# Builds a release: ScreenRec.exe (Release) + pinned FFmpeg -> installer, portable zip and checksums in dist\.
#
#   powershell -ExecutionPolicy Bypass -File packaging\package.ps1
#   powershell -ExecutionPolicy Bypass -File packaging\package.ps1 -FFmpegDir <folder containing bin\ffmpeg.exe>
#
# Used locally and by .github/workflows/release.yml. The version comes from the VERSION file.
param(
    [string]$FFmpegDir = "",   # optional: an already-extracted FFmpeg build (skips the download)
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $root "VERSION") -TotalCount 1).Trim()
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "stage"
Write-Host "Packaging ScreenRec $version"

# FFmpeg build bundled with releases. Update all three together (see docs/RELEASING.md).
$ffVersion = "9.0.2"
$ffUrl = "https://github.com/GyanD/codexffmpeg/releases/download/$ffVersion/ffmpeg-$ffVersion-full_build.zip"
$ffSha256 = "759d0a9831c436a0eb331ad36f236c06cb04aaa0005da46571f0e9d3d9206f6b"

# 1. Build the app (Release).
if (-not $SkipBuild) {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {   # CI: MSVC environment already set up
        cmake -S $root -B (Join-Path $root "build") -G Ninja -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE) { throw "cmake configure failed" }
        cmake --build (Join-Path $root "build")
        if ($LASTEXITCODE) { throw "build failed" }
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "build.ps1") -Config Release
        if ($LASTEXITCODE) { throw "build failed" }
    }
}
$exe = Join-Path $root "build\ScreenRec.exe"
if (-not (Test-Path $exe)) { throw "ScreenRec.exe not found - build first" }

# 2. FFmpeg: use the given folder, or download the pinned build and verify its checksum.
if (-not $FFmpegDir) {
    $cache = Join-Path $root "build\ffmpeg-$ffVersion"
    $zip = "$cache.zip"
    if (-not (Test-Path (Join-Path $cache "bin\ffmpeg.exe"))) {
        if (-not (Test-Path $zip)) {
            Write-Host "Downloading FFmpeg $ffVersion..."
            $ProgressPreference = "SilentlyContinue"
            Invoke-WebRequest -Uri $ffUrl -OutFile $zip -UseBasicParsing
        }
        $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $ffSha256) { Remove-Item $zip; throw "FFmpeg checksum mismatch ($hash)" }
        $tmp = "$cache.tmp"
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        Expand-Archive $zip -DestinationPath $tmp
        Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName $cache
        Remove-Item -Recurse -Force $tmp
    }
    $FFmpegDir = $cache
}
$ffmpeg = Join-Path $FFmpegDir "bin\ffmpeg.exe"
if (-not (Test-Path $ffmpeg)) { throw "ffmpeg.exe not found in $FFmpegDir\bin" }

# 3. Stage the files that ship.
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $stage "licenses") | Out-Null
Copy-Item $exe $stage
Copy-Item $ffmpeg $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "THIRD-PARTY-NOTICES.md") (Join-Path $stage "THIRD-PARTY-NOTICES.txt")
Copy-Item (Join-Path $FFmpegDir "LICENSE") (Join-Path $stage "licenses\FFmpeg-LICENSE.txt")

# 4. Portable zip.
$portable = Join-Path $dist "ScreenRec-$version-portable.zip"
Remove-Item $portable -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $portable -CompressionLevel Optimal

# 5. Installer.
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }
& $iscc /Q "/DAppVersion=$version" "/DStageDir=$stage" (Join-Path $root "installer\ScreenRec.iss")
if ($LASTEXITCODE) { throw "Inno Setup failed" }
$setup = Join-Path $dist "ScreenRec-Setup-$version.exe"

# 6. Checksums.
$sums = foreach ($f in $setup, $portable) { "{0}  {1}" -f (Get-FileHash $f -Algorithm SHA256).Hash.ToLower(), (Split-Path $f -Leaf) }
$sums | Set-Content (Join-Path $dist "SHA256SUMS.txt") -Encoding ascii
Get-ChildItem $dist -File | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } } | Format-Table -AutoSize
