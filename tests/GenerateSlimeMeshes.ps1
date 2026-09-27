$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$output = Join-Path $PSScriptRoot '../Resources/Models/Ink'
foreach ($kind in @('beam','ring')) {
    $lines = [Collections.Generic.List[string]]::new()
    $rows = if ($kind -eq 'beam') { 24 } else { 48 }
    $cols = 32
    for ($j=0; $j -le $rows; ++$j) {
        for ($i=0; $i -le $cols; ++$i) {
            $u=$i/[double]$cols; $v=$j/[double]$rows
            $a=$u*2*[Math]::PI; $b=$v*2*[Math]::PI
            if ($kind -eq 'beam') {
                $radius=1-.35*[Math]::Pow($v,6)
                $x=[Math]::Cos($a)*$radius; $y=[Math]::Sin($a)*$radius; $z=$v
                $nx=[Math]::Cos($a); $ny=[Math]::Sin($a); $nz=0
            } else {
                $x=(1+.07*[Math]::Cos($a))*[Math]::Cos($b)
                $y=(1+.07*[Math]::Cos($a))*[Math]::Sin($b); $z=.07*[Math]::Sin($a)
                $nx=[Math]::Cos($a)*[Math]::Cos($b); $ny=[Math]::Cos($a)*[Math]::Sin($b); $nz=[Math]::Sin($a)
            }
            $lines.Add([string]::Format($culture,'v {0:F6} {1:F6} {2:F6}',$x,$y,$z))
            $lines.Add([string]::Format($culture,'vt {0:F6} {1:F6}',$u,$v))
            $lines.Add([string]::Format($culture,'vn {0:F6} {1:F6} {2:F6}',$nx,$ny,$nz))
        }
    }
    for ($j=0; $j -lt $rows; ++$j) {
        for ($i=0; $i -lt $cols; ++$i) {
            $a=$j*($cols+1)+$i+1; $b=$a+1; $c=$a+$cols+1; $d=$c+1
            $lines.Add("f $a/$a/$a $b/$b/$b $c/$c/$c")
            $lines.Add("f $b/$b/$b $d/$d/$d $c/$c/$c")
        }
    }
    if ($kind -eq 'beam') {
        # Close the pressure column: viewing down its axis must reveal a bright core, not an empty tube.
        $next=($rows+1)*($cols+1)+1
        foreach($end in @(0,1)){
            $normal=if($end -eq 0){-1}else{1}
            $radius=if($end -eq 0){1.0}else{.65}
            $center=$next
            $lines.Add("v 0 0 $end");$lines.Add('vt 0.5 0.5');$lines.Add("vn 0 0 $normal");++$next
            for($i=0;$i -le $cols;++$i){$a=$i*2*[Math]::PI/$cols
                $lines.Add([string]::Format($culture,'v {0:F6} {1:F6} {2}',([Math]::Cos($a)*$radius),([Math]::Sin($a)*$radius),$end))
                $lines.Add('vt 0.5 0.5');$lines.Add("vn 0 0 $normal");++$next
            }
            for($i=0;$i -lt $cols;++$i){$a=$center+1+$i;$b=$a+1
                if($end -eq 0){$lines.Add("f $center/$center/$center $b/$b/$b $a/$a/$a")}else{$lines.Add("f $center/$center/$center $a/$a/$a $b/$b/$b")}
            }
        }
    }
    [IO.File]::WriteAllLines((Join-Path $output "slime-$kind.obj"),$lines)
}
