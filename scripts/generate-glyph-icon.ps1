#Requires -Version 5.1
<#
.SYNOPSIS
  Render a glyph into PNG and ICO icon assets.

.PARAMETER Glyph
  The character or short text to render. Defaults to "ㄓ".

.PARAMETER SourceImagePath
  Optional path to an existing image to scale into PNG and ICO assets.

.PARAMETER OutputPath
  Target .ico path. Defaults to scripts\out\zhi.ico.

.PARAMETER PreviewPngPath
  Target PNG preview path. Defaults to the same base name as OutputPath.

.PARAMETER FontFamily
  Preferred font family. Falls back to common CJK-capable fonts if unavailable.

.PARAMETER ForegroundColor
  Text color. Accepts HTML hex or named colors.

.PARAMETER BackgroundColor
  Background color. Use white by default.

.PARAMETER Sizes
  Icon frame sizes to embed in the .ico file. Defaults to 32x32.

.PARAMETER PreviewSize
  PNG preview size. This is separate from the ICO frame sizes.

.PARAMETER Padding
  Inner padding ratio per side. 0.05 means 5 percent padding on each edge.

.PARAMETER GlyphScale
  Scales the glyph inside the draw area without changing the output image size.
#>
param(
    [string] $Glyph = "ㄓ",
    [string] $SourceImagePath = "",
    [string] $OutputPath = "",
    [string] $PreviewPngPath = "",
    [string] $FontFamily = "Microsoft JhengHei UI",
    [string] $ForegroundColor = "#111111",
    [string] $BackgroundColor = "#FFFFFF",
    [int[]] $Sizes = @(32),
    [int] $PreviewSize = 32,
    [ValidateRange(0.00, 0.45)]
    [double] $Padding = 0.05,
    [ValidateRange(0.80, 1.50)]
    [double] $GlyphScale = 2
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function Resolve-Color {
    param([string] $Value)

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "Transparent") {
        return [System.Drawing.Color]::Transparent
    }

    return [System.Drawing.ColorTranslator]::FromHtml($Value)
}

function New-ParentDirectory {
    param([string] $Path)

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Resolve-FontFamilyName {
    param([string] $PreferredFont)

    $installed = New-Object System.Drawing.Text.InstalledFontCollection
    $familyNames = @{}
    foreach ($family in $installed.Families) {
        $familyNames[$family.Name.ToLowerInvariant()] = $family.Name
    }

    $candidates = @(
        $PreferredFont,
        "Microsoft JhengHei UI",
        "Microsoft JhengHei",
        "PMingLiU",
        "Segoe UI Symbol",
        "Arial Unicode MS"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($candidate in $candidates) {
        $key = $candidate.ToLowerInvariant()
        if ($familyNames.ContainsKey($key)) {
            return $familyNames[$key]
        }
    }

    throw "No suitable font was found. Tried: $($candidates -join ', ')"
}

function New-GlyphBitmap {
    param(
        [int] $Size,
        [string] $Glyph,
        [string] $FontName,
        [System.Drawing.Color] $Foreground,
        [System.Drawing.Color] $Background,
        [double] $Padding,
        [double] $GlyphScale
    )

    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $graphics.Clear($Background)

        $paddingPixels = [single]([math]::Round($Size * $Padding, 2))
        $drawArea = New-Object System.Drawing.RectangleF(
            $paddingPixels,
            $paddingPixels,
            [single]($Size - ($paddingPixels * 2)),
            [single]($Size - ($paddingPixels * 2))
        )
        if ($GlyphScale -ne 1.0) {
            $scaledWidth = [single]($drawArea.Width * $GlyphScale)
            $scaledHeight = [single]($drawArea.Height * $GlyphScale)
            $drawArea = New-Object System.Drawing.RectangleF(
                [single](($Size - $scaledWidth) / 2.0),
                [single](($Size - $scaledHeight) / 2.0),
                $scaledWidth,
                $scaledHeight
            )
        }

        $stringFormat = New-Object System.Drawing.StringFormat
        $stringFormat.Alignment = [System.Drawing.StringAlignment]::Center
        $stringFormat.LineAlignment = [System.Drawing.StringAlignment]::Center

        $fontSize = [single]$drawArea.Height
        $font = $null
        while ($fontSize -ge 4) {
            if ($font) {
                $font.Dispose()
                $font = $null
            }

            $font = New-Object System.Drawing.Font(
                $FontName,
                $fontSize,
                [System.Drawing.FontStyle]::Bold,
                [System.Drawing.GraphicsUnit]::Pixel
            )

            $measured = $graphics.MeasureString(
                $Glyph,
                $font,
                [System.Drawing.SizeF]::new($drawArea.Width * 2, $drawArea.Height * 2),
                $stringFormat
            )

            if ($measured.Width -le $drawArea.Width -and $measured.Height -le $drawArea.Height) {
                break
            }

            $fontSize -= 1
        }

        if (-not $font) {
            throw "Failed to create a font for rendering."
        }

        $brush = New-Object System.Drawing.SolidBrush($Foreground)
        try {
            $graphics.DrawString($Glyph, $font, $brush, $drawArea, $stringFormat)
        }
        finally {
            $brush.Dispose()
            $font.Dispose()
            $stringFormat.Dispose()
        }

        return $bitmap
    }
    catch {
        $bitmap.Dispose()
        throw
    }
    finally {
        $graphics.Dispose()
    }
}

function New-ScaledImageBitmap {
    param(
        [int] $Size,
        [string] $SourceImagePath,
        [System.Drawing.Color] $Background,
        [double] $Padding
    )

    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $sourceImage = $null
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.Clear($Background)

        $sourceImage = [System.Drawing.Image]::FromFile($SourceImagePath)
        $paddingPixels = [single]([math]::Round($Size * $Padding, 2))
        $contentSize = [single]($Size - ($paddingPixels * 2))
        $scale = [math]::Min($contentSize / $sourceImage.Width, $contentSize / $sourceImage.Height)
        $targetWidth = [single]($sourceImage.Width * $scale)
        $targetHeight = [single]($sourceImage.Height * $scale)
        $drawRect = New-Object System.Drawing.RectangleF(
            [single](($Size - $targetWidth) / 2.0),
            [single](($Size - $targetHeight) / 2.0),
            $targetWidth,
            $targetHeight
        )

        $graphics.DrawImage($sourceImage, $drawRect)
        return $bitmap
    }
    catch {
        $bitmap.Dispose()
        throw
    }
    finally {
        if ($sourceImage) {
            $sourceImage.Dispose()
        }
        $graphics.Dispose()
    }
}

function Convert-BitmapToPngBytes {
    param([System.Drawing.Bitmap] $Bitmap)

    $stream = New-Object System.IO.MemoryStream
    try {
        $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return [byte[]]$stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

function Convert-BitmapToIcoFrameBytes {
    param([System.Drawing.Bitmap] $Bitmap)

    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $xorStride = $width * 4
    $andStride = [int]([math]::Ceiling($width / 32.0) * 4)
    $andMaskSize = $andStride * $height
    $xorBitmapSize = $xorStride * $height

    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        # BITMAPINFOHEADER
        $writer.Write([UInt32]40)
        $writer.Write([Int32]$width)
        $writer.Write([Int32]($height * 2))
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]0)
        $writer.Write([UInt32]($xorBitmapSize + $andMaskSize))
        $writer.Write([Int32]0)
        $writer.Write([Int32]0)
        $writer.Write([UInt32]0)
        $writer.Write([UInt32]0)

        for ($y = $height - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $width; $x++) {
                $pixel = $Bitmap.GetPixel($x, $y)
                $writer.Write([byte]$pixel.B)
                $writer.Write([byte]$pixel.G)
                $writer.Write([byte]$pixel.R)
                $writer.Write([byte]$pixel.A)
            }
        }

        $andMask = New-Object byte[] $andMaskSize
        $writer.Write($andMask)

        return [byte[]]$stream.ToArray()
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Write-IcoFile {
    param(
        [string] $Path,
        [object[]] $Frames
    )

    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$Frames.Count)

        $offset = 6 + (16 * $Frames.Count)
        foreach ($frame in $Frames) {
            $frameBytes = [byte[]]$frame.IcoBytes
            $dimensionByte = if ($frame.Size -ge 256) { [byte]0 } else { [byte]$frame.Size }
            $writer.Write($dimensionByte)
            $writer.Write($dimensionByte)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$frameBytes.Length)
            $writer.Write([UInt32]$offset)
            $offset += $frameBytes.Length
        }

        foreach ($frame in $Frames) {
            $writer.Write([byte[]]$frame.IcoBytes)
        }

        [System.IO.File]::WriteAllBytes($Path, $stream.ToArray())
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

$scriptOutDir = Join-Path $PSScriptRoot "out"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    if ([string]::IsNullOrWhiteSpace($SourceImagePath)) {
        $OutputPath = Join-Path $scriptOutDir "zhi.ico"
    } else {
        $sourceImageFullPath = [System.IO.Path]::GetFullPath($SourceImagePath)
        $OutputPath = Join-Path ([System.IO.Path]::GetDirectoryName($sourceImageFullPath)) (([System.IO.Path]::GetFileNameWithoutExtension($sourceImageFullPath)) + ".ico")
    }
}
if ([string]::IsNullOrWhiteSpace($PreviewPngPath)) {
    $PreviewPngPath = [System.IO.Path]::ChangeExtension($OutputPath, ".png")
}

$sourceImageFullPath = ""
if (-not [string]::IsNullOrWhiteSpace($SourceImagePath)) {
    $sourceImageFullPath = [System.IO.Path]::GetFullPath($SourceImagePath)
    if (-not (Test-Path -LiteralPath $sourceImageFullPath)) {
        throw "Source image not found: $sourceImageFullPath"
    }
}

$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$PreviewPngPath = [System.IO.Path]::GetFullPath($PreviewPngPath)
New-ParentDirectory -Path $OutputPath
New-ParentDirectory -Path $PreviewPngPath

$foreground = Resolve-Color -Value $ForegroundColor
$background = Resolve-Color -Value $BackgroundColor
$resolvedSizes = $Sizes | Where-Object { $_ -ge 16 -and $_ -le 256 } | Sort-Object -Unique
if (-not $resolvedSizes) {
    throw "Provide at least one icon size between 16 and 256."
}
$fontName = $null
if ([string]::IsNullOrWhiteSpace($sourceImageFullPath)) {
    $fontName = Resolve-FontFamilyName -PreferredFont $FontFamily
}

$frames = @(
    foreach ($size in $resolvedSizes) {
        if ([string]::IsNullOrWhiteSpace($sourceImageFullPath)) {
            $bitmap = New-GlyphBitmap `
                -Size $size `
                -Glyph $Glyph `
                -FontName $fontName `
                -Foreground $foreground `
                -Background $background `
                -Padding $Padding `
                -GlyphScale $GlyphScale
        } else {
            $bitmap = New-ScaledImageBitmap `
                -Size $size `
                -SourceImagePath $sourceImageFullPath `
                -Background $background `
                -Padding $Padding
        }

        try {
            [pscustomobject]@{
                Size     = $size
                IcoBytes = Convert-BitmapToIcoFrameBytes -Bitmap $bitmap
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
)

Write-IcoFile -Path $OutputPath -Frames $frames

if ([string]::IsNullOrWhiteSpace($sourceImageFullPath)) {
    $previewBitmap = New-GlyphBitmap `
        -Size $PreviewSize `
        -Glyph $Glyph `
        -FontName $fontName `
        -Foreground $foreground `
        -Background $background `
        -Padding $Padding `
        -GlyphScale $GlyphScale
} else {
    $previewBitmap = New-ScaledImageBitmap `
        -Size $PreviewSize `
        -SourceImagePath $sourceImageFullPath `
        -Background $background `
        -Padding $Padding
}
try {
    [System.IO.File]::WriteAllBytes($PreviewPngPath, (Convert-BitmapToPngBytes -Bitmap $previewBitmap))
}
finally {
    $previewBitmap.Dispose()
}

if ([string]::IsNullOrWhiteSpace($sourceImageFullPath)) {
    Write-Host "Glyph: $Glyph"
    Write-Host "Font : $fontName"
} else {
    Write-Host "Image: $sourceImageFullPath"
}
Write-Host "ICO  : $OutputPath"
Write-Host "PNG  : $PreviewPngPath"
Write-Host "Sizes: $($resolvedSizes -join ', ')"
