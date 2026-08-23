function Test-ShouldStopComposeOnStartupFailure {
    param([bool]$MysqlWasRunningBeforeStartup)

    return -not $MysqlWasRunningBeforeStartup
}

function Invoke-NativeCommandForExitCode {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Command
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Command 2>&1 | Out-Null
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    return $exitCode
}
