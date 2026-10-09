$ErrorActionPreference='Stop'
$repoPath=Split-Path $PSScriptRoot -Parent
$headers=@{'User-Agent'='neo_Engine-MeadowAssetImport/2.0'}
$records=@()
function Save-Asset($record,[string]$relative){
    if(!$record.url -or !$record.md5){throw "Missing metadata: $relative"}
    $path=Join-Path $repoPath $relative
    [void][IO.Directory]::CreateDirectory((Split-Path $path -Parent))
    if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm MD5).Hash -ine $record.md5){
        Invoke-WebRequest -Uri $record.url -Headers $headers -OutFile "$path.download"
        if((Get-FileHash -LiteralPath "$path.download" -Algorithm MD5).Hash -ine $record.md5){throw "Checksum mismatch: $relative"}
        Move-Item -LiteralPath "$path.download" -Destination $path -Force
    }
    $script:records+=@{path=$relative;url=$record.url;md5=$record.md5;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    Write-Output "Verified $relative"
}
foreach($id in @('boulder_01','namaqualand_boulder_02','grass_medium_02','leafy_grass')){
    $f=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -Headers $headers
    if($id -ne 'leafy_grass'){Save-Asset $f.fbx.'2k'.fbx "Resources/Models/PolyHaven/${id}_2k.fbx"}
    $resolution=if($id -eq 'leafy_grass'){'4k'}else{'2k'}
    Save-Asset $f.Diffuse.$resolution.jpg "Resources/Textures/PolyHaven/${id}_diff_$resolution.jpg"
    foreach($map in @('nor_dx','Rough','AO')){Save-Asset $f.$map.'2k'.jpg "Resources/Textures/PolyHaven/${id}_${map}_2k.jpg"}
    if($id -eq 'grass_medium_02'){Save-Asset $f.Alpha.'2k'.png 'Resources/Textures/PolyHaven/grass_medium_02_alpha_2k.png'}
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repoPath 'Resources/Credits/MeadowRefreshSources.json') -Encoding utf8
