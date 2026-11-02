$ErrorActionPreference='Stop'
# Downloads the CC0 assets for the rebuilt ring city (alley -> houses -> shops -> offices -> towers).
# Poly Haven files are verified against the official MD5. ambientCG zips have no published hash,
# so their byte size is checked against the API and only the PBR maps are kept.
# The game only uses the local copies. Sources are recorded in Resources/Credits/CityRingSources.json.
$repoPath=Split-Path $PSScriptRoot -Parent
$headers=@{'User-Agent'='neo_Engine-CityRingImport/1.0'}
$records=@()
$skipped=@()
function Save-File([string]$url,[string]$md5,[long]$size,[string]$relative){
    $path=Join-Path $repoPath $relative
    [void][IO.Directory]::CreateDirectory((Split-Path $path -Parent))
    $ok={param($p) if($md5){(Get-FileHash -LiteralPath $p -Algorithm MD5).Hash -ieq $md5}else{(Get-Item -LiteralPath $p).Length -eq $size}}
    if(!(Test-Path -LiteralPath $path) -or !(& $ok $path)){
        # A file the server keeps failing on is skipped and listed at the end instead of stopping the run.
        for($attempt=1;;++$attempt){
            try{Invoke-WebRequest -Uri $url -Headers $headers -OutFile "$path.download";break}
            catch{if($attempt -ge 4){$script:skipped+="$relative ($url): $($_.Exception.Message)";Remove-Item -LiteralPath "$path.download" -ErrorAction SilentlyContinue;return};Start-Sleep -Seconds (2*$attempt)}
        }
        if(!(& $ok "$path.download")){throw "Checksum mismatch: $relative"}
        Move-Item -LiteralPath "$path.download" -Destination $path -Force
    }
    $script:records+=@{path=$relative;url=$url;md5=$md5;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    Write-Output "Verified $relative"
}
# Poly Haven models: the 2K FBX plus every texture it references (multi-material models keep
# their own texture folder next to the FBX). Single-material maps are also copied in the
# Resources/Textures/PolyHaven naming the city loader already uses.
$models=@(
    # Houses
    'planter_box_01','planter_box_02','potted_plant_01','potted_plant_02','painted_wooden_bench','wooden_picnic_table',
    'plastic_monobloc_chair_01','metal_trash_can','modular_electricity_poles','tree_small_02','concrete_cat_statue','exterior_aircon_unit',
    # Shops
    'street_lamp_01','street_lamp_02','modular_street_seating','outdoor_table_chair_set_01','CoffeeCart_01','rollershutter_door',
    'rollershutter_window_01','modular_urban_apartments_facade','modular_fire_escape','Barrel_01','wooden_barrels_01','security_light',
    # Offices and towers
    'island_tree_01','jacaranda_tree','horse_statue_01','bronze_whale_statue','portable_generator','fire_hydrant',
    'modular_chainlink_fence','cement_bag')
# Poly Haven tiling surfaces for house walls, roofs and pavements.
$textures=@('brick_wall_02','plastered_wall_02','weathered_plank_siding','clay_roof_tiles','grey_roof_tiles','roof_slates_02',
    'patterned_paving','square_brick_paving')
foreach($id in $models){
    $f=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -Headers $headers
    $fbx=$f.fbx.'2k'.fbx
    Save-File $fbx.url $fbx.md5 $fbx.size "Resources/Models/PolyHaven/${id}_2k.fbx"
    # The FBX references its colour maps as JPG next to it (textures/...); its data maps are EXR,
    # which the engine cannot read, so only the JPG references are kept.
    foreach($include in $fbx.include.PSObject.Properties){
        if($include.Name -match '\.(jpg|png)$'){Save-File $include.Value.url $include.Value.md5 $include.Value.size ("Resources/Models/PolyHaven/"+$include.Name)}
    }
    if($f.Diffuse){
        Save-File $f.Diffuse.'2k'.jpg.url $f.Diffuse.'2k'.jpg.md5 $f.Diffuse.'2k'.jpg.size "Resources/Textures/PolyHaven/${id}_diff_2k.jpg"
        foreach($map in @('nor_dx','Rough','AO')){if($f.$map){$m=$f.$map.'2k'.jpg;Save-File $m.url $m.md5 $m.size "Resources/Textures/PolyHaven/${id}_${map}_2k.jpg"}}
    }
    # Multi-material models list each texture set's maps as <set>_diff / <set>_nor_dx / <set>_rough / <set>_ao.
    foreach($p in $f.PSObject.Properties){
        if($p.Name -match '^(.+)_(diff|nor_dx|rough|ao)$' -and $p.Value.'2k'.jpg){$m=$p.Value.'2k'.jpg
            Save-File $m.url $m.md5 $m.size "Resources/Textures/PolyHaven/${id}_$($p.Name)_2k.jpg"}
    }
}
foreach($id in $textures){
    $f=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -Headers $headers
    Save-File $f.Diffuse.'2k'.jpg.url $f.Diffuse.'2k'.jpg.md5 $f.Diffuse.'2k'.jpg.size "Resources/Textures/PolyHaven/${id}_diff_2k.jpg"
    foreach($map in @('nor_dx','Rough','AO')){if($f.$map){$m=$f.$map.'2k'.jpg;Save-File $m.url $m.md5 $m.size "Resources/Textures/PolyHaven/${id}_${map}_2k.jpg"}}
}
# ambientCG building facades (glass offices and skyscrapers). Each zip is unpacked in its own
# temporary folder; only the colour, DirectX normal, roughness, AO and metalness maps are kept,
# renamed to the Poly Haven suffixes (diff / nor_dx / Rough / AO / metal).
$facades=@('Facade001','Facade003','Facade005','Facade006','Facade012','Facade014','Facade018A','Facade019A','Facade020A')
$suffix=@{Color='diff';NormalDX='nor_dx';Roughness='Rough';AmbientOcclusion='AO';Metalness='metal'}
foreach($id in $facades){
    $info=(Invoke-RestMethod -Uri "https://ambientcg.com/api/v2/full_json?id=$id&include=downloadData" -Headers $headers).foundAssets[0]
    $zip=$info.downloadFolders.default.downloadFiletypeCategories.zip.downloads | Where-Object attribute -eq '2K-JPG'
    $zipPath="Resources/Textures/AmbientCG/download/$($zip.fileName)"
    Save-File $zip.downloadLink $null $zip.size $zipPath
    $unpack=Join-Path ([IO.Path]::GetTempPath()) ("acg-"+[Guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath (Join-Path $repoPath $zipPath) -DestinationPath $unpack
    foreach($file in Get-ChildItem -LiteralPath $unpack -Filter '*.jpg'){
        foreach($key in $suffix.Keys){if($file.BaseName -like "*_$key"){
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $repoPath "Resources/Textures/AmbientCG/${id}_$($suffix[$key])_2k.jpg") -Force}}
    }
    Remove-Item -LiteralPath $unpack -Recurse -Force
    Remove-Item -LiteralPath (Join-Path $repoPath $zipPath) -Force
}
Remove-Item -LiteralPath (Join-Path $repoPath 'Resources/Textures/AmbientCG/download') -Recurse -Force -ErrorAction SilentlyContinue
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repoPath 'Resources/Credits/CityRingSources.json') -Encoding utf8
if($skipped.Count){Write-Output "SKIPPED $($skipped.Count) file(s):";$skipped | ForEach-Object {Write-Output "  $_"}}else{Write-Output 'All files verified'}
