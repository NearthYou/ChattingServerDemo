[CmdletBinding()]
param(
    [string]$BindAddress = '127.0.0.1',
    [ValidateRange(1, 65535)]
    [int]$Port = 8888,
    [ValidateRange(10, 600)]
    [int]$HealthTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
if (-not $PSBoundParameters.ContainsKey('BindAddress')) {
    $environmentBindAddress = [Environment]::GetEnvironmentVariable('CHAT_SERVER_BIND_ADDRESS', 'Process')
    if (-not [string]::IsNullOrWhiteSpace($environmentBindAddress)) {
        $BindAddress = $environmentBindAddress
    }
}
$parsedBindAddress = $null
if (-not [Net.IPAddress]::TryParse($BindAddress, [ref]$parsedBindAddress) -or
    $parsedBindAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'BindAddress must be an IPv4 literal such as 127.0.0.1, a LAN address, or 0.0.0.0.'
}
$BindAddress = $parsedBindAddress.ToString()
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$composeFile = Join-Path $root 'docker-compose.yml'
$envFile = Join-Path $root '.env.local'
$runDirectory = Join-Path $root '.run'
$runFile = Join-Path $runDirectory 'server.json'
. (Join-Path $PSScriptRoot 'ChatServiceLifecycle.ps1')

function Resolve-ReleaseBinary {
    param([string]$Name)
    $candidates = @(
        (Join-Path $root "bin\$Name.exe"),
        (Join-Path $root "$Name\x64\Release\$Name.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "$Name x64 Release binary was not found. Run scripts/Package-Release.ps1 or build Release first."
}

function New-Secret {
    $bytes = [byte[]]::new(32)
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    } finally {
        $generator.Dispose()
    }
    return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Read-EnvironmentFile {
    param([string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path -Encoding utf8) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -lt 1) { throw "Invalid entry in .env.local." }
        $values[$trimmed.Substring(0, $separator)] = $trimmed.Substring($separator + 1)
    }
    return $values
}

function Get-VerifiedOwnedServerProcess {
    param(
        [int]$ProcessId,
        [string]$ExpectedExecutable
    )
    $candidate = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
    if (-not $candidate) { return $null }
    $actual = [IO.Path]::GetFullPath($candidate.ExecutablePath)
    $expected = [IO.Path]::GetFullPath($ExpectedExecutable)
    if ($actual -ne $expected -or [IO.Path]::GetFileName($actual) -ne 'Server.exe') {
        throw 'The launched PID no longer belongs to the owned chat server executable.'
    }
    return $candidate
}

function Stop-LaunchedServer {
    param(
        [Diagnostics.Process]$LaunchedProcess,
        [string]$StopEventName,
        [string]$ExpectedExecutable
    )
    if (-not $LaunchedProcess -or $LaunchedProcess.HasExited) { return }
    $null = Get-VerifiedOwnedServerProcess -ProcessId $LaunchedProcess.Id -ExpectedExecutable $ExpectedExecutable

    $gracefulStarted = $false
    $eventDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        try {
            $stopEvent = [Threading.EventWaitHandle]::OpenExisting($StopEventName)
            try {
                $gracefulStarted = $stopEvent.Set()
            } finally {
                $stopEvent.Dispose()
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    } while (-not $gracefulStarted -and [DateTime]::UtcNow -lt $eventDeadline)

    if ($gracefulStarted) {
        $shutdownDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $owned = Get-VerifiedOwnedServerProcess -ProcessId $LaunchedProcess.Id -ExpectedExecutable $ExpectedExecutable
        } while ($owned -and [DateTime]::UtcNow -lt $shutdownDeadline)
        if (-not $owned) { return }
    }

    $owned = Get-VerifiedOwnedServerProcess -ProcessId $LaunchedProcess.Id -ExpectedExecutable $ExpectedExecutable
    if ($owned) {
        Stop-Process -Id $LaunchedProcess.Id -Force
    }
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker CLI was not found.'
}
& docker compose version *> $null
if ($LASTEXITCODE -ne 0) { throw 'Docker Compose is unavailable.' }
& docker info --format '{{.ServerVersion}}' *> $null
if ($LASTEXITCODE -ne 0) { throw 'Docker Engine is not running.' }

$serverExe = Resolve-ReleaseBinary -Name 'Server'
$null = Resolve-ReleaseBinary -Name 'Client'

$driverOverride = [Environment]::GetEnvironmentVariable('CHAT_DB_DRIVER', 'Process')
$drivers = @(Get-OdbcDriver -Platform '64-bit' -ErrorAction Stop)
if ($driverOverride) {
    $selectedDriver = $drivers | Where-Object Name -CEQ $driverOverride | Select-Object -First 1
} else {
    $selectedDriver = $drivers |
        Where-Object { $_.Name -match 'MySQL ODBC' -and $_.Name -match 'Unicode' } |
        Select-Object -First 1
}
if (-not $selectedDriver) {
    throw 'Install the x64 MySQL Connector/ODBC Unicode driver, or set CHAT_DB_DRIVER to its exact registered name.'
}

if (-not (Test-Path -LiteralPath $envFile -PathType Leaf)) {
    $environmentLines = @(
        "MYSQL_ROOT_PASSWORD=$(New-Secret)"
        'CHAT_DB_NAME=chatdb'
        'CHAT_DB_USER=chatapp'
        "CHAT_DB_PASSWORD=$(New-Secret)"
    )
    [IO.File]::WriteAllLines($envFile, $environmentLines, [Text.UTF8Encoding]::new($false))
}
$settings = Read-EnvironmentFile -Path $envFile
foreach ($required in 'MYSQL_ROOT_PASSWORD', 'CHAT_DB_NAME', 'CHAT_DB_USER', 'CHAT_DB_PASSWORD') {
    if (-not $settings.ContainsKey($required) -or [string]::IsNullOrWhiteSpace($settings[$required])) {
        throw ".env.local is missing $required."
    }
}

if (Test-Path -LiteralPath $runFile -PathType Leaf) {
    throw 'A recorded server process already exists. Run scripts/Stop-ChatService.ps1 first.'
}

$runningMysqlContainer = (@(& docker compose --env-file $envFile -f $composeFile ps --status running -q mysql) -join '').Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the existing MySQL service state.' }
$mysqlWasRunningBeforeStartup = -not [string]::IsNullOrWhiteSpace($runningMysqlContainer)

$launchedProcess = $null
$stopEventName = $null
Push-Location $root
try {
    & docker compose --env-file $envFile -f $composeFile up -d
    if ($LASTEXITCODE -ne 0) { throw 'docker compose up failed.' }

    $containerId = (@(& docker compose --env-file $envFile -f $composeFile ps -q mysql) -join '').Trim()
    if (-not $containerId) { throw 'The MySQL container was not created.' }
    $deadline = [DateTime]::UtcNow.AddSeconds($HealthTimeoutSeconds)
    do {
        $health = (@(& docker inspect --format '{{.State.Health.Status}}' $containerId 2>$null) -join '').Trim()
        if ($health -eq 'healthy') { break }
        if ($health -eq 'unhealthy') {
            throw 'MySQL authentication health check failed. The current .env.local may not match the preserved MySQL volume.'
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($health -ne 'healthy') { throw 'Timed out waiting for MySQL health.' }

    [Environment]::SetEnvironmentVariable('CHAT_DB_HOST', '127.0.0.1', 'Process')
    [Environment]::SetEnvironmentVariable('CHAT_DB_PORT', '3307', 'Process')
    [Environment]::SetEnvironmentVariable('CHAT_DB_NAME', $settings.CHAT_DB_NAME, 'Process')
    [Environment]::SetEnvironmentVariable('CHAT_DB_USER', $settings.CHAT_DB_USER, 'Process')
    [Environment]::SetEnvironmentVariable('CHAT_DB_PASSWORD', $settings.CHAT_DB_PASSWORD, 'Process')
    if ($driverOverride) {
        [Environment]::SetEnvironmentVariable('CHAT_DB_DRIVER', $driverOverride, 'Process')
    } else {
        [Environment]::SetEnvironmentVariable('CHAT_DB_DRIVER', $null, 'Process')
    }
    $stopEventName = 'Local\ChatService-' + [Guid]::NewGuid().ToString('N')
    [Environment]::SetEnvironmentVariable('CHAT_SERVER_STOP_EVENT', $stopEventName, 'Process')

    New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
    $launchedProcess = Start-Process -FilePath $serverExe `
        -ArgumentList @('--bind-address', $BindAddress, '--port', $Port) `
        -WorkingDirectory (Split-Path -Parent $serverExe) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 500
    if ($launchedProcess.HasExited) { throw "Server exited during startup with code $($launchedProcess.ExitCode)." }
    $runRecord = [ordered]@{
        pid = $launchedProcess.Id
        executable = $serverExe
        startedUtc = [DateTime]::UtcNow.ToString('o')
        port = $Port
        bindAddress = $BindAddress
        stopEvent = $stopEventName
    } | ConvertTo-Json
    [IO.File]::WriteAllText($runFile, $runRecord, [Text.UTF8Encoding]::new($false))
    Write-Host "Chat service started on $BindAddress`:$Port."
} catch {
    $startupFailure = $_
    Stop-LaunchedServer -LaunchedProcess $launchedProcess -StopEventName $stopEventName -ExpectedExecutable $serverExe
    if (Test-Path -LiteralPath $runFile -PathType Leaf) {
        Remove-Item -LiteralPath $runFile -Force
    }
    if (Test-ShouldStopComposeOnStartupFailure -MysqlWasRunningBeforeStartup $mysqlWasRunningBeforeStartup) {
        $rollbackExitCode = Invoke-NativeCommandForExitCode -Command {
            & docker compose --env-file $envFile -f $composeFile down
        }
        if ($rollbackExitCode -ne 0) {
            Write-Warning "Docker Compose rollback failed with exit code $rollbackExitCode."
        }
    }
    throw $startupFailure
} finally {
    [Environment]::SetEnvironmentVariable('CHAT_DB_PASSWORD', $null, 'Process')
    [Environment]::SetEnvironmentVariable('CHAT_SERVER_STOP_EVENT', $null, 'Process')
    if ($settings -and $settings.ContainsKey('CHAT_DB_PASSWORD')) {
        $settings.CHAT_DB_PASSWORD = $null
    }
    Pop-Location
}
