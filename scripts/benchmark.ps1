# LoomSearch 一键 benchmark：固定语料 + 固定参数，结果可复现。
param(
    [string]$Bin = ".\build\loomsearch.exe",
    [string]$Corpus = ".\out\corpus",
    [int]$Files = 2000,
    [int]$Words = 80,
    [int]$Threads = 4,
    [int]$Queries = 30
)

& $Bin corpus-gen $Corpus --files $Files --words $Words
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $Bin bench-search $Corpus --threads $Threads --queries $Queries
exit $LASTEXITCODE
