Add-Type -AssemblyName System.Drawing

$src = Join-Path $PSScriptRoot "logo_white_bg.png"
$dst = Join-Path $PSScriptRoot "nativeoffice.ico"
$sizes = @(16, 32, 48, 64, 128, 256)

$srcImg = [System.Drawing.Image]::FromFile($src)

$entries = @()
foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $g.Clear([System.Drawing.Color]::Transparent)
    # Source is already square (1024x1024) so this preserves aspect ratio exactly.
    $g.DrawImage($srcImg, 0, 0, $size, $size)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBytes = $ms.ToArray()
    $ms.Dispose()
    $bmp.Dispose()

    $entries += [PSCustomObject]@{ Size = $size; Bytes = $pngBytes }
}
$srcImg.Dispose()

$fs = New-Object System.IO.FileStream($dst, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICONDIR
$bw.Write([UInt16]0)               # reserved
$bw.Write([UInt16]1)               # type: 1 = icon
$bw.Write([UInt16]$entries.Count)  # image count

$headerSize = 6 + (16 * $entries.Count)
$offset = $headerSize
foreach ($e in $entries) {
    $wByte = if ($e.Size -ge 256) { 0 } else { $e.Size }
    $bw.Write([Byte]$wByte)        # width (0 = 256)
    $bw.Write([Byte]$wByte)        # height (0 = 256)
    $bw.Write([Byte]0)             # color palette count
    $bw.Write([Byte]0)             # reserved
    $bw.Write([UInt16]1)           # color planes
    $bw.Write([UInt16]32)          # bits per pixel
    $bw.Write([UInt32]$e.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $e.Bytes.Length
}
foreach ($e in $entries) {
    $bw.Write($e.Bytes)
}
$bw.Flush()
$bw.Close()
$fs.Close()

Write-Output "Wrote $dst ($($entries.Count) sizes: $($sizes -join ', '))"
