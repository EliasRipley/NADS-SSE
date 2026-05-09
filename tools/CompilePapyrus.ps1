param(
    [Parameter(Mandatory = $true)]
    [string]$PapyrusCompilerPath,

    [Parameter(Mandatory = $false)]
    [string]$FlagsPath,

    [Parameter(Mandatory = $false)]
    [string]$ImportRoot,

    [Parameter(Mandatory = $false)]
    [string]$SkyUiSourcePath,

    [Parameter(Mandatory = $false)]
    [switch]$IncludeMcm
)

$ErrorActionPreference = "Stop"

$sourceDir = Join-Path $PSScriptRoot "..\\Data\\Scripts\\Source"
$outputDir = Join-Path $PSScriptRoot "..\\Data\\Scripts"

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

if (-not (Test-Path $PapyrusCompilerPath)) {
    throw "PapyrusCompiler.exe not found: $PapyrusCompilerPath"
}

if ([string]::IsNullOrWhiteSpace($FlagsPath)) {
    $compilerDir = Split-Path -Parent $PapyrusCompilerPath
    $candidateFlags = @(
        (Join-Path $compilerDir "TESV_Papyrus_Flags.flg"),
        (Join-Path $compilerDir "Scripts\\Source\\TESV_Papyrus_Flags.flg")
    )
    $FlagsPath = $candidateFlags | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $FlagsPath -or -not (Test-Path $FlagsPath)) {
    throw "TESV_Papyrus_Flags.flg not found. Provide -FlagsPath explicitly."
}

if ([string]::IsNullOrWhiteSpace($ImportRoot)) {
    $compilerDir = Split-Path -Parent $PapyrusCompilerPath
    $candidateImportRoots = @(
        (Join-Path $compilerDir "Scripts\\Source"),
        (Join-Path $compilerDir "Source"),
        $sourceDir
    )
    $ImportRoot = $candidateImportRoots | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $ImportRoot -or -not (Test-Path $ImportRoot)) {
    throw "Papyrus import root not found. Provide -ImportRoot explicitly."
}

if ($IncludeMcm -and [string]::IsNullOrWhiteSpace($SkyUiSourcePath)) {
    $bundledSkyUiSource = Join-Path $PSScriptRoot "..\\third_party\\SkyUI\\Scripts\\Source"
    if (Test-Path $bundledSkyUiSource) {
        $SkyUiSourcePath = (Resolve-Path $bundledSkyUiSource).Path
        Write-Host "Using bundled SkyUI headers at $SkyUiSourcePath"
    }
}

$importPaths = @()
if (-not [string]::IsNullOrWhiteSpace($SkyUiSourcePath)) {
    if (-not (Test-Path $SkyUiSourcePath)) {
        throw "SkyUI source path not found: $SkyUiSourcePath"
    }
    $importPaths += $SkyUiSourcePath
}

# Keep local script sources ahead of external import roots so local natives win
# when duplicate script names exist in the vanilla compiler source tree.
$importPaths += $sourceDir
$importPaths += $ImportRoot
$fallbackStubPath = Join-Path $PSScriptRoot "papyrus_stubs"
if (Test-Path $fallbackStubPath) {
    # Fallback compatibility stubs for environments with partial vanilla script sources.
    # Keep this last so real script sources in ImportRoot win whenever available.
    $importPaths += $fallbackStubPath
}
$imports = ($importPaths | Select-Object -Unique) -join ";"

$scripts = @(
    "DodgeBlinkNative.psc",
    "DBS_Blink_AME.psc",
    "DodgeBlink_GenericAME.psc"
)

$scripts = $scripts | Where-Object {
    $scriptPath = Join-Path $sourceDir $_
    if (Test-Path $scriptPath) {
        return $true
    }
    Write-Warning "Missing source script: $scriptPath"
    return $false
}

if ($IncludeMcm) {
    $importsList = $imports -split ';'
    $sourceDirResolved = (Resolve-Path $sourceDir).Path
    $hasSkiConfigBase = $false
    foreach ($importPath in $importsList) {
        $skiPath = Join-Path $importPath "SKI_ConfigBase.psc"
        if (-not (Test-Path $skiPath)) {
            continue
        }

        $skiResolved = (Resolve-Path $skiPath).Path
        if ($skiResolved.StartsWith($sourceDirResolved, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Warning "Ignoring local SKI_ConfigBase stub at $skiResolved. Provide -SkyUiSourcePath for real SkyUI MCM compile."
            continue
        }

        if (Test-Path $skiPath) {
            $hasSkiConfigBase = $true
            break
        }
    }

    if ($hasSkiConfigBase) {
        $scripts += "DodgeBlink_MCN.psc"
    } else {
        Write-Warning "IncludeMcm was set, but SKI_ConfigBase.psc was not found in import paths. Skipping DodgeBlink_MCN.psc."
    }
}

foreach ($script in $scripts) {
    & $PapyrusCompilerPath $script `
        -i="$imports" `
        -o="$outputDir" `
        -f="$FlagsPath"
}

Write-Host "Papyrus compile complete -> $outputDir"
