Add-Type -AssemblyName System.Drawing

# Builds resources/nativeoffice.ico from the master brand art.
#
# The source PNG is the full brand LOCKUP: the N mark, the three app tiles,
# the "NativeOffice" wordmark and the "CREATE - ANALYZE - PRESENT" tagline,
# stacked vertically on a 1024x1024 transparent canvas.
#
# Only the mark belongs in an app icon. The previous version of this script
# scaled the whole lockup into each frame, so by the time it reached the 32px
# and 16px entries that Windows uses for the taskbar and the window corner,
# the wordmark and tagline had collapsed into unreadable smears and the mark
# itself was left tiny in the middle of the frame.
#
# So: crop to the mark, then centre it in a square frame with a little
# breathing room, and scale THAT to each size. Every entry is resampled once,
# straight from the 1024px master, rather than from an already-shrunk bitmap.
#
# Crop box below is the mark's alpha bounding box in the master, measured
# rather than eyeballed. Re-measure if the artwork is ever replaced: the
# wordmark band starts at y=697, so anything at or below that is text.
$srcX = 267; $srcY = 141; $srcW = 477; $srcH = 539

# Fraction of the frame the mark fills. 0.88 keeps a hairline of padding so
# the art does not touch the edges when Windows rounds the corners.
$fill = 0.88

# Windows asks for more than the classic six: 20/24/40 are used at 125%, 150%
# and 200% display scaling, and 96 shows up in some Explorer views. Supplying
# them means Windows never has to resample one of ours itself.
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

$src = Join-Path $PSScriptRoot "logo_white_bg.png"
$dst = Join-Path $PSScriptRoot "nativeoffice.ico"
$srcImg = [System.Drawing.Image]::FromFile($src)

$entries = @()
foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # Fit the crop inside `size * fill`, preserving aspect ratio, centred.
    $box = $size * $fill
    $scale = [Math]::Min($box / $srcW, $box / $srcH)
    $dw = $srcW * $scale
    $dh = $srcH * $scale
    $dstRect = New-Object System.Drawing.RectangleF(
        (($size - $dw) / 2.0), (($size - $dh) / 2.0), $dw, $dh)
    $srcRect = New-Object System.Drawing.RectangleF($srcX, $srcY, $srcW, $srcH)
    $g.DrawImage($srcImg, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
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
