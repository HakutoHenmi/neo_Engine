$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$headers = @{ 'User-Agent' = 'neo_Engine-MeadowAssetImport/1.0' }

function Save-PolyHavenFile($record, [string]$relativePath) {
    if (!$record -or !$record.url -or !$record.md5) { throw "Missing Poly Haven metadata: $relativePath" }
    $destination = Join-Path $repoPath $relativePath
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    if (Test-Path -LiteralPath $destination) {
        $actual = (Get-FileHash -LiteralPath $destination -Algorithm MD5).Hash
        if ($actual -ieq $record.md5) { Write-Output "Verified $relativePath"; return }
    }
    $temporary = "$destination.download"
    Invoke-WebRequest -Uri $record.url -Headers $headers -OutFile $temporary
    $actual = (Get-FileHash -LiteralPath $temporary -Algorithm MD5).Hash
    if ($actual -ine $record.md5) { throw "Checksum mismatch: $relativePath" }
    Move-Item -LiteralPath $temporary -Destination $destination -Force
    Write-Output "Downloaded $relativePath"
}

$rockFace = Invoke-RestMethod -Uri 'https://api.polyhaven.com/files/rock_face_02' -Headers $headers
$boulder = Invoke-RestMethod -Uri 'https://api.polyhaven.com/files/namaqualand_boulders_01' -Headers $headers
$stone = Invoke-RestMethod -Uri 'https://api.polyhaven.com/files/rock_09' -Headers $headers
$grass = Invoke-RestMethod -Uri 'https://api.polyhaven.com/files/grass_bermuda_01' -Headers $headers

Save-PolyHavenFile $rockFace.fbx.'1k'.fbx 'Resources/Models/PolyHaven/rock_face_02_1k.fbx'
Save-PolyHavenFile $rockFace.Diffuse.'2k'.jpg 'Resources/Textures/PolyHaven/rock_face_02_diff_2k.jpg'
Save-PolyHavenFile $boulder.fbx.'1k'.fbx 'Resources/Models/PolyHaven/namaqualand_boulders_01_1k.fbx'
Save-PolyHavenFile $boulder.Diffuse.'2k'.jpg 'Resources/Textures/PolyHaven/namaqualand_boulders_01_diff_2k.jpg'
Save-PolyHavenFile $stone.fbx.'1k'.fbx 'Resources/Models/PolyHaven/rock_09_1k.fbx'
Save-PolyHavenFile $stone.Diffuse.'2k'.jpg 'Resources/Textures/PolyHaven/rock_09_diff_2k.jpg'
Save-PolyHavenFile $grass.fbx.'1k'.fbx 'Resources/Models/PolyHaven/grass_bermuda_01_1k.fbx'
Save-PolyHavenFile $grass.Diffuse.'2k'.jpg 'Resources/Textures/PolyHaven/grass_bermuda_01_diff_2k.jpg'
Save-PolyHavenFile $grass.Alpha.'2k'.png 'Resources/Textures/PolyHaven/grass_bermuda_01_alpha_2k.png'

$grass004 = Join-Path $repoPath 'Resources/Textures/AmbientCG/grass004_color_1k.jpg'
$grass004Hash = '9E1C60DA44B34A9738B1256BA827541F097DC583521B1D281E61B5F4B4217BC5'
if (!(Test-Path -LiteralPath $grass004) -or (Get-FileHash -LiteralPath $grass004 -Algorithm SHA256).Hash -ine $grass004Hash) {
    New-Item -ItemType Directory -Force -Path (Split-Path $grass004 -Parent) | Out-Null
    Add-Type -AssemblyName System.IO.Compression
    $archivePath = "$grass004.download.zip"
    Invoke-WebRequest -Uri 'https://ambientcg.com/get?file=Grass004_1K-JPG.zip' -OutFile $archivePath
    $archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $entry = $archive.GetEntry('Grass004_1K-JPG_Color.jpg')
        if (!$entry) { throw 'Grass004 color map missing from archive' }
        $inputStream = $entry.Open()
        $outputStream = [System.IO.File]::Create($grass004)
        try { $inputStream.CopyTo($outputStream) }
        finally { $outputStream.Dispose(); $inputStream.Dispose() }
    } finally { $archive.Dispose(); Remove-Item -LiteralPath $archivePath }
    if ((Get-FileHash -LiteralPath $grass004 -Algorithm SHA256).Hash -ine $grass004Hash) { throw 'Grass004 checksum mismatch' }
    Write-Output 'Downloaded Resources/Textures/AmbientCG/grass004_color_1k.jpg'
} else { Write-Output 'Verified Resources/Textures/AmbientCG/grass004_color_1k.jpg' }
