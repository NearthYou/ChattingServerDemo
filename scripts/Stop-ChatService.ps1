[CmdletBinding()]
param(
    [ValidateRange(1, 60)]
    [int]$ShutdownTimeoutSeconds = 10
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$composeFile = Join-Path $root 'docker-compose.yml'
$envFile = Join-Path $root '.env.local'
$runFile = Join-Path $root '.run\server.json'
. (Join-Path $PSScriptRoot 'ChatServiceLifecycle.ps1')

if (Test-Path -LiteralPath $runFile -PathType Leaf) {
    $record = Get-Content -LiteralPath $runFile -Raw -Encoding utf8 | ConvertFrom-Json
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $([int]$record.pid)" -ErrorAction SilentlyContinue
    if ($process) {
        $actual = [IO.Path]::GetFullPath($process.ExecutablePath)
        $recorded = [IO.Path]::GetFullPath([string]$record.executable)
        if ($actual -ne $recorded -or [IO.Path]::GetFileName($actual) -ne 'Server.exe') {
            throw 'The recorded PID does not belong to the owned chat server process.'
        }

        if ([string]::IsNullOrWhiteSpace([string]$record.stopEvent)) {
            throw 'The run record does not contain the owned server stop event.'
        }
        try {
            $stopEvent = [Threading.EventWaitHandle]::OpenExisting([string]$record.stopEvent)
        } catch {
            throw 'Could not open the owned server stop event.'
        }
        try {
            if (-not $stopEvent.Set()) { throw 'Could not signal the owned server process.' }
        } finally {
            $stopEvent.Dispose()
        }

        $deadline = [DateTime]::UtcNow.AddSeconds($ShutdownTimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 100
            $process = Get-Process -Id ([int]$record.pid) -ErrorAction SilentlyContinue
        } while ($process -and [DateTime]::UtcNow -lt $deadline)
        if ($process) { throw 'The server did not complete graceful shutdown before the deadline.' }
    }
    Remove-Item -LiteralPath $runFile -Force
}

if (Test-Path -LiteralPath $envFile -PathType Leaf) {
    Push-Location $root
    try {
        $downExitCode = Invoke-NativeCommandForExitCode -Command {
            & docker compose --env-file $envFile -f $composeFile down
        }
        if ($downExitCode -ne 0) { throw "docker compose down failed with exit code $downExitCode." }
    } finally {
        Pop-Location
    }
}
Write-Host 'Chat service stopped. The MySQL named volume was preserved.'
