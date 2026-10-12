param(
    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"
$metadataPath = Join-Path $PSScriptRoot "payload.metadata.json"
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
    throw "payload metadata was not found."
}
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if (-not $metadata.parts -or $metadata.parts.Count -eq 0) {
    throw "payload parts were not listed in metadata."
}

$parts = @()
foreach ($expected in $metadata.parts) {
    $part = Join-Path $PSScriptRoot ([string]$expected.name)
    if (-not (Test-Path -LiteralPath $part -PathType Leaf)) {
        throw "payload part is missing: $($expected.name)"
    }
    $item = Get-Item -LiteralPath $part
    if ($item.Length -ne [int64]$expected.size) {
        throw "payload part size mismatch: $($expected.name)"
    }
    $hash = (Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne ([string]$expected.sha256).ToLowerInvariant()) {
        throw "payload part checksum mismatch: $($expected.name)"
    }
    $parts += $item
}

$archive = Join-Path $env:TEMP ("sindre-installer-" + [guid]::NewGuid().ToString("N") + ".zip")
try {
    $output = [System.IO.File]::Open($archive, [System.IO.FileMode]::CreateNew)
    try {
        foreach ($part in $parts) {
            $input = [System.IO.File]::OpenRead($part.FullName)
            try {
                $input.CopyTo($output, 1048576)
            }
            finally {
                $input.Dispose()
            }
        }
    }
    finally {
        $output.Dispose()
    }
    $archiveItem = Get-Item -LiteralPath $archive
    if ($archiveItem.Length -ne [int64]$metadata.archive.size) {
        throw "payload archive size mismatch."
    }
    $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($archiveHash -ne ([string]$metadata.archive.sha256).ToLowerInvariant()) {
        throw "payload archive checksum mismatch."
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
}
finally {
    Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
}
