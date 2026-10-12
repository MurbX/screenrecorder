# Generates res\app.ico: a round dark badge with a white ring and a red record dot.
# Multi-size ICO (16..256) with PNG-compressed entries. Run: powershell -File tools\make_icon.ps1
Add-Type -AssemblyName System.Drawing
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$pngs = foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $pad = [Math]::Max(0.5, $s * 0.02)
    $rect = New-Object System.Drawing.RectangleF $pad, $pad, ($s - 2 * $pad), ($s - 2 * $pad)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(255, 0x3a, 0x3a, 0x3c)), ([System.Drawing.Color]::FromArgb(255, 0x1c, 0x1c, 0x1e)), 90
    $g.FillEllipse($bg, $rect)
    $ringW = [Math]::Max(1.2, $s * 0.07)
    $r = $s * 0.30
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 0xf2, 0xf2, 0xf7)), $ringW
    $g.DrawEllipse($pen, ($s / 2 - $r), ($s / 2 - $r), (2 * $r), (2 * $r))
    $d = $s * 0.19
    $red = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xff, 0x45, 0x3a))
    $g.FillEllipse($red, ($s / 2 - $d), ($s / 2 - $d), (2 * $d), (2 * $d))
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}

$out = Join-Path $PSScriptRoot "..\res\app.ico"
$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()
Write-Output "Wrote $out"
