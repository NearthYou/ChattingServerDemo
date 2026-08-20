[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scripts = @(
    (Join-Path $root 'scripts\Start-ChatService.ps1'),
    (Join-Path $root 'scripts\Stop-ChatService.ps1'),
    (Join-Path $root 'scripts\Package-Release.ps1')
)

foreach ($script in $scripts) {
    $tokens = $null
    $errors = $null
    [Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -ne 0) {
        throw "PowerShell parser rejected $script`: $($errors[0].Message)"
    }
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker CLI is required for the Compose contract test.'
}

$localEnvironment = Join-Path $root '.env.local'
$createdEnvironment = $false
if (-not (Test-Path -LiteralPath $localEnvironment -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $root '.env.example') -Destination $localEnvironment
    $createdEnvironment = $true
}

try {
    $configuration = & docker compose --env-file $localEnvironment -f (Join-Path $root 'docker-compose.yml') config
    if ($LASTEXITCODE -ne 0) { throw 'Docker Compose rejected docker-compose.yml.' }
    $rendered = $configuration -join "`n"
    if ($rendered -notmatch 'mysql:8\.4' -or
        $rendered -notmatch '127\.0\.0\.1' -or
        $rendered -notmatch 'schema\.sql') {
        throw 'Rendered Compose configuration lost its image, loopback binding, or schema mount.'
    }
} finally {
    if ($createdEnvironment -and (Test-Path -LiteralPath $localEnvironment)) {
        Remove-Item -LiteralPath $localEnvironment -Force
    }
}

Write-Host 'PASS PowerShell syntax and Docker Compose contract'
