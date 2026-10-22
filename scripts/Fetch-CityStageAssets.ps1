$ErrorActionPreference='Stop'
# Downloads the CC0 Poly Haven assets used by the slime city stage and verifies
# the official MD5 of every file. The game only uses the local copies.
$repoPath=Split-Path $PSScriptRoot -Parent
$headers=@{'User-Agent'='neo_Engine-CityStageImport/1.0'}
$records=@()
function Save-Asset($record,[string]$relative){
    if(!$record.url -or !$record.md5){throw "Missing metadata: $relative"}
    $path=Join-Path $repoPath $relative
    [void][IO.Directory]::CreateDirectory((Split-Path $path -Parent))
    if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm MD5).Hash -ine $record.md5){
        for($attempt=1;;++$attempt){
            try{Invoke-WebRequest -Uri $record.url -Headers $headers -OutFile "$path.download";break}
            catch{if($attempt -ge 4){throw};Start-Sleep -Seconds (2*$attempt)}
        }
        if((Get-FileHash -LiteralPath "$path.download" -Algorithm MD5).Hash -ine $record.md5){throw "Checksum mismatch: $relative"}
        Move-Item -LiteralPath "$path.download" -Destination $path -Force
    }
    $script:records+=@{path=$relative;url=$record.url;md5=$record.md5;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    Write-Output "Verified $relative"
}
function Save-Maps($files,[string]$id){
    Save-Asset $files.Diffuse.'2k'.jpg "Resources/Textures/PolyHaven/${id}_diff_2k.jpg"
    foreach($map in @('nor_dx','Rough','AO')){
        if($files.$map){Save-Asset $files.$map.'2k'.jpg "Resources/Textures/PolyHaven/${id}_${map}_2k.jpg"}
    }
}
# Single-material scanned/authored props: one albedo + DirectX normal/roughness/AO.
$models=@('street_rat','cannon_01','wooden_crate_01','wooden_crate_02','cardboard_box_01','trashbag','plastic_crate_01','plastic_crate_02',
    'industrial_pastic_container','plastic_container','metal_jerrycan','propane_tank','old_tyre','WetFloorSign_01','hand_truck',
    'covered_car','korean_public_payphone_01','utility_box_01','utility_box_02','power_box_01','concrete_road_barrier','concrete_road_barrier_02',
    'water_manhole_cover','gothic_statue')
# Tiling PBR surfaces for roads, pavements, modern facades, shutters, the barricade and the golem.
$textures=@('asphalt_02','large_grey_tiles','concrete_floor_worn_001','concrete_wall_008','exterior_wall_cladding_02',
    'rectangular_facade_tiles_02','concrete_tile_facade','concrete_slab_wall','grey_tiles','painted_metal_shutter','rusty_metal_shutter',
    'white_bricks','medieval_wood','castle_wall_slates')
foreach($id in $models){
    $f=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -Headers $headers
    Save-Asset $f.fbx.'2k'.fbx "Resources/Models/PolyHaven/${id}_2k.fbx"
    Save-Maps $f $id
}
foreach($id in $textures){
    $f=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -Headers $headers
    Save-Maps $f $id
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repoPath 'Resources/Credits/CityStageSources.json') -Encoding utf8
