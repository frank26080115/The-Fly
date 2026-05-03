$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$searchDirs = @('src', 'include', 'lib', 'test')
$extensions = @('.c', '.cpp', '.h', '.hpp')

if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
    Write-Error 'clang-format was not found on PATH. Install LLVM/clang-format or add it to PATH.'
    exit 1
}

$files = @(
    foreach ($dir in $searchDirs) {
        $path = Join-Path $repoRoot $dir
        if (Test-Path -LiteralPath $path -PathType Container) {
            Get-ChildItem -LiteralPath $path -Recurse -File |
                Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() } |
                ForEach-Object { $_.FullName }
        }
    }
)

if ($files.Count -eq 0) {
    Write-Host 'No C/C++ files found to format.'
    exit 0
}

& clang-format -i -- $files
