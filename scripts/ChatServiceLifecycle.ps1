function Test-ShouldStopComposeOnStartupFailure {
    param([bool]$MysqlWasRunningBeforeStartup)

    return -not $MysqlWasRunningBeforeStartup
}
