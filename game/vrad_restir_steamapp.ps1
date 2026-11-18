# Prints the install folder of a Steam app (steamapps\common\<name>) from the first Steam library that has it.
# Used by vrad_restir_l4d2.bat to find Left 4 Dead 2 and Source SDK Base 2013 Multiplayer. Exits 1 if not found.
param([Parameter(Mandatory = $true)][string]$Name)

$steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
if (-not $steam) { exit 1 }
$libraries = @($steam)
$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
if (Test-Path -LiteralPath $vdf) {
    foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
        $libraries += $m.Groups[1].Value -replace '\\\\', '\'
    }
}
foreach ($library in $libraries) {
    $path = "$library\steamapps\common\$Name"  # plain string: Join-Path fails on libraries whose drive is gone
    if (Test-Path -LiteralPath $path) {
        (Resolve-Path -LiteralPath $path).ProviderPath
        exit 0
    }
}
exit 1
