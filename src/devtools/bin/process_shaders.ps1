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
    [Parameter(ParameterSetName='Native')][string]$Stage,
    # Comma-separated logicals to compile; every other logical republishes its existing staged artifacts unchanged.
    # PowerShell turns "-Only a,b" into an array, so the names are rejoined with commas for ShaderCompiler3.
    [Parameter(ParameterSetName='Native')][string[]]$Only
)

if ($Native) {
    $ErrorActionPreference = 'Stop'
    if ([bool]$Game -eq [bool]$Stage) { throw 'Specify exactly one of -Game or -Stage.' }
    # ShaderCompiler3 compiles every manifest logical to SM 6.0 DXIL in process (per-combo cache), writes the combo
    # includes and the DX9 combo-ABI references, then validates and publishes (formerly nativeshaderpack_dx12).
    $compiler = Join-Path $PSScriptRoot 'x64\ShaderCompiler3.exe'
    if (!(Test-Path $compiler)) { throw "Missing ShaderCompiler3: $compiler (build shadercompiler3_win64)" }
    $arguments = @(if ($Game) { '-game', $Game } else { '-stage', $Stage })
    if ($Root) { $arguments += '-root', $Root }
    if ($LegacyRoot) { $arguments += '-legacyroot', $LegacyRoot }
    if ($Only) { $arguments += '-only', ($Only -join ',') }
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "ShaderCompiler3 failed ($LASTEXITCODE)" }
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
