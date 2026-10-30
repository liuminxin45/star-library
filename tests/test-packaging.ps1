param([ValidateSet('x64','x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $projectRoot 'scripts\pack.ps1')
$tool = Get-StarUpx -ProjectRoot $projectRoot
$source = Join-Path $projectRoot "build\$Architecture\StarLibrary.unpacked.exe"
$folder = Join-Path $projectRoot ("build\$Architecture\packaging-test-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
$script:checks = 0
function Assert-Check([bool]$Condition, [string]$Label) {
    if (!$Condition) { throw "Packaging regression: $Label" }
    $script:checks++
    Write-Host "PASS $Label"
}
function Assert-Rejected([scriptblock]$Action, [string]$Label) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-Check $rejected $Label
}
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$destination = Join-Path $folder 'preserved.exe'
[IO.File]::WriteAllText($destination, 'existing output must survive failed packaging')
$preservedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
$invalid = Join-Path $folder 'invalid.exe'
[IO.File]::WriteAllText($invalid, 'not a PE executable')
Assert-Rejected { Compress-StarExecutable -Source $invalid -Destination $destination -Upx $tool } 'invalid input is rejected'
Assert-Check ((Get-FileHash -LiteralPath $destination).Hash -eq $preservedHash) 'failed packaging preserves an existing destination'
Assert-Rejected { Compress-StarExecutable -Source $source -Destination $source -Upx $tool } 'in-place compression is rejected'
Assert-Check ((Get-FileHash -LiteralPath $source).Hash -eq $sourceHash) 'uncompressed source is preserved'
$tamperedTool = Join-Path $folder 'tampered-upx.exe'
$bytes = [IO.File]::ReadAllBytes($tool)
$bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
[IO.File]::WriteAllBytes($tamperedTool, $bytes)
Assert-Rejected { Get-StarUpx -ProjectRoot $projectRoot -Path $tamperedTool } 'modified tool is rejected before execution'
Assert-Rejected { Compress-StarExecutable -Source $source -Destination $destination -Upx $tamperedTool } 'compression rejects a modified tool'
Assert-Check ((Get-FileHash -LiteralPath $destination).Hash -eq $preservedHash) 'tool verification failure preserves existing output'
$oldEnvironment = [Environment]::GetEnvironmentVariable('UPX', 'Process')
try {
    $env:UPX = '--this-option-must-never-be-used'
    $record = Compress-StarExecutable -Source $source -Destination (Join-Path $folder 'isolated.exe') -Upx $tool
    Assert-Check $record.ExactRoundtrip 'inherited UPX options cannot change the verified packing policy'
    Assert-Check (($record.DllCharacteristics -band 0x140) -eq 0x140) 'packed executable preserves ASLR and DEP'
    Assert-Check ($record.Version -eq (Get-StarPeInfo -Path $source).Version) 'packed version resource is preserved'
} finally {
    [Environment]::SetEnvironmentVariable('UPX', $oldEnvironment, 'Process')
}
$wrapperExit = $null
$previousErrorPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & (Join-Path $projectRoot 'build.cmd') -Architecture invalid *> (Join-Path $folder 'invalid-architecture.log')
    $wrapperExit = $LASTEXITCODE
} finally { $ErrorActionPreference = $previousErrorPreference }
Assert-Check ($wrapperExit -eq 1) 'build entrypoint propagates argument validation failure'
"$script:checks packaging checks passed ($Architecture)."
# The deliberately rejected child process leaves LASTEXITCODE=1. Report the
# test suite's success explicitly so CI does not reuse that expected failure.
exit 0
