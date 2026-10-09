$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$lutPath=Join-Path (Split-Path $PSScriptRoot -Parent) 'Resources/Textures/ColorGrading/CinematicLUT.png'
New-Item -ItemType Directory -Force (Split-Path $lutPath -Parent) | Out-Null
$bitmap=New-Object System.Drawing.Bitmap 256,16
try {
    for($slice=0;$slice -lt 16;$slice++){for($row=0;$row -lt 16;$row++){for($column=0;$column -lt 16;$column++){
        $red=$column/15.0; $green=$row/15.0; $blue=$slice/15.0
        $luma=.2126*$red+.7152*$green+.0722*$blue
        $shadow=[math]::Pow(1-$luma,2); $highlight=[math]::Pow($luma,2)
        $red=[math]::Clamp([double](($red-.5)*1.04+.5-.014*$shadow+.025*$highlight),[double]0,[double]1)
        $green=[math]::Clamp([double](($green-.5)*1.04+.5+.003*$shadow+.007*$highlight),[double]0,[double]1)
        $blue=[math]::Clamp([double](($blue-.5)*1.04+.5+.020*$shadow-.015*$highlight),[double]0,[double]1)
        $bitmap.SetPixel($slice*16+$column,$row,[System.Drawing.Color]::FromArgb([int]($red*255),[int]($green*255),[int]($blue*255)))
    }}}
    $bitmap.Save($lutPath,[System.Drawing.Imaging.ImageFormat]::Png)
} finally {$bitmap.Dispose()}
