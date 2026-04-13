Get-ChildItem -Include *.h,*.hpp,*.cc,*.cpp -Recurse -File | ForEach-Object {
    $lines = Get-Content $_.FullName
    $depth = 0
    $inNs = $false
    
    for ($i=0; $i -lt $lines.Count; $i++) {
        $l = $lines[$i] -replace '//.*','' # Ignoriere Line-Comments
        
        if ($l -match '\bnamespace\b') { $inNs = $true }
        
        $depth += ([char[]]$l -eq '{').Count
        $depth -= ([char[]]$l -eq '}').Count
        
        if ($depth -eq 0) { $inNs = $false } # Zurück im globalen Scope
        
        if ($inNs -and $depth -gt 0 -and $l -match '^\s*#include') {
            Write-Host "$($_.FullName):$($i+1) -> $($lines[$i].Trim())" -ForegroundColor Red
        }
    }
}