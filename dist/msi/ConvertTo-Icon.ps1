$ErrorActionPreference = 'Stop'

# Adapted from: https://gist.github.com/darkfall/1656050

Add-Type -AssemblyName System.Drawing

$outputFileName = $args[0]
$inputFileNames = $args[1..$args.Count]

$inputImages = [System.Collections.ArrayList]@()

foreach ($inputFileName in $inputFileNames) {
    $resolvedInputFileName = $ExecutionContext.SessionState.Path.GetResolvedPSPathFromPSPath($inputFileName)
    Write-Output "Input[$($inputImages.Count)]: $inputFileName ($resolvedInputFileName)"

    $inputBitmap = New-Object System.Drawing.Bitmap "$resolvedInputFileName"
    if ($inputBitmap.Width -ne $inputBitmap.Height) {
        throw "Input image is not square ($($inputBitmap.Width) != $($inputBitmap.Height))"
    }

    $inputStream = New-Object System.IO.MemoryStream
    $inputBitmap.Save($inputStream, [System.Drawing.Imaging.ImageFormat]::Png)

    $inputImage = @{
        Size = $inputBitmap.Width
        Bytes = $inputStream.ToArray()
    }
    Write-Output "  Size = $($inputImage.Size), Bytes = $($inputImage.Bytes.Count)"

    [void]$inputImages.Add($inputImage)

    $inputBitmap.Dispose()
    $inputStream.Dispose()
}

$resolvedOutputFileName = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($outputFileName)
Write-Output "Output: $outputFileName ($resolvedOutputFileName)"

$outputFile = [System.IO.File]::Create($resolvedOutputFileName)

$outputFileWriter = New-Object System.IO.BinaryWriter($outputFile)
$outputFileWriter.Write([short]0) # 0-1 reserved, 0
$outputFileWriter.Write([short]1) # 2-3 image type, 1 = icon, 2 = cursor
$outputFileWriter.Write([short]$inputImages.Count) # 4-5 number of images

$offset = 6 + (16 * $inputImages.Count)

for ($i = 0; $i -lt $inputImages.Count; ++$i) {
    $size = $inputImages[$i].Size
    if ($size -eq 256) {
        $size = 0
    }

    $outputFileWriter.Write([byte]$size) # 0 image width
    $outputFileWriter.Write([byte]$size) # 1 image height
    $outputFileWriter.Write([byte]0) # 2 number of colors
    $outputFileWriter.Write([byte]0) # 3 reserved
    $outputFileWriter.Write([short]0) # 4-5 color planes
    $outputFileWriter.Write([short]32) # 6-7 bits per pixel
    $outputFileWriter.Write([int]$inputImages[$i].Bytes.Count) # 8-11 size of image data
    $outputFileWriter.Write([int]$offset) # 12-15 offset of image data

    $offset += $inputImages[$i].Bytes.Count
}

for ($i = 0; $i -lt $inputImages.Count; ++$i) {
    # write image data
    # png data must contain the whole png data file
    $outputFileWriter.Write($inputImages[$i].Bytes)
}

$outputFileWriter.Flush()
$outputFile.Close()
