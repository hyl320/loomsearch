# First run is labelled first; repeats reuse the corpus without flushing OS caches.
param(
    [string]$Bin = ".\build\loomsearch.exe",
    [string]$Corpus = ".\out\benchmark-corpora",
    [int[]]$FileCounts = @(200, 2000),
    [int[]]$WordCounts = @(20, 200),
    [int[]]$ThreadCounts = @(1, 4),
    [int]$Runs = 3,
    [int]$Queries = 30,
    [string]$Output = ".\out\benchmark.csv"
)

$ErrorActionPreference = 'Stop'
if ($Runs -lt 2 -or $Queries -lt 1 -or
    @($FileCounts + $WordCounts + $ThreadCounts | Where-Object { $_ -lt 1 }).Count -gt 0) {
    throw 'Runs must be >= 2; all matrix dimensions and Queries must be positive.'
}
if (Test-Path -LiteralPath $Output) { throw "Results already exist: $Output" }
$exe = (Resolve-Path -LiteralPath $Bin).Path
$corpusRoot = [IO.Path]::GetFullPath($Corpus)
$outputPath = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $corpusRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null

function Invoke-Measured([string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $exe
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($arg in $Arguments) { [void]$start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    $peak = $process.PeakWorkingSet64
    while (-not $process.WaitForExit(1)) {
        $process.Refresh()
        $peak = [Math]::Max($peak, $process.PeakWorkingSet64)
    }
    $process.Refresh()
    try { $peak = [Math]::Max($peak, $process.PeakWorkingSet64) } catch { }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0) { throw "$exe $($Arguments -join ' ') failed: $stderr" }
    return [pscustomobject]@{ Text = $stdout; PeakBytes = $peak }
}

function Read-Metrics([string]$Text) {
    $metrics = @{}
    foreach ($line in $Text -split "`n") {
        if ($line -match '^([a-z_0-9]+): ([0-9]+)') { $metrics[$Matches[1]] = $Matches[2] }
    }
    return $metrics
}

foreach ($files in $FileCounts) {
    foreach ($words in $WordCounts) {
        $case = Join-Path $corpusRoot "files_${files}_words_${words}"
        New-Item -ItemType Directory -Force -Path $case | Out-Null
        & $exe corpus-gen $case --files $files --words $words | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "corpus-gen failed: $case" }
        if (@(Get-ChildItem -LiteralPath $case -File).Count -ne $files) {
            throw "Corpus contains unexpected files: $case"
        }
        foreach ($threads in $ThreadCounts) {
            for ($run = 1; $run -le $Runs; ++$run) {
                $indexPath = Join-Path (Split-Path -Parent $outputPath) 'benchmark-index.bin'
                $dbPath = Join-Path (Split-Path -Parent $outputPath) 'benchmark-index.db'
                $hand = Invoke-Measured @('index', $case, '--threads', "$threads", '--output', $indexPath)
                $sqlite = Invoke-Measured @('sqlite-index', $case, '--threads', "$threads", '--output', $dbPath)
                $bench = Read-Metrics (Invoke-Measured @('bench-search', $case, '--threads', "$threads", '--queries', "$Queries")).Text
                if ($bench.parity_mismatches -ne '0' -or $bench.corpus_files -ne "$files") {
                    throw "Benchmark parity or corpus count failed: $case"
                }
                foreach ($key in @('hand_index_build_ms', 'sqlite_index_build_ms',
                        'hand_single_p50_ns', 'hand_single_p95_ns',
                        'sqlite_single_p50_ns', 'sqlite_single_p95_ns',
                        'hand_and_p50_ns', 'hand_and_p95_ns',
                        'sqlite_and_p50_ns', 'sqlite_and_p95_ns')) {
                    if (-not $bench.ContainsKey($key)) { throw "Missing benchmark metric: $key" }
                }
                [pscustomobject]@{
                    files = $files; words_per_file = $words; threads = $threads
                    run = $run; cache_state = $(if ($run -eq 1) { 'first' } else { 'repeat' })
                    hand_peak_working_set_bytes = $hand.PeakBytes
                    sqlite_peak_working_set_bytes = $sqlite.PeakBytes
                    hand_build_ms = $bench.hand_index_build_ms
                    sqlite_build_ms = $bench.sqlite_index_build_ms
                    hand_single_p50_ns = $bench.hand_single_p50_ns
                    hand_single_p95_ns = $bench.hand_single_p95_ns
                    sqlite_single_p50_ns = $bench.sqlite_single_p50_ns
                    sqlite_single_p95_ns = $bench.sqlite_single_p95_ns
                    hand_and_p50_ns = $bench.hand_and_p50_ns
                    hand_and_p95_ns = $bench.hand_and_p95_ns
                    sqlite_and_p50_ns = $bench.sqlite_and_p50_ns
                    sqlite_and_p95_ns = $bench.sqlite_and_p95_ns
                    parity_mismatches = $bench.parity_mismatches
                } | Export-Csv -LiteralPath $outputPath -NoTypeInformation -Append
                Write-Host "files=$files words=$words threads=$threads run=$run"
            }
        }
    }
}
