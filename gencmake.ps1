param(
    [string]$Dir = "."
)

if (-not (Test-Path -Path $Dir -PathType Container)) {
    Write-Error "Directory not found: $Dir"
    exit 1
}

$files = Get-ChildItem -Path $Dir -File

# Common files
$privateCommon = $files | Where-Object {
    $_.Extension -eq '.cc' -and
    $_.BaseName -notmatch '(_windows|_linux|_vulkan)$'
}

$publicCommon = $files | Where-Object {
    $_.Extension -eq '.h' -and
    $_.BaseName -notmatch '(_windows|_linux|_vulkan)$'
}

# Windows files
$windowsPrivate = $files | Where-Object {
    $_.Extension -eq '.cc' -and
    $_.BaseName -match '(_windows)$'
}

$windowsPublic = $files | Where-Object {
    $_.Extension -eq '.h' -and
    $_.BaseName -match '(_windows)$'
}

# Linux files
$linuxPrivate = $files | Where-Object {
    $_.Extension -eq '.cc' -and
    $_.BaseName -match '(_linux)$'
}

$linuxPublic = $files | Where-Object {
    $_.Extension -eq '.h' -and
    $_.BaseName -match '(_linux)$'
}

# Vulkan files
$vulkanPrivate = $files | Where-Object {
    $_.Extension -eq '.cc' -and
    $_.BaseName -match '(_vulkan)$'
}

$vulkanPublic = $files | Where-Object {
    $_.Extension -eq '.h' -and
    $_.BaseName -match '(_vulkan)$'
}

Write-Output "target_sources (`${TARGET} PRIVATE"
$privateCommon | ForEach-Object { Write-Output "    $($_.Name)" }
Write-Output ")"

Write-Output ""

Write-Output "target_sources (`${TARGET} PUBLIC"
$publicCommon | ForEach-Object { Write-Output "    $($_.Name)" }
Write-Output ")"

if ($windowsPrivate.Count -gt 0 -or $windowsPublic.Count -gt 0 -or $linuxPrivate.Count -gt 0 -or $linuxPublic.Count -gt 0) {
    Write-Output ""
    Write-Output "if (WIN32)"

    if ($windowsPrivate.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PRIVATE"
        $windowsPrivate | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    if ($windowsPublic.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PUBLIC"
        $windowsPublic | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    Write-Output "else()"

    if ($linuxPrivate.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PRIVATE"
        $linuxPrivate | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    if ($linuxPublic.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PUBLIC"
        $linuxPublic | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    Write-Output "endif()"
}

if ($vulkanPrivate.Count -gt 0 -or $vulkanPublic.Count -gt 0) {
    Write-Output ""
    Write-Output "if (Vulkan_FOUND)"

    if ($vulkanPrivate.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PRIVATE"
        $vulkanPrivate | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    if ($vulkanPublic.Count -gt 0) {
        Write-Output "    target_sources (`${TARGET} PUBLIC"
        $vulkanPublic | ForEach-Object { Write-Output "        $($_.Name)" }
        Write-Output "    )"
    }

    Write-Output "endif()"
}