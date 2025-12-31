param(
  [string]$RootDir = "."
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $RootDir -PathType Container)) {
  Write-Error "Error: '$RootDir' is not a directory"
  exit 2
}

$status = 0
$exts = @("*.h","*.hpp","*.hh","*.hxx")

Get-ChildItem -LiteralPath $RootDir -Recurse -File -Include $exts | ForEach-Object {
  $file = $_.FullName

  # Read file; be tolerant of different encodings
  try {
    $content = Get-Content -LiteralPath $file -ErrorAction Stop
  } catch {
    Write-Error "Failed to read: $file"
    $status = 1
    return
  }

  # Match pragma once at start of line (allows leading whitespace)
  $hasPragma = $content | Select-String -Quiet -Pattern '^\s*#pragma\s+once\s*$'

  if (-not $hasPragma) {
    [Console]::Error.WriteLine("Missing #pragma once: $file")
    $status = 1
  }
}

exit $status
