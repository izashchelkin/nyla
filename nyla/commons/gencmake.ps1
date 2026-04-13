Write-Output "PRIVATE"
Get-ChildItem -Filter *.cc -File | ForEach-Object { "   $($_.Name)" }

Write-Output "`nPUBLIC"
Get-ChildItem -Filter *.h -File | ForEach-Object { "   $($_.Name)" }