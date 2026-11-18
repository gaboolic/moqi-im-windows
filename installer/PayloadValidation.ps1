# Read-only package validation shared by staging and installer compilation.
function Assert-MoqiPayload {
    param([Parameter(Mandatory = $true)][string] $Root)
    function Assert-PE([string] $Relative, [int[]] $Machines) {
        $path = Join-Path $Root $Relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing payload: $path" }
        $stream = [IO.File]::OpenRead($path)
        $reader = New-Object IO.BinaryReader($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE: $path" }
            $stream.Position = 0x3c
            $offset = $reader.ReadInt32()
            if ($offset -lt 64 -or $offset -gt ($stream.Length - 24)) { throw "Invalid PE offset: $path" }
            $stream.Position = $offset
            if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -notin $Machines) {
                throw "Wrong PE architecture: $path"
            }
        } finally { $reader.Dispose() }
    }
    Assert-PE 'MoqiTextService.dll' @(0x14c)
    Assert-PE 'x64\MoqiTextService.dll' @(0x8664)
    Assert-PE 'SetupHelper.exe' @(0x14c)
    Assert-PE 'MoqiLauncher.exe' @(0x14c)
    Assert-PE 'moqi-ime\server.exe' @(0x14c, 0x8664)
    $backendText = Get-Content -LiteralPath (Join-Path $Root 'backends.json') -Raw -Encoding UTF8
    if (-not $backendText.TrimStart().StartsWith('[')) { throw 'backends.json must be an array' }
    $backends = @($backendText | ConvertFrom-Json)
    if ($backends.Count -eq 0) { throw 'backends.json must be a nonempty array' }
    $guids = @{}
    $count = 0
    foreach ($backend in $backends) {
        if ($backend.name -isnot [string] -or -not $backend.name -or $backend.name -in @('.', '..') -or $backend.name -match '[\\/:\"]') {
            throw 'Invalid backend directory name'
        }
        $profiles = Join-Path $Root ($backend.name + '\input_methods')
        if (-not (Test-Path -LiteralPath $profiles -PathType Container)) { throw "Missing profiles: $profiles" }
        $backendCount = 0
        foreach ($directory in Get-ChildItem -LiteralPath $profiles -Directory) {
            if ($directory.Name.StartsWith('.')) { continue }
            $path = Join-Path $directory.FullName 'ime.json'
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
            $profile = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($field in @('name', 'guid', 'locale', 'fallbackLocale', 'icon')) {
                if ($profile.$field -isnot [string]) { throw "Invalid profile field ${field}: $path" }
            }
            $guid = [guid]::Empty
            if (-not $profile.name -or -not [guid]::TryParse($profile.guid, [ref]$guid) -or $guid -eq [guid]::Empty -or $guids.ContainsKey($guid.ToString())) {
                throw "Invalid or duplicate profile GUID/name: $path"
            }
            $guids[$guid.ToString()] = $true
            $localeValid = $false
            foreach ($locale in @($profile.locale, $profile.fallbackLocale)) {
                try { if ($locale -and [Globalization.CultureInfo]::GetCultureInfo($locale).LCID -gt 0) { $localeValid = $true } } catch {}
            }
            if (-not $localeValid) { throw "Invalid profile locale: $path" }
            if (-not $profile.icon) { throw "Missing profile icon: $path" }
            $icon = $profile.icon
            if (-not [IO.Path]::IsPathRooted($icon)) { $icon = Join-Path $directory.FullName $icon }
            if (-not (Test-Path -LiteralPath $icon -PathType Leaf)) { throw "Missing profile icon: $icon" }
            $backendCount++
            $count++
        }
        if ($backendCount -eq 0) { throw "No profiles in backend: $profiles" }
    }
    if ($count -eq 0) { throw 'No language profiles in payload' }
}
