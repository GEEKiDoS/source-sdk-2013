[CmdletBinding(DefaultParameterSetName='Legacy')]
param (
    [Parameter(ParameterSetName='Legacy',Mandatory=$true, ValueFromPipeline=$true)][System.IO.FileInfo]$File,
    [Parameter(ParameterSetName='Legacy',Mandatory=$true)][string]$Version,
    [Parameter(ParameterSetName='Legacy')][switch]$Dynamic,
    [Parameter(ParameterSetName='Legacy')][System.UInt32]$Threads,
    [Parameter(ParameterSetName='Native',Mandatory=$true)][switch]$Native,
    [Parameter(ParameterSetName='Native')][string]$Root,
    [Parameter(ParameterSetName='Native')][string]$LegacyRoot,
    [Parameter(ParameterSetName='Native')][string]$Game,
    [Parameter(ParameterSetName='Native')][string]$Stage
)

if ($Native) {
    $ErrorActionPreference = 'Stop'
    if ([bool]$Game -eq [bool]$Stage) { throw 'Specify exactly one of -Game or -Stage.' }
    $destination = if ($Game) { $Game } else { $Stage }
    # $PSScriptRoot is not available to Windows PowerShell 5.1 parameter defaults.
    if (!$Root) { $Root = Join-Path $PSScriptRoot '..\..\materialsystem\stdshaders_dx12' }
    if (!$LegacyRoot) { $LegacyRoot = Join-Path $PSScriptRoot '..\..\materialsystem\stdshaders' }
    $rootPath = (Resolve-Path $Root).Path
    $legacyPath = (Resolve-Path $LegacyRoot).Path
    if (!(Test-Path $destination)) { throw "Destination does not exist: $destination" }
    $destination = (Resolve-Path $destination).Path
    $compiler = Join-Path $PSScriptRoot 'ShaderCompile2.exe'
    $packer = Join-Path $PSScriptRoot 'nativeshaderpack_dx12.exe'
    if (!(Test-Path $packer)) { $packer = Join-Path $PSScriptRoot 'x64\nativeshaderpack_dx12.exe' }
    if (!(Test-Path $packer)) { throw "Missing native shader packer: $packer" }
    # Persistent per-tree staging: compile roots keep their shaders\fxc\*.vcs, so ShaderCompile2's source CRC check
    # skips unchanged shaders on the next run (lightmappedgeneric/vertexlit ps alone take ~40 min from scratch).
    $sha = [Security.Cryptography.SHA1]::Create()
    $rootKey = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($rootPath.ToLowerInvariant()))[0..7] | ForEach-Object { $_.ToString('x2') })
    $staging = Join-Path ([IO.Path]::GetTempPath()) "native-shader-cache-$rootKey"
    try {
        New-Item -ItemType Directory -Path "$staging\legacy", "$staging\native" -Force | Out-Null
        $hlslFiles = @{}
        foreach ($sourceDir in @('hlsl', 'native_src')) {
            $sourcePath = Join-Path $rootPath $sourceDir
            if (!(Test-Path $sourcePath)) { continue }
            Get-ChildItem $sourcePath -Recurse -File | ForEach-Object {
                # ShaderCompile2 resolves #include by file name within -shaderpath; flatten both native trees.
                if ($hlslFiles.ContainsKey($_.Name)) { throw "Duplicate HLSL file name in native trees: $($_.Name)" }
                $hlslFiles[$_.Name] = $_.FullName
            }
        }
        $outputs = @()
        foreach ($manifest in (Get-ChildItem (Join-Path $rootPath 'manifests') -Filter '*.txt' | Sort-Object Name)) {
            foreach ($line in (Get-Content $manifest.FullName)) {
                if ($line -notmatch '^\s*([^#\s]\S*)\s+(vs|ps|cs)\s+(\w+)\s+(20b|20|30|native)(?:\s+(\S+))?\s*$') { continue }
                $source,$stageName,$logical,$profile = $Matches[1],$Matches[2],$Matches[3],$Matches[4]
                $legacySource = if ($profile -eq 'native') { $null } elseif ($Matches[5] -and $Matches[5] -ne '-') { $Matches[5] } else { "$logical.fxc" }
                $outputs += [pscustomobject]@{Source=$source;Stage=$stageName;Logical=$logical;Legacy=$legacySource;Profile=$profile}
            }
        }
        # Each manifest line compiles in its own root so several logical outputs of one base never collide.
        $nativeMap = @()
        $nativeIndex = 0
        foreach ($item in $outputs) {
            $artifact = "native\$($item.Logical)"
            $compileRoot = Join-Path $staging $artifact
            New-Item -ItemType Directory -Path $compileRoot -Force | Out-Null
            # Mirror the flattened tree exactly: files removed from hlsl\ must not stay visible to #include.
            Get-ChildItem $compileRoot -File | Where-Object { !$hlslFiles.ContainsKey($_.Name) } | Remove-Item -Force
            foreach ($name in $hlslFiles.Keys) { Copy-Item $hlslFiles[$name] (Join-Path $compileRoot $name) -Force }
            $stem = [IO.Path]::GetFileNameWithoutExtension($item.Source)
            $compiledBase = [regex]::Replace($stem, '_(vs|ps|cs)(2x|20b|20|30|40|41|50|51|xx)$', '') + "_$($item.Stage)51"
            Push-Location $compileRoot
            try {
                & $compiler -ver 51 -shaderpath $compileRoot $item.Source
                if ($LASTEXITCODE -ne 0) { throw "Native compile failed: $($item.Source) ($LASTEXITCODE)" }
            } finally { Pop-Location }
            if (!(Test-Path (Join-Path $compileRoot "include\$compiledBase.inc")) -or !(Test-Path (Join-Path $compileRoot "shaders\fxc\$compiledBase.vcs"))) {
                throw "Native compiler did not produce expected artifacts for $($item.Source): $compiledBase"
            }
            $nativeMap += "$($item.Source)|$($item.Stage)|$($item.Logical)|$compiledBase|$artifact"
            $nativeIndex++
        }
        # Compile roots of logicals no longer in any manifest are dropped.
        $live = @{}; foreach ($item in $outputs) { $live[$item.Logical] = $true }
        Get-ChildItem "$staging\native" -Directory | Where-Object { !$live.ContainsKey($_.Name) } | Remove-Item -Recurse -Force
        Set-Content "$staging\native-map.txt" $nativeMap
        # Legacy combo ABI references: DX9 sources, with generated shipped-contract overrides where necessary.
        # Native-only logicals intentionally have no legacy compile or fallback reference.
        foreach ($item in ($outputs | Where-Object { $_.Profile -ne 'native' } | Sort-Object Legacy,Profile -Unique)) {
            $original = Join-Path $legacyPath $item.Legacy
            $reference = Join-Path (Join-Path $rootPath 'legacy_reference') $item.Legacy
            if (Test-Path $reference) { $original = $reference }
            if (!(Test-Path $original)) { throw "Missing legacy shader: $original" }
            $folder = Join-Path "$staging\legacy" $item.Profile
            if (!(Test-Path $folder)) {
                New-Item -ItemType Directory -Force $folder | Out-Null
                Copy-Item (Join-Path $legacyPath '*.fxc') $folder -Force
                Copy-Item (Join-Path $legacyPath '*.h') $folder -Force
            }
            $legacyFile = $item.Legacy
            Copy-Item $original (Join-Path $folder $legacyFile) -Force
            # ps_2_0-only logicals (profile 20) are referenced against the -ver 20b compile (no ps20 target exists).
            $legacyVersion = if ($item.Profile -eq '20') { '20b' } else { $item.Profile }
            Push-Location $folder
            try {
                & $compiler -dynamic -ver $legacyVersion -shaderpath $folder $legacyFile
                if ($LASTEXITCODE -ne 0) { throw "Legacy combo compile failed: $($item.Legacy) ($LASTEXITCODE)" }
            } finally { Pop-Location }
        }
        & $packer -root $rootPath -staging $staging -game $destination
        if ($LASTEXITCODE -ne 0) { throw "Native shader validation/publish failed ($LASTEXITCODE)" }
    } finally { if ($sha) { $sha.Dispose() } }
    return
}

if ($Version -notin @("20b", "30", "40", "41", "50", "51")) {
	return
}

$fileList = $File.OpenText()
while ($null -ne ($line = $fileList.ReadLine())) {
	if ($line -match '^\s*$' -or $line -match '^\s*//') {
		continue
	}

	if ($Dynamic) {
		& "$PSScriptRoot\ShaderCompile2" "-dynamic" "-ver" $Version "-shaderpath" $File.DirectoryName $line
		continue
	}

	if ($Threads -ne 0) {
		& "$PSScriptRoot\ShaderCompile2" "-threads" $Threads "-ver" $Version "-shaderpath" $File.DirectoryName $line
		continue
	}

	& "$PSScriptRoot\ShaderCompile2" "-ver" $Version "-shaderpath" $File.DirectoryName $line
}
$fileList.Close()
