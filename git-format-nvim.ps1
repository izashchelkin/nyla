$branch = git rev-parse --abbrev-ref HEAD
$tmpBranch = "tmp-squash-patch-$(Get-Random)"
$tmpFile = [System.IO.Path]::GetTempFileName() + ".patch"
$stashed = $false

try {
    $stashOut = git stash 2>&1
    if ($stashOut -notmatch "No local changes") { $stashed = $true }

    git checkout -b $tmpBranch origin/master 2>$null
    git merge --squash $branch 2>$null
    git commit -m "squashed" 2>$null
    git format-patch --stdout -1 HEAD | Out-File $tmpFile -Encoding utf8

    git checkout $branch 2>$null
    git branch -D $tmpBranch 2>$null

    if ($stashed) { git stash pop 2>$null }

    nvim $tmpFile
}
finally {
    git checkout $branch 2>$null
    git branch -D $tmpBranch -f 2>$null
    if ($stashed) { git stash pop 2>$null }
    if (Test-Path $tmpFile) { Remove-Item $tmpFile }
}
