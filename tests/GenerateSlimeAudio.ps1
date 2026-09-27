# Original procedural pressure / suction / acid sounds; no external assets.
$ErrorActionPreference='Stop'
foreach ($kind in @('pressure','recall','acid')) {
    $rate=48000
    $seconds=if($kind -eq 'pressure'){1.25}elseif($kind -eq 'recall'){.28}else{.18}
    $count=[int]($rate*$seconds)
    $stream=[IO.File]::Create((Join-Path $PSScriptRoot "../Resources/Sound/slime-$kind.wav"))
    $writer=[IO.BinaryWriter]::new($stream)
    $rng=[Random]::new(731);$low=0.0;$smooth=0.0;$phase=0.0;$peak=0.0
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$writer.Write([int](36+$count*2))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$writer.Write([int]16)
        $writer.Write([int16]1);$writer.Write([int16]1);$writer.Write([int]$rate);$writer.Write([int]($rate*2));$writer.Write([int16]2);$writer.Write([int16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'));$writer.Write([int]($count*2))
        for($i=0;$i -lt $count;++$i){
            $t=$i/[double]$rate;$f=$t/$seconds;$noise=$rng.NextDouble()*2-1
            $low=.97*$low+.03*$noise;$smooth=.97*$smooth+.03*$low
            if($kind -eq 'pressure'){
                $phase+=2*[Math]::PI*(72+90*[Math]::Exp(-$t*8))/$rate
                $v=.28*[Math]::Sin($phase)+.07*[Math]::Sin($phase*2)+.025*[Math]::Sin($phase*3)+.32*$smooth
                $env=[Math]::Pow([Math]::Sin([Math]::Min(1,$t/.035)*[Math]::PI/2),2)*[Math]::Pow(1-$f,.8)
            }elseif($kind -eq 'recall'){
                $phase+=2*[Math]::PI*(130+600*$f*$f)/$rate
                $v=.18*[Math]::Sin($phase)+.25*$smooth
                $env=[Math]::Sin([Math]::PI*$f)
            }else{$v=.35*$smooth+.035*[Math]::Sin($t*900);$env=[Math]::Pow([Math]::Sin([Math]::PI*$f),2)}
            $sample=$v*$env;$peak=[Math]::Max($peak,[Math]::Abs($sample))
            if([Math]::Abs($sample) -ge .8){throw 'Insufficient audio headroom.'}
            $writer.Write([int16]($sample*32767))
        }
        Write-Output ("{0}: 48kHz, peak {1:F3}, no clipping" -f $kind,$peak)
    }finally{$writer.Dispose();$stream.Dispose()}
}
