$tmp = [System.IO.Path]::GetTempFileName()
try {
    nvim $tmp
    
    if ((Test-Path $tmp) -and (Get-Item $tmp).Length -gt 0) {
        git am --whitespace=fix $tmp
    } else {
        Write-Warning "Datei leer. git am wurde nicht ausgeführt."
    }
} 
finally {
    if (Test-Path $tmp) { Remove-Item $tmp }
}