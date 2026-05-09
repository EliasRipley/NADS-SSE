param(
    [Parameter(Mandatory = $true)]
    [string]$GameDataPath,

    [Parameter(Mandatory = $false)]
    [bool]$PruneLegacyNestedData = $true
)

$ErrorActionPreference = "Stop"

$repoRoot = Join-Path $PSScriptRoot ".."

if (Test-Path (Join-Path $GameDataPath "SkyrimSE.exe")) {
    throw "GameDataPath points to the game root. Pass the Skyrim Data folder or an MO2 mod folder."
}

if (-not (Test-Path $GameDataPath)) {
    New-Item -ItemType Directory -Force -Path $GameDataPath | Out-Null
}

if ($PruneLegacyNestedData) {
    $legacyDataRoot = Join-Path $GameDataPath "Data"
    if (Test-Path $legacyDataRoot) {
        Remove-Item -Path $legacyDataRoot -Recurse -Force
        Write-Host "Removed legacy nested staging root: $legacyDataRoot"
    }
}

# Keep staging targets flat by removing previously nested smoke-stage folders.
Get-ChildItem -Path $GameDataPath -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "stage_smoke*" } |
    ForEach-Object {
        Remove-Item -Path $_.FullName -Recurse -Force
        Write-Host "Removed nested smoke-stage folder: $($_.FullName)"
    }

$legacyCustomConditionsDst = Join-Path $GameDataPath "Meshes\\Actors\\Character\\Animations\\DynamicAnimationReplacer\\_CustomConditions\\940100"
if (Test-Path $legacyCustomConditionsDst) {
    Remove-Item -Path $legacyCustomConditionsDst -Recurse -Force
    Write-Host "Removed legacy DAR custom-conditions path from staging: $legacyCustomConditionsDst"
}

$staticDll = Join-Path $repoRoot "build\\vs2022-release-vcpkg-static\\Release\\DodgeBlinkShout.dll"
$dataDll = Join-Path $repoRoot "Data\\SKSE\\Plugins\\DodgeBlinkShout.dll"
$usingStaticDll = $false

if (Test-Path $staticDll) {
    $dllSource = $staticDll
    $usingStaticDll = $true
    Write-Host "Using static plugin DLL: $dllSource"
} elseif (Test-Path $dataDll) {
    $dllSource = $dataDll
    Write-Warning "Static plugin DLL not found, falling back to Data copy: $dllSource"
} else {
    throw "Missing plugin DLL. Build static preset first or provide Data\\SKSE\\Plugins\\DodgeBlinkShout.dll."
}

$pluginDst = Join-Path $GameDataPath "SKSE\\Plugins\\DodgeBlinkShout.dll"
$pluginDstDir = Split-Path $pluginDst -Parent
New-Item -ItemType Directory -Force -Path $pluginDstDir | Out-Null
Copy-Item -Path $dllSource -Destination $pluginDst -Force
Write-Host "Copied plugin DLL from $dllSource"

$items = @(
    "Data\\DodgeBlinkShout.esp",
    "Data\\SKSE\\Plugins\\DodgeBlinkShout.ini",
    "Data\\Scripts\\DodgeBlinkNative.pex",
    "Data\\Scripts\\DBS_Blink_AME.pex",
    "Data\\Scripts\\DodgeBlink_GenericAME.pex",
    "Data\\Scripts\\DodgeBlink_MCN.pex"
)

$optionalRuntimeItems = @(
    "Data\\SKSE\\Plugins\\fmt.dll",
    "Data\\SKSE\\Plugins\\spdlog.dll"
)

$frameworkDirs = @(
    "Data\\Meshes\\OpenAnimationReplacer\\NADS",
    "Data\\Nemesis_Engine\\mod\\tkuc",
    "Data\\Nemesis_Engine\\mod\\dmco",
    "Data\\MCM\\Config\\DodgeFramework",
    "Data\\MCM\\Config\\Dodge_MCO-DXP"
)

$sourceItems = @(
    "Data\\Scripts\\Source\\DodgeBlinkNative.psc",
    "Data\\Scripts\\Source\\DBS_Blink_AME.psc",
    "Data\\Scripts\\Source\\DodgeBlink_GenericAME.psc",
    "Data\\Scripts\\Source\\DodgeBlink_MCN.psc",
    "third_party\\SkyUI\\Scripts\\Source\\SKI_ConfigBase.psc",
    "third_party\\SkyUI\\Scripts\\Source\\SKI_QuestBase.psc"
)

foreach ($item in $items) {
    $src = Join-Path $repoRoot $item
    if (-not (Test-Path $src)) {
        Write-Warning "Missing: $src"
        continue
    }

    $dst = Join-Path $GameDataPath ($item -replace "^Data\\", "")
    $dstDir = Split-Path $dst -Parent
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    Copy-Item -Path $src -Destination $dst -Force
    Write-Host "Copied $item"
}

foreach ($item in $optionalRuntimeItems) {
    if ($usingStaticDll) {
        break
    }

    $src = Join-Path $repoRoot $item
    if (-not (Test-Path $src)) {
        continue
    }

    $dst = Join-Path $GameDataPath ($item -replace "^Data\\", "")
    $dstDir = Split-Path $dst -Parent
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    Copy-Item -Path $src -Destination $dst -Force
    Write-Host "Copied optional runtime dependency $item"
}

foreach ($dir in $frameworkDirs) {
    $srcDir = Join-Path $repoRoot $dir
    if (-not (Test-Path $srcDir)) {
        continue
    }

    $dstDir = Join-Path $GameDataPath ($dir -replace "^Data\\", "")
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    Copy-Item -Path (Join-Path $srcDir "*") -Destination $dstDir -Recurse -Force
    Write-Host "Copied animation framework directory $dir"
}

foreach ($item in $sourceItems) {
    $src = Join-Path $repoRoot $item
    if (-not (Test-Path $src)) {
        continue
    }

    $leaf = Split-Path $item -Leaf
    $destinations = @(
        (Join-Path "Source\\Scripts" $leaf),
        (Join-Path "Scripts\\Source" $leaf)
    )

    foreach ($rel in ($destinations | Select-Object -Unique)) {
        $dst = Join-Path $GameDataPath $rel
        $dstDir = Split-Path $dst -Parent
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
        Copy-Item -Path $src -Destination $dst -Force
        Write-Host "Copied source script $rel"
    }
}

Write-Host "Staging complete."
