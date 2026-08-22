[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lifecycleHelper = Join-Path $root 'scripts\ChatServiceLifecycle.ps1'
$scripts = @(
    $lifecycleHelper,
    (Join-Path $root 'scripts\Start-ChatService.ps1'),
    (Join-Path $root 'scripts\Stop-ChatService.ps1'),
    (Join-Path $root 'scripts\Package-Release.ps1')
)

if (-not (Test-Path -LiteralPath $lifecycleHelper -PathType Leaf)) {
    throw 'Chat service lifecycle helper is missing.'
}
. $lifecycleHelper
if (-not (Test-ShouldStopComposeOnStartupFailure -MysqlWasRunningBeforeStartup $false)) {
    throw 'Startup-owned MySQL would not be rolled back.'
}
if (Test-ShouldStopComposeOnStartupFailure -MysqlWasRunningBeforeStartup $true) {
    throw 'Preexisting running MySQL would be composed down during rollback.'
}

$startScriptText = Get-Content -LiteralPath (Join-Path $root 'scripts\Start-ChatService.ps1') -Raw -Encoding utf8
$preexistingProbe = $startScriptText.IndexOf('ps --status running -q mysql', [StringComparison]::Ordinal)
$composeUp = $startScriptText.IndexOf('up -d', [StringComparison]::Ordinal)
if ($preexistingProbe -lt 0 -or $composeUp -lt 0 -or $preexistingProbe -gt $composeUp) {
    throw 'Start script does not record preexisting running MySQL before Compose up.'
}
if ($startScriptText -notmatch 'Test-ShouldStopComposeOnStartupFailure\s+-MysqlWasRunningBeforeStartup\s+\$mysqlWasRunningBeforeStartup') {
    throw 'Start script rollback does not use the recorded MySQL state.'
}
if ($startScriptText -notmatch '\[string\]\$BindAddress\s*=\s*''127\.0\.0\.1''' -or
    $startScriptText -notmatch '''--bind-address''' -or
    $startScriptText -notmatch 'Chat service started on' -or
    $startScriptText -notmatch '\$BindAddress') {
    throw 'Start script does not preserve the loopback default and pass or report the configured bind address.'
}

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
