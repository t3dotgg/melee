# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Launch Theo's automated slop experiment with the Windows native runtime.
.DESCRIPTION
Creates separate local settings and saves. Existing input settings are preserved
unless -ResetInput is supplied. -PrepareOnly validates and prepares a launch
without starting the game. TexturePack is an extracted root containing GALE01.
#>
[CmdletBinding()]
param(
    [string]$RuntimeDir,
    [string]$DiscDir,
    [string]$UserDir,
    [string]$TexturePack,
    [ValidateSet('Vulkan', 'OGL')]
    [string]$Graphics = 'Vulkan',
    [switch]$NoTextures,
    [switch]$OriginalDiscTiming,
    [switch]$SingleCore,
    [switch]$ResetInput,
    [switch]$PrepareOnly,
    [string[]]$RuntimeArguments = @()
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$RuntimeDir) { $RuntimeDir = Join-Path $repository 'build/native-windows/recomp' }
if (!$UserDir) { $UserDir = Join-Path $repository 'build/native-windows/user' }
if (!$DiscDir) {
    $DiscDir = Join-Path $RuntimeDir 'private/GALE01r2'
    if (!(Test-Path -LiteralPath (Join-Path $DiscDir 'sys/main.dol'))) {
        $DiscDir = Join-Path $repository 'build/disc'
    }
}
if (!$TexturePack -and !$NoTextures) {
    $candidate = Join-Path $repository 'build/native-windows/textures'
    if (Test-Path -LiteralPath (Join-Path $candidate 'GALE01')) { $TexturePack = $candidate }
}
$RuntimeDir = [IO.Path]::GetFullPath($RuntimeDir)
$DiscDir = [IO.Path]::GetFullPath($DiscDir)
$UserDir = [IO.Path]::GetFullPath($UserDir)
$runtime = Join-Path $RuntimeDir 'build/runtime/moderngekko-run.exe'
$module = Join-Path $RuntimeDir 'build/game-clang/gGALE01_recomp.dll'
$moduleCompiler = 'clang-cl'
$launchManifest = Join-Path $RuntimeDir 'build/launch-manifest.json'

function Resolve-ManifestBuildPath([string]$RelativePath, [string]$ExpectedPath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [IO.Path]::IsPathRooted($RelativePath)) {
        throw "Launch manifest paths must be relative to the runtime workspace: $launchManifest"
    }
    $resolved = [IO.Path]::GetFullPath((Join-Path $RuntimeDir $RelativePath))
    $expected = [IO.Path]::GetFullPath((Join-Path $RuntimeDir $ExpectedPath))
    if (!$resolved.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected launch manifest path '$RelativePath'; expected '$ExpectedPath'. Rebuild with native/windows/build.py."
    }
    return $resolved
}

if (Test-Path -LiteralPath $launchManifest) {
    try {
        $manifest = Get-Content -LiteralPath $launchManifest -Raw | ConvertFrom-Json
    } catch {
        throw "Invalid launch manifest: $launchManifest. Rebuild with native/windows/build.py."
    }
    if ($manifest.version -ne 1 -or $manifest.module_compiler -notin @('clang-cl', 'msvc') -or
        $manifest.runtime -isnot [string] -or $manifest.module -isnot [string] -or
        $manifest.module_ipo -isnot [bool]) {
        throw "Unsupported or incomplete launch manifest: $launchManifest. Rebuild with native/windows/build.py."
    }
    $moduleCompiler = $manifest.module_compiler
    $expectedModule = if ($moduleCompiler -eq 'clang-cl') {
        'build/game-clang/gGALE01_recomp.dll'
    } else {
        'build/game/gGALE01_recomp.dll'
    }
    if ($manifest.module_ipo -ne ($moduleCompiler -eq 'clang-cl')) {
        throw "Launch manifest optimization settings do not match its module compiler: $launchManifest"
    }
    $runtime = Resolve-ManifestBuildPath $manifest.runtime 'build/runtime/moderngekko-run.exe'
    $module = Resolve-ManifestBuildPath $manifest.module $expectedModule
}
# A missing manifest selects the optimized module. A stale or missing module
# fails validation below; it must never silently select an older MSVC DLL.
$dol = Join-Path $DiscDir 'sys/main.dol'

foreach ($path in @($runtime, $module, $dol)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Missing or empty launch input: $path. Build with native/windows/build.py first."
    }
}
foreach ($path in @((Join-Path $DiscDir 'files'), (Join-Path $RuntimeDir 'build/runtime/Sys'))) {
    if (!(Test-Path -LiteralPath $path -PathType Container) -or
        !(Get-ChildItem -LiteralPath $path | Select-Object -First 1)) {
        throw "Missing or empty resource directory: $path"
    }
}
if ((Get-FileHash -LiteralPath $dol -Algorithm SHA1).Hash -ne '08E0BF20134DFCB260699671004527B2D6BB1A45') {
    throw 'The game executable must match Melee US v1.02 (GALE01 revision 2).'
}
if ($TexturePack -and !$NoTextures) {
    $TexturePack = [IO.Path]::GetFullPath($TexturePack)
    $textureGame = Join-Path $TexturePack 'GALE01'
    if (!(Test-Path -LiteralPath $textureGame -PathType Container) -or
        !(Get-ChildItem -LiteralPath $textureGame -Filter '*.dds' -Recurse -File | Select-Object -First 1)) {
        throw "TexturePack must contain GALE01 with extracted DDS files: $TexturePack"
    }
}

function Set-IniValue([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    $lines = [Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $Path) { $lines.AddRange([IO.File]::ReadAllLines($Path)) }
    $sectionPattern = '^\s*\[' + [regex]::Escape($Section) + '\]\s*$'
    $keyPattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    $start = -1
    $end = $lines.Count
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $sectionPattern) { $start = $i; continue }
        if ($start -ge 0 -and $lines[$i] -match '^\s*\[') { $end = $i; break }
    }
    if ($start -lt 0) {
        $lines.Add("[$Section]")
        $lines.Add("$Key = $Value")
    } else {
        $found = $false
        for ($i = $start + 1; $i -lt $end; $i++) {
            if ($lines[$i] -match $keyPattern) { $lines[$i] = "$Key = $Value"; $found = $true }
        }
        if (!$found) { $lines.Insert($end, "$Key = $Value") }
    }
    [IO.File]::WriteAllLines($Path, $lines, [Text.UTF8Encoding]::new($false))
}

function Quote-WindowsArgument([string]$Value) {
    # Start-Process joins its ArgumentList; quote for the Windows argv parser.
    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

[IO.Directory]::CreateDirectory($UserDir) | Out-Null
$sessionLock = $null
$previousEnvironment = @{}
try {
    try {
        $sessionLock = [IO.File]::Open((Join-Path $UserDir 'session.lock'),
            [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        throw "This save folder is already in use, or cannot be opened: $UserDir"
    }
    $configDir = Join-Path $UserDir 'Config'
    $logDir = Join-Path $UserDir 'Logs'
    [IO.Directory]::CreateDirectory($configDir) | Out-Null
    [IO.Directory]::CreateDirectory($logDir) | Out-Null
    $padConfig = Join-Path $configDir 'GCPadNew.ini'
    if ($ResetInput -or !(Test-Path -LiteralPath $padConfig)) {
        if (Test-Path -LiteralPath $padConfig) {
            Copy-Item -LiteralPath $padConfig -Destination "$padConfig.$([DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')).bak"
        }
        # DInput names are case-sensitive. Space belongs to the runtime's
        # fast-forward hotkey, so use U/I for the GameCube jump buttons.
        # Xbox mapping matches native/macos/input/MeleeControllerConfig.h:
        # face buttons keep their Xbox labels, D-pad drives movement, and
        # triggers use their full analog travel with a 90% digital threshold.
        $inputDefaults = @'
[GCPad1]
Device = DInput/0/Keyboard Mouse
Buttons/A = J | `XInput/0/Gamepad:Button A`
Buttons/B = K | `XInput/0/Gamepad:Button B`
Buttons/X = U | `XInput/0/Gamepad:Button X`
Buttons/Y = I | `XInput/0/Gamepad:Button Y`
Buttons/Z = O | `XInput/0/Gamepad:Shoulder R`
Buttons/Start = RETURN | `XInput/0/Gamepad:Start`
Main Stick/Up = W | `XInput/0/Gamepad:Left Y+` | `XInput/0/Gamepad:Pad N`
Main Stick/Down = S | `XInput/0/Gamepad:Left Y-` | `XInput/0/Gamepad:Pad S`
Main Stick/Left = A | `XInput/0/Gamepad:Left X-` | `XInput/0/Gamepad:Pad W`
Main Stick/Right = D | `XInput/0/Gamepad:Left X+` | `XInput/0/Gamepad:Pad E`
Main Stick/Modifier = LSHIFT
Main Stick/Modifier/Range = 50.0
Main Stick/Calibration = 100.00
Main Stick/Dead Zone = 10.0
C-Stick/Up = UP | `XInput/0/Gamepad:Right Y+`
C-Stick/Down = DOWN | `XInput/0/Gamepad:Right Y-`
C-Stick/Left = LEFT | `XInput/0/Gamepad:Right X-`
C-Stick/Right = RIGHT | `XInput/0/Gamepad:Right X+`
C-Stick/Calibration = 100.00
C-Stick/Dead Zone = 10.0
Triggers/L = Q | `XInput/0/Gamepad:Trigger L`
Triggers/R = E | `XInput/0/Gamepad:Trigger R`
Triggers/L-Analog = Q | `XInput/0/Gamepad:Trigger L`
Triggers/R-Analog = E | `XInput/0/Gamepad:Trigger R`
Triggers/Dead Zone = 0.0
Triggers/Threshold = 90.0
D-Pad/Up = T
D-Pad/Down = G
D-Pad/Left = F
D-Pad/Right = H
Rumble/Motor = `XInput/0/Gamepad:Motor L` | `XInput/0/Gamepad:Motor R`
[GCPad2]
[GCPad3]
[GCPad4]
'@
        [IO.File]::WriteAllText($padConfig, $inputDefaults + "`n", [Text.UTF8Encoding]::new($false))
    }
    $frontendConfig = Join-Path $UserDir 'config.ini'
    if (!(Test-Path -LiteralPath $frontendConfig)) {
        [IO.File]::WriteAllText($frontendConfig,
            "resolution=1920x1080`nshow_fps_in_title=true`nfullscreen=false`n",
            [Text.UTF8Encoding]::new($false))
    }
    $dolphinConfig = Join-Path $configDir 'Dolphin.ini'
    if (!(Test-Path -LiteralPath $dolphinConfig)) {
        [IO.File]::WriteAllText($dolphinConfig,
            "[Core]`nStaticRecompIdlePC = 0x8034B164`n",
            [Text.UTF8Encoding]::new($false))
    }
    # Match the Mac app's native disc service: remove simulated seek waits
    # while preserving real asynchronous reads and completion callbacks.
    Set-IniValue $dolphinConfig 'Core' 'FastDiscSpeed' (!$OriginalDiscTiming).ToString()
    # System::Initialize uses this to separate CPU and graphics work. The
    # Windows Onett comparison reduced frame-rate dips with this enabled.
    Set-IniValue $dolphinConfig 'Core' 'CPUThread' (!$SingleCore).ToString()
    # Portable settings used by the Mac frontend in its 60 FPS mode.
    Set-IniValue $dolphinConfig 'Core' 'RushFramePresentation' 'True'
    Set-IniValue $dolphinConfig 'Core' 'PrecisionFrameTiming' 'True'
    Set-IniValue $dolphinConfig 'Core' 'VIOverclockEnable' 'False'
    Set-IniValue $dolphinConfig 'Core' 'VIOverclock' '1.0'
    $texturesEnabled = [bool]($TexturePack -and !$NoTextures)
    if ($texturesEnabled) {
        $loadTextures = Join-Path $UserDir 'Load/Textures'
        [IO.Directory]::CreateDirectory($loadTextures) | Out-Null
        $link = Join-Path $loadTextures 'GALE01'
        $existing = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
        if ($existing) {
            if ($existing.LinkType -ne 'Junction' -or
                [IO.Path]::GetFullPath([string]$existing.Target[0]).TrimEnd('\') -ne $textureGame.TrimEnd('\')) {
                throw "Existing texture directory uses another location: $link. Use a separate -UserDir for this texture pack."
            }
        } else {
            New-Item -ItemType Junction -Path $link -Target $textureGame | Out-Null
        }
    }
    $gfxConfig = Join-Path $configDir 'GFX.ini'
    Set-IniValue $gfxConfig 'Settings' 'HiresTextures' $texturesEnabled.ToString()
    Set-IniValue $gfxConfig 'Settings' 'CacheHiresTextures' 'False'
    Set-IniValue $gfxConfig 'Hacks' 'ImmediateXFBEnable' 'True'
    Set-IniValue $gfxConfig 'Hacks' 'CapImmediateXFB' 'False'

    $launchArgs = @('--game', $DiscDir, '--module', $module, '--user-dir', $UserDir,
        '--graphics', $Graphics, '--audio', 'Cubeb', '--no-mods', '--title',
        'Melee for Windows - Automated slop experiment') + $RuntimeArguments
    Write-Host 'Theo''s fully automated slop experiment. Not for serious use or investigation.'
    Write-Host 'No support, maintenance, or human review is promised.'
    Write-Host "Game: $DiscDir"
    Write-Host "Game module ($moduleCompiler): $module"
    Write-Host "Saves and settings: $UserDir"
    Write-Host "Textures enabled: $texturesEnabled"
    Write-Host 'Controls: WASD move, J confirm/attack, K back/special, Enter Start, U/I jump.'
    if ($PrepareOnly) { Write-Host 'Launch inputs and settings prepared.'; return }

    $launchEnvironment = @{
        MODERNGEKKO_STATICRECOMP = '1'
        MELEE_STRICT_NATIVE = '1'
        MELEE_APP_BUNDLE = '1'
        MELEE_RENDER_FPS = '60'
        MELEE_TEXTURE_PACK = $(if ($texturesEnabled) { $TexturePack } else { $null })
    }
    foreach ($name in $launchEnvironment.Keys) {
        $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $launchEnvironment[$name], 'Process')
    }
    $stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
    $stdout = Join-Path $logDir "launch-$stamp.stdout.log"
    $stderr = Join-Path $logDir "launch-$stamp.stderr.log"
    Write-Host "Runtime log: $stderr"
    $arguments = ($launchArgs | ForEach-Object { Quote-WindowsArgument $_ }) -join ' '
    $process = Start-Process -FilePath $runtime -ArgumentList $arguments -WorkingDirectory (Split-Path $runtime) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.ExitCode -ne 0) {
        throw "Melee exited with code $($process.ExitCode). See $stderr"
    }
} finally {
    foreach ($name in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
    if ($sessionLock) { $sessionLock.Dispose() }
}
