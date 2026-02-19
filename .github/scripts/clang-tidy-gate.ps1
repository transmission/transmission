# disclaimer: slop

$ErrorActionPreference = 'Stop'

if ($Env:TR_CLANG_TIDY_ENABLE -ne '1') {
    exit 0
}

$clangTidyBin = $args[0]
if (-not $clangTidyBin) {
    Write-Error 'clang-tidy binary path was not provided'
}

$clangTidyArgs = @()
if ($args.Count -gt 1) {
    $clangTidyArgs = @($args[1..($args.Count - 1)])
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

$normalizedArgs = @()
$stdArg = '/std:c++20'
$externalIncludeDirs = @()
$skipNext = $false
for ($i = 0; $i -lt $clangTidyArgs.Count; ++$i) {
    if ($skipNext) {
        $skipNext = $false
        continue
    }

    $arg = $clangTidyArgs[$i]

    if ($arg -eq '-std' -or $arg -eq '/std') {
        if ($i + 1 -lt $clangTidyArgs.Count) {
            $stdValue = $clangTidyArgs[$i + 1]
            if (-not [string]::IsNullOrWhiteSpace($stdValue)) {
                $stdArg = "/std:$stdValue"
            }
        }
        $skipNext = $true
        continue
    }

    if ($arg -like '-std:*' -or $arg -like '/std:*' -or $arg -like '-std=*' -or $arg -like '/std=*') {
        $separatorIndex = $arg.IndexOf(':')
        if ($separatorIndex -lt 0) {
            $separatorIndex = $arg.IndexOf('=')
        }

        if ($separatorIndex -ge 0 -and $separatorIndex + 1 -lt $arg.Length) {
            $stdValue = $arg.Substring($separatorIndex + 1)
            if (-not [string]::IsNullOrWhiteSpace($stdValue)) {
                $stdArg = "/std:$stdValue"
            }
        }
        continue
    }

    if ($arg -eq '-external') {
        continue
    }

    if ($arg -eq '-I' -or $arg -eq '/I' -or $arg -eq '-isystem' -or $arg -eq '/imsvc') {
        if ($i + 1 -lt $clangTidyArgs.Count) {
            $normalizedArgs += $arg
            $normalizedArgs += $clangTidyArgs[$i + 1]
            $skipNext = $true
        }
        continue
    }

    if ($arg -like '-I*' -or $arg -like '/I*' -or $arg -like '-isystem*' -or $arg -like '/imsvc*') {
        $normalizedArgs += $arg
        continue
    }

    if ($arg -like '-external:*') {
        $externalValue = $arg.Substring(10)
        if ($externalValue -match '^W\d+$') {
            continue
        }

        if ($externalValue -like 'I*') {
            $includePath = $externalValue.Substring(1).Replace('\\', '/')
            $externalIncludeDirs += $includePath
            continue
        }

        continue
    }

    if ($arg -like '-DPACKAGE_DATA_DIR=*') {
        continue
    }

    $normalizedArgs += $arg
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
    foreach ($arg in $Args) {
        if ($arg -like '--source=*') {
            $val = $arg.Substring(9)
            if (-not [string]::IsNullOrWhiteSpace($val)) {
                $sources += $val
            }
            continue
        }
        # Some CMake versions may pass '--source' as a token followed by the path (rare)
        if ($arg -eq '--source') {
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
$sources = $extract[1]

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
) + $buildIncludeArgs + $depsIncludeArgs + $fallbackIncludeArgs + $externalIncludeArgs + @(
    "--extra-arg-before=$stdArg",
    '--extra-arg-before=--driver-mode=cl'
)

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
