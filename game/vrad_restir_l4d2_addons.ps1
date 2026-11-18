# Bakes enhanced lighting (<map>.light) for every map inside L4D2 add-on VPKs.
#   vrad_restir_l4d2_addons.bat [vrad_restir options] [addon.vpk ...]
# Without .vpk arguments every mod_l4d2\addons\*.vpk is processed (the folder the game mounts add-ons from).
# Without options the Dead Center settings are used; -restir_shadowmaps is always added.
# Each add-on's maps, materials and models are extracted to %TEMP% (the add-on itself is not modified) and every map is
# baked through vrad_restir_l4d2.bat with that folder mounted ahead of the game content, so the add-on's own materials
# and models are found. Results go to mod_l4d2\maps; the extracted files are deleted afterwards.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaults = @('-final', '-restir_skyibl', '-restir_skyambientscale', '4', '-restir_bouncescale', '2.5', '-restir_maxbounces', '2',
              '-restir_hlight_density', '2', '-restir_ambientgrid', '28')

$options = @(); $vpks = @()
foreach ($a in $args) {
    if ("$a" -like '*.vpk') { $vpks += (Resolve-Path -LiteralPath $a).ProviderPath } else { $options += "$a" }
}
if (-not $options.Count) { $options = $defaults }
if ($options -notcontains '-restir_shadowmaps') { $options = @('-restir_shadowmaps') + $options }
if (-not $vpks.Count) {
    $vpks = @(Get-ChildItem -LiteralPath (Join-Path $here 'mod_l4d2\addons') -Filter *.vpk -File -ErrorAction SilentlyContinue |
              ForEach-Object FullName)
}
if (-not $vpks.Count) { Write-Host 'No add-on VPKs given and none in mod_l4d2\addons.'; exit 1 }

# Extracts the maps/, materials/ and models/ members of a single-file VPK (v1 or v2) under $root and returns the map
# names. The baker reads loose files rather than the archive itself.
function Expand-VpkContent([string]$path, [string]$root) {
    $stream = [IO.File]::OpenRead($path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt32() -ne 0x55AA1234) { throw "$path is not a VPK" }
        $version = $reader.ReadUInt32(); $treeSize = $reader.ReadUInt32()
        if ($version -eq 2) { $null = $reader.ReadBytes(16) } elseif ($version -ne 1) { throw "$path`: VPK version $version" }
        $dataStart = $stream.Position + $treeSize
        $readString = {
            $bytes = New-Object Collections.Generic.List[byte]
            while (($b = $reader.ReadByte()) -ne 0) { $bytes.Add($b) }
            [Text.Encoding]::UTF8.GetString($bytes.ToArray())
        }
        $entries = New-Object Collections.Generic.List[object]
        while (($ext = & $readString) -ne '') {
            while (($dir = & $readString) -ne '') {
                while (($name = & $readString) -ne '') {
                    $null = $reader.ReadUInt32()                       # CRC
                    $preload = $reader.ReadUInt16(); $archive = $reader.ReadUInt16()
                    $offset = $reader.ReadUInt32(); $length = $reader.ReadUInt32()
                    $null = $reader.ReadUInt16()                       # terminator
                    $preloadBytes = $reader.ReadBytes($preload)
                    if ($dir -match '^(maps|materials|models)(/|$)') {
                        if ($archive -ne 0x7FFF) { throw "$path`: split VPK archives are not supported" }
                        $entries.Add([pscustomobject]@{ Dir = $dir; File = "$name.$ext"; Preload = $preloadBytes
                                                        Offset = $dataStart + $offset; Length = $length })
                    }
                }
            }
        }
        $maps = @()
        foreach ($e in $entries) {
            $target = Join-Path $root ($e.Dir -replace '/', '\')
            [IO.Directory]::CreateDirectory($target) | Out-Null
            $data = New-Object byte[] ($e.Preload.Length + $e.Length)
            [Array]::Copy($e.Preload, $data, $e.Preload.Length)
            $stream.Position = $e.Offset; $null = $stream.Read($data, $e.Preload.Length, $e.Length)
            [IO.File]::WriteAllBytes((Join-Path $target $e.File), $data)
            if ($e.Dir -ieq 'maps' -and $e.File -like '*.bsp') { $maps += [IO.Path]::GetFileNameWithoutExtension($e.File) }
        }
        $maps
    } finally { $reader.Close() }
}

$failed = @(); $baked = @()
foreach ($vpk in $vpks) {
    $root = Join-Path $env:TEMP ('vrad_restir_addon\' + [IO.Path]::GetFileNameWithoutExtension($vpk))
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    Write-Host "Extracting $([IO.Path]::GetFileName($vpk)) ..."
    $maps = @(Expand-VpkContent $vpk $root)
    if (-not $maps.Count) { Write-Host "$vpk`: no maps"; Remove-Item -LiteralPath $root -Recurse -Force; continue }
    $env:L4D2_ADDON_PATH = $root
    foreach ($map in $maps) {
        Write-Host "=== $map ($([IO.Path]::GetFileName($vpk)))"
        $mapOptions = @($options)
        while ($true) {
            & cmd /c "`"$here\vrad_restir_l4d2.bat`" $($mapOptions -join ' ') `"$root\maps\$map.bsp`" 2>&1" | Tee-Object -Variable lines | Out-Host
            $code = $LASTEXITCODE
            # A large map can need a probe grid over -restir_ambientgrid_budget. Retry with wider probe spacing (the
            # baker never coarsens on its own) up to 64 units; a bigger budget would cost the same VRAM in game.
            if ($code -eq 0 -or -not ($lines -match 'over the -restir_ambientgrid_budget')) { break }
            $i = [Array]::IndexOf($mapOptions, '-restir_ambientgrid')
            $spacing = if ($i -ge 0) { [int]$mapOptions[$i + 1] } else { 32 }
            if ($spacing -ge 64) { break }
            $next = [Math]::Min(64, $spacing + 8)
            Write-Host "$map`: probe grid over budget at spacing $spacing; retrying with -restir_ambientgrid $next"
            if ($i -ge 0) { $mapOptions[$i + 1] = "$next" } else { $mapOptions += @('-restir_ambientgrid', "$next") }
        }
        if ($code -ne 0) { $failed += $map } else { $baked += $map }
    }
    Remove-Item -LiteralPath $root -Recurse -Force
}
Remove-Item Env:L4D2_ADDON_PATH -ErrorAction SilentlyContinue
Write-Host "Baked: $($baked.Count) map(s) $($baked -join ', ')"
if ($failed.Count) { Write-Host "FAILED: $($failed -join ', ')"; exit 1 }
