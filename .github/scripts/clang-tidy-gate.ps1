# disclaimer: slop

$ErrorActionPreference = 'Stop'

if ($Env:TR_CLANG_TIDY_ENABLE -ne '1') {
    exit 0
}

$clangTidyBin = $args[0]
if (-not $clangTidyBin) {
    Write-Error 'clang-tidy binary path was not provided'
}

# Normalize arguments:
# - CMake passes: pwsh;-File;...;clang-tidy;--extra-arg-before=...;--source=...;--;cl.exe ...
#   PowerShell may receive them already split, but in some cases they come as a single token with semicolons.
function Normalize-Args {
    param([string[]]$InputArgs)
    if ($InputArgs.Count -eq 1 -and $InputArgs[0] -like '*;*') {
        # Split on ';' but keep empty parts out.
        return @($InputArgs[0].Split(';') | Where-Object { $_ -ne '' })
    }
    return $InputArgs
}

$clangTidyArgs = @()
if ($args.Count -gt 1) {
    $clangTidyArgs = Normalize-Args -InputArgs @($args[1..($args.Count - 1)])
}
else {
    $clangTidyArgs = Normalize-Args -InputArgs $args
}


$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.Replace('\', '/')
$workspaceRoot = ($repoRoot | Split-Path -Parent)
$buildRoot = $null
try {
    $buildCandidate = [System.IO.Path]::GetFullPath((Join-Path $workspaceRoot 'obj'))
    if (Test-Path $buildCandidate) {
        $buildRoot = $buildCandidate.Replace('\\', '/')
    }
}
catch {
}
$thirdPartyRoot = Join-Path $repoRoot 'third-party'
$objThirdPartyRoot = $null
try {
    $objThirdPartyCandidate = [System.IO.Path]::GetFullPath((Join-Path $workspaceRoot 'obj/third-party'))
    if (Test-Path $objThirdPartyCandidate) {
        $objThirdPartyRoot = $objThirdPartyCandidate
    }
}
catch {
}
$depsIncludeDir = $null
if (-not [string]::IsNullOrWhiteSpace($Env:DEPS_PREFIX)) {
    $depsIncludeDir = ([System.IO.Path]::GetFullPath((Join-Path $Env:DEPS_PREFIX 'include'))).Replace('\\', '/')
}

# Collect external include dirs (from either clang-tidy args or compiler args) so we can
# re-inject them as -I /imsvc fallback; this covers things like -external:ID:<path>.
function Collect-ExternalIncludeDirs {
    param([string[]]$Args)
    $dirs = @()
    foreach ($arg in $Args) {
        if ($arg -like '-external:*' -or $arg -like '/external:*') {
            $externalValue = $arg.Substring(10)
            if ($externalValue -match '^W\d+$') { continue }
            if ($externalValue -like 'I*') {
                $includePath = $externalValue.Substring(1).Replace('\\', '/')
                if (-not [string]::IsNullOrWhiteSpace($includePath)) {
                    $dirs += $includePath
                }
            }
            continue
        }
    }
    return @($dirs | Sort-Object -Unique)
}

# Extract the translation unit (source file) from various forms:
# - --source=<path> (CMake passes this to tidy)
# - --source <path>
# - Compiler args containing *.c, *.cc, *.cpp, *.cxx, *.mm, *.ixx, *.m, *.hpp, etc.
function Extract-TUSources {
    param([string[]]$ClangArgs, [string[]]$CompilerArgs)
    $sources = @()

    # From --source flags
    for ($i = 0; $i -lt $ClangArgs.Count; ++$i) {
        $arg = $ClangArgs[$i]
        if ($arg -like '--source=*') {
            $val = $arg.Substring(9)
            if (-not [string]::IsNullOrWhiteSpace($val)) { $sources += $val }
            continue
        }
        if ($arg -eq '--source' -and $i + 1 -lt $ClangArgs.Count -and -not ($ClangArgs[$i+1] -eq '--')) {
            $sources += $ClangArgs[$i+1]; $i++; continue
        }
    }

    # Fallback: from compiler args (-c <path>, or bare *.cc)
    if ($sources.Count -eq 0 -and $CompilerArgs.Count -gt 0) {
        $tuPattern = '\\.(c|cc|cpp|cxx|c\+\+|mm|m|ixx|hpp|hh|hxx|ipp)$'
        for ($j = 0; $j -lt $CompilerArgs.Count; ++$j) {
            $carg = $CompilerArgs[$j]
            if ($carg -eq '-c' -and $j + 1 -lt $CompilerArgs.Count) {
                $sources += $CompilerArgs[$j+1]; $j++; continue
            }
            if ($carg -match $tuPattern) { $sources += $carg; continue }
        }
    }

    return @($sources | Sort-Object -Unique)
}

# Derive std arg if provided via -std or /std forms in the compiler args; default to c++20.
function Detect-StdArg {
    param([string[]]$Args)
    $stdArg = '/std:c++20'
    for ($i = 0; $i -lt $Args.Count; ++$i) {
        $arg = $Args[$i]
        if (($arg -eq '-std' -or $arg -eq '/std') -and $i + 1 -lt $Args.Count) {
            $stdValue = $Args[$i + 1]
            if (-not [string]::IsNullOrWhiteSpace($stdValue)) { return "/std:$stdValue" }
        }
        if ($arg -like '-std:*' -or $arg -like '/std:*' -or $arg -like '-std=*' -or $arg -like '/std=*') {
            $sep = $arg.IndexOf(':'); if ($sep -lt 0) { $sep = $arg.IndexOf('=') }
            if ($sep -ge 0 -and $sep + 1 -lt $arg.Length) {
                $stdValue = $arg.Substring($sep + 1)
                if (-not [string]::IsNullOrWhiteSpace($stdValue)) { return "/std:$stdValue" }
            }
        }
    }
    return $stdArg
}

$fallbackIncludeDirs = @()
if (Test-Path $thirdPartyRoot) {
    foreach ($dir in (Get-ChildItem -Path $thirdPartyRoot -Directory)) {
        $includeDir = Join-Path $dir.FullName 'include'
        if (Test-Path $includeDir) {
            $fallbackIncludeDirs += ((Resolve-Path $includeDir).Path.Replace('\\', '/'))
        }

        $sourceDir = Join-Path $dir.FullName 'source'
        if (Test-Path $sourceDir) {
            $fallbackIncludeDirs += ((Resolve-Path $sourceDir).Path.Replace('\\', '/'))
        }
    }

    $madlerRoot = Join-Path $thirdPartyRoot 'madler-crcany'
    if (Test-Path $madlerRoot) {
        $fallbackIncludeDirs += ((Resolve-Path $madlerRoot).Path.Replace('\\', '/'))
    }
}

if (-not [string]::IsNullOrWhiteSpace($objThirdPartyRoot)) {
    foreach ($includeDir in (Get-ChildItem -Path $objThirdPartyRoot -Directory -Recurse | Where-Object { $_.Name -eq 'include' })) {
        $fallbackIncludeDirs += ($includeDir.FullName.Replace('\\', '/'))
    }
}

# Some third-party packages keep headers at the root (e.g., madler-crcany).
# If a directory has any .h/.hpp/.hh files, include the directory itself.
if (Test-Path $thirdPartyRoot) {
    foreach ($dir in (Get-ChildItem -Path $thirdPartyRoot -Directory)) {
        $hasHeadersAtRoot = Get-ChildItem -Path $dir.FullName -Include *.h,*.hpp,*.hh -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $hasHeadersAtRoot) {
            $fallbackIncludeDirs += ((Resolve-Path $dir.FullName).Path.Replace('\\', '/'))
        }
    }
}

$fallbackIncludeDirs = @($fallbackIncludeDirs | Sort-Object -Unique)
$fallbackIncludeArgs = @()
$fallbackIncludeArgs += @($fallbackIncludeDirs | ForEach-Object { "--extra-arg-before=/imsvc$_" })
$fallbackIncludeArgs += @($fallbackIncludeDirs | ForEach-Object { "--extra-arg-before=-I$_" })

$externalIncludeDirs = @($externalIncludeDirs | Sort-Object -Unique)
$externalIncludeArgs = @($externalIncludeDirs | ForEach-Object { "--extra-arg-before=/imsvc$_" })
# Keep a non-system include variant for directories that may contain generated headers or
# project headers that appear in quoted includes.
$externalIncludeArgs += @($externalIncludeDirs | ForEach-Object { "--extra-arg-before=-I$_" })

$depsIncludeArgs = @()
if (-not [string]::IsNullOrWhiteSpace($depsIncludeDir)) {
    $depsIncludeArgs = @("--extra-arg-before=/imsvc$depsIncludeDir")
}

# Ensure generated headers reachable from the build tree (CMake configure writes version.h etc.).
$buildIncludeArgs = @()
if (-not [string]::IsNullOrWhiteSpace($buildRoot)) {
    $buildIncludeArgs += "--extra-arg-before=-I$buildRoot"
}
# The cmake `__run_co_compile` helper passes clang-tidy args followed by `--` and the
# compiler's command line. Preserve this split exactly; otherwise compiler options (like -I)
# get eaten by clang-tidy and you see "unknown argument: -external" et al.
function Split-ClangTidyArgs {
    param([string[]]$Args)
    $sentinelIndex = [Array]::IndexOf($Args, '--')
    if ($sentinelIndex -ge 0) {
        $clangArgs = if ($sentinelIndex -gt 0) { @($Args[0..($sentinelIndex - 1)]) } else { @() }
        $compilerArgs = if ($sentinelIndex + 1 -lt $Args.Count) { @($Args[($sentinelIndex + 1)..($Args.Count - 1)]) } else { @() }
    }
    else {
        $clangArgs = $Args
        $compilerArgs = @()
    }
    return ,@($clangArgs, $compilerArgs, $sentinelIndex)
}

function Extract-Sources {
    param([string[]]$Args)
    $sources = @()
    $remaining = @()
    for ($i = 0; $i -lt $Args.Count; ++$i) {
        $arg = $Args[$i]
        if ($arg -like '--source=*') {
            $val = $arg.Substring(9)
            if (-not [string]::IsNullOrWhiteSpace($val)) { $sources += $val }
            continue
        }
        # Some CMake versions may pass '--source' as a token followed by the path (rare)
        if ($arg -eq '--source') {
            if ($i + 1 -lt $Args.Count -and -not ($Args[$i+1] -eq '--')) {
                $sources += $Args[$i+1]
                $i++
                continue
            }
            # If nothing follows, keep the token to avoid losing info
            $remaining += $arg
            continue
        }
        $remaining += $arg
    }
    return ,@($remaining, $sources)
}

$split = Split-ClangTidyArgs -Args $clangTidyArgs
$clangArgs = $split[0]
$compilerArgs = $split[1]
$sentinelIndex = $split[2]

$extract = Extract-Sources -Args $clangArgs
$clangArgs = $extract[0]
$sourceFlags = $extract[1]

# Derive std arg and external include dirs from the combined args (clang + compiler).
$allArgs = @($clangArgs + $compilerArgs)
$stdArg = Detect-StdArg -Args $allArgs
$externalIncludeDirs = Collect-ExternalIncludeDirs -Args $allArgs

# Get TU sources from both source flags and compiler args.
$sources = Extract-TUSources -ClangArgs $clangTidyArgs -CompilerArgs $compilerArgs
if ($sources.Count -eq 0 -and $sourceFlags.Count -gt 0) {
    $sources = $sourceFlags
}


# Passthrough mode (parity with POSIX gate). Useful for debugging or when normalization is at fault.
if ($Env:TR_CLANG_TIDY_PASSTHRU -eq '1') {
    $normalizedArgs = @($clangArgs + $sources)
    if ($sentinelIndex -ge 0 -and $compilerArgs.Count -gt 0) {
        $normalizedArgs += @('--') + $compilerArgs
    }

    if ($Env:TR_CLANG_TIDY_DEBUG -eq '1') {
        Write-Host "[clang-tidy-gate] PASSTHRU repoRoot=$repoRoot buildRoot=$buildRoot"
        Write-Host "[clang-tidy-gate] rawArgs:`n$($args -join "`n")"
        Write-Host "[clang-tidy-gate] clangArgs:`n$($clangArgs -join "`n")"
        Write-Host "[clang-tidy-gate] sources:`n$($sources -join "`n")"
        Write-Host "[clang-tidy-gate] compilerArgs:`n$($compilerArgs -join "`n")"
    }

    & $clangTidyBin @normalizedArgs
    exit $LASTEXITCODE
}

$prefixArgs = @(
    "--extra-arg-before=-I$repoRoot"
) + $buildIncludeArgs + $depsIncludeArgs + $fallbackIncludeArgs + @(
    # external include dirs collected from compiler args. Add both /imsvc and -I variants.
    $externalIncludeDirs | ForEach-Object { "--extra-arg-before=/imsvc$_" }
    $externalIncludeDirs | ForEach-Object { "--extra-arg-before=-I$_" }
) + @(
    "--extra-arg-before=$stdArg",
    '--extra-arg-before=--driver-mode=cl'
)

if ($sources.Count -eq 0) {
    Write-Host "[clang-tidy-gate] ERROR: No sources found in args; refusing to call clang-tidy without input." -ForegroundColor Red
    if ($Env:TR_CLANG_TIDY_DEBUG -eq '1') {
        Write-Host "[clang-tidy-gate] rawArgs:`n$($args -join "`n")"
        Write-Host "[clang-tidy-gate] clangArgs:`n$($clangArgs -join "`n")"
        Write-Host "[clang-tidy-gate] compilerArgs:`n$($compilerArgs -join "`n")"
        Write-Host "[clang-tidy-gate] externalIncludeDirs:`n$($externalIncludeDirs -join "`n")"
    }
    exit 2
}

# Assemble final arguments:
#   clang-tidy [options + prefixArgs] [sources] -- [compilerArgs]
$normalizedArgs = @($clangArgs + $prefixArgs + $sources)
if ($sentinelIndex -ge 0 -and $compilerArgs.Count -gt 0) {
    $normalizedArgs += @('--') + $compilerArgs
}

# Optional debug logging for CI investigations
if ($Env:TR_CLANG_TIDY_DEBUG -eq '1') {
    Write-Host "[clang-tidy-gate] repoRoot=$repoRoot buildRoot=$buildRoot"
    Write-Host "[clang-tidy-gate] rawArgs:`n$($args -join "`n")"
    Write-Host "[clang-tidy-gate] clangArgs:`n$($clangArgs -join "`n")"
    Write-Host "[clang-tidy-gate] prefixArgs:`n$($prefixArgs -join "`n")"
    Write-Host "[clang-tidy-gate] sources:`n$($sources -join "`n")"
    Write-Host "[clang-tidy-gate] compilerArgs:`n$($compilerArgs -join "`n")"
    Write-Host "[clang-tidy-gate] normalizedArgs:`n$($normalizedArgs -join "`n")"
}

& $clangTidyBin @normalizedArgs
exit $LASTEXITCODE
