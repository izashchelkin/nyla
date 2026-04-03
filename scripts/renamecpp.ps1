function RenameCpp() {
    param ( 
        [string]$from, 
        [string]$to
    ) 

    Get-ChildItem -path .\ -Recurse -File -Name -Filter *.h |
        ForEach-Object {(Get-Content -Raw -Path ([string]$_)) -replace $from,$to |
        Set-Content -NoNewLine -Path ([string]$_)}

    Get-ChildItem -path .\ -Recurse -File -Name -Filter *.cc |
        ForEach-Object {(Get-Content -Raw -Path ([string]$_)) -replace $from,$to |
        Set-Content -NoNewLine -Path ([string]$_)}
}
