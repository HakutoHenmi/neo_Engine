$text = Get-Content -Encoding Byte -Path "Resources\Models\Animation\enem.fbx"
$string = [System.Text.Encoding]::ASCII.GetString($text)
[regex]::Matches($string, 'Armature\|[A-Za-z0-9_|.\|]+') | ForEach-Object { $_.Value } | Select-Object -Unique
