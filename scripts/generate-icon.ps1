# 用 Windows 自带的 System.Drawing 生成图标，无需安装额外工具。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path -Parent $PSScriptRoot
$resourceDir = Join-Path $projectRoot 'src\res'
New-Item -ItemType Directory -Force -Path $resourceDir | Out-Null
$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)
$frames = [System.Collections.Generic.List[byte[]]]::new()

foreach ($size in $sizes) {
    # 四倍分辨率绘制后缩小，让小尺寸的边缘也保持平滑。
    $canvas = [System.Drawing.Bitmap]::new($size * 4, $size * 4)
    $graphics = [System.Drawing.Graphics]::FromImage($canvas)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size / 16.0, $size / 16.0)
    $shape = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $shape.AddArc(2, 2, 24, 24, 180, 90)
    $shape.AddArc(38, 2, 24, 24, 270, 90)
    $shape.AddArc(38, 38, 24, 24, 0, 90)
    $shape.AddArc(2, 38, 24, 24, 90, 90)
    $shape.CloseFigure()
    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.Point]::new(0, 0), [System.Drawing.Point]::new(64, 64),
        [System.Drawing.ColorTranslator]::FromHtml('#19374F'),
        [System.Drawing.ColorTranslator]::FromHtml('#0B172B'))
    $white = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#F1F7FC'), 5)
    $white.StartCap = $white.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $white.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $mint = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#43E0BA'))
    $image = [System.Drawing.Bitmap]::new($size, $size)
    $output = [System.Drawing.Graphics]::FromImage($image)
    $stream = [System.IO.MemoryStream]::new()
    try {
        $graphics.FillPath($background, $shape)
        $points = [System.Drawing.PointF[]]@(
            [System.Drawing.PointF]::new(13, 23),
            [System.Drawing.PointF]::new(21, 31),
            [System.Drawing.PointF]::new(13, 39))
        $graphics.DrawLines($white, $points)
        $graphics.FillRectangle($mint, 29, 35, 6, 13)
        $graphics.FillRectangle($mint, 39, 27, 6, 21)
        $graphics.FillRectangle($mint, 49, 19, 6, 29)
        $output.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $output.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $output.DrawImage($canvas, 0, 0, $size, $size)
        $image.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        $frames.Add($stream.ToArray())
        if ($size -eq 256) {
            [System.IO.File]::WriteAllBytes((Join-Path $resourceDir 'CodexUsage.png'), $stream.ToArray())
        }
    } finally {
        $stream.Dispose()
        $output.Dispose()
        $image.Dispose()
        $mint.Dispose()
        $white.Dispose()
        $background.Dispose()
        $shape.Dispose()
        $graphics.Dispose()
        $canvas.Dispose()
    }
}

# ICO 中嵌入各尺寸的透明 PNG，适用于本程序支持的 Windows 10/11。
$file = [System.IO.File]::Create((Join-Path $resourceDir 'CodexUsage.ico'))
$writer = [System.IO.BinaryWriter]::new($file)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write($frame) }
} finally {
    $writer.Dispose()
}
Write-Host '已生成 src\res\CodexUsage.ico 和 PNG 预览。'
