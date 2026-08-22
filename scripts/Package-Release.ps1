[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'vswhere.exe was not found.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }

foreach ($project in 'Server\Server.vcxproj', 'Client\Client.vcxproj') {
    & $msbuild (Join-Path $root $project) /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Release build failed for $project." }
}

$artifactRoot = [IO.Path]::GetFullPath((Join-Path $root 'artifacts'))
$distRoot = [IO.Path]::GetFullPath((Join-Path $root 'dist'))
if (-not $artifactRoot.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or
    -not $distRoot.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output resolved outside the repository.'
}
$stage = Join-Path $artifactRoot ("package-" + [Guid]::NewGuid().ToString('N'))
$zip = Join-Path $distRoot 'ChatService-x64-Release.zip'
New-Item -ItemType Directory -Path (Join-Path $stage 'bin') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'scripts') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'Server\Database') -Force | Out-Null
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null

try {
    Copy-Item -LiteralPath (Join-Path $root 'Server\x64\Release\Server.exe') -Destination (Join-Path $stage 'bin\Server.exe')
    Copy-Item -LiteralPath (Join-Path $root 'Client\x64\Release\Client.exe') -Destination (Join-Path $stage 'bin\Client.exe')
    Copy-Item -LiteralPath (Join-Path $root 'scripts\Start-ChatService.ps1') -Destination (Join-Path $stage 'scripts\Start-ChatService.ps1')
    Copy-Item -LiteralPath (Join-Path $root 'scripts\ChatServiceLifecycle.ps1') -Destination (Join-Path $stage 'scripts\ChatServiceLifecycle.ps1')
    Copy-Item -LiteralPath (Join-Path $root 'scripts\Stop-ChatService.ps1') -Destination (Join-Path $stage 'scripts\Stop-ChatService.ps1')
    Copy-Item -LiteralPath (Join-Path $root 'scripts\Package-Release.ps1') -Destination (Join-Path $stage 'scripts\Package-Release.ps1')
    Copy-Item -LiteralPath (Join-Path $root 'Server\Database\schema.sql') -Destination (Join-Path $stage 'Server\Database\schema.sql')
    Copy-Item -LiteralPath (Join-Path $root 'docker-compose.yml') -Destination (Join-Path $stage 'docker-compose.yml')
    Copy-Item -LiteralPath (Join-Path $root '.env.example') -Destination (Join-Path $stage '.env.example')
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $stage 'README.md')
    if (Test-Path -LiteralPath $zip -PathType Leaf) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Host "Created $zip"
