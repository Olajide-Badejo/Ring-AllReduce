[CmdletBinding()]
param(
    [switch]$Quick,
    [ValidateSet('float', 'double')]
    [string]$DType = 'float',
    [string]$BuildDir = 'build-real',
    [string]$OutputDir = 'results/local_run',
    [Int64]$MinBytes = 8,
    [Int64]$MaxBytes = 134217728,
    [int]$Iterations = 1000,
    [int]$WarmupIterations = 5,
    [double]$MaxTimePerConfigMs = 300
)

# Runs a real local Microsoft MPI sweep on Windows. The MSYS2 UCRT64 MPI
# development package provides the compatible compiler wrappers; the
# Microsoft MPI runtime provides mpiexec. Unlike Open MPI, Microsoft MPI has
# no coll/tuned MCA controls, so this script compares only the custom ring
# against the vendor's default MPI_Allreduce selection.
#
# Usage:
#   .\scripts\run_local_sweep.ps1 -Quick -BuildDir build-real
#   .\scripts\run_local_sweep.ps1 -DType double -MaxBytes 67108864

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $mpiExec = Get-Command mpiexec.exe -ErrorAction Stop
    $benchmark = Join-Path $BuildDir 'apps\benchmark.exe'
    $pingpong = Join-Path $BuildDir 'apps\pingpong.exe'

    if (-not (Test-Path -LiteralPath $benchmark) -or -not (Test-Path -LiteralPath $pingpong)) {
        throw "Build products not found under '$BuildDir'. Configure and build the project first."
    }

    $processCounts = if ($Quick) { @(2, 4, 8, 16) } else { 2..16 }
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $resultsPath = Join-Path $OutputDir 'results.csv'
    $pingpongPath = Join-Path $OutputDir 'pingpong.csv'
    Remove-Item -LiteralPath $resultsPath, $pingpongPath -Force -ErrorAction SilentlyContinue

    Write-Host "Microsoft MPI local sweep: N = $($processCounts -join ', '), dtype = $DType"
    Write-Host "Output directory: $OutputDir"

    foreach ($numProcs in $processCounts) {
        Write-Host "N = $numProcs: ring, mpi-default"
        & $mpiExec.Source -n $numProcs $benchmark `
            --algorithm ring,mpi-default `
            --dtype $DType `
            --min-bytes $MinBytes `
            --max-bytes $MaxBytes `
            --iterations $Iterations `
            --warmup-iterations $WarmupIterations `
            --max-time-per-config-ms $MaxTimePerConfigMs `
            --output $resultsPath
        if ($LASTEXITCODE -ne 0) {
            throw "benchmark failed at N = $numProcs with exit code $LASTEXITCODE"
        }
    }

    Write-Host 'N = 2: pingpong'
    & $mpiExec.Source -n 2 $pingpong `
        --min-bytes $MinBytes `
        --max-bytes $MaxBytes `
        --iterations $Iterations `
        --warmup-iterations $WarmupIterations `
        --max-time-per-config-ms $MaxTimePerConfigMs `
        --output $pingpongPath
    if ($LASTEXITCODE -ne 0) {
        throw "pingpong failed with exit code $LASTEXITCODE"
    }

    Write-Host "Done. Analyze with: python analysis\run_full_analysis.py --results $resultsPath --pingpong $pingpongPath --outdir report"
}
finally {
    Pop-Location
}
