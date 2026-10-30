param([string]$Executable, [string]$Report)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable = Join-Path $projectRoot 'build\x64\StarLibrary.exe' }
if (!$Report) { $Report = Join-Path $projectRoot 'artifacts\defender-scan.json' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Report = [IO.Path]::GetFullPath($Report)
New-Item -ItemType Directory -Path (Split-Path -Parent $Report) -Force | Out-Null
$result = [ordered]@{ Status='unavailable'; Sha256=(Get-FileHash -LiteralPath $Executable).Hash; Reason=''; ExitCode=$null }
$status = $null
try { $status = Get-MpComputerStatus -ErrorAction Stop } catch { $result.Reason = 'Defender status could not be read.' }
if ($status -and $status.AntivirusEnabled -and $status.AMServiceEnabled) {
    $platform = Join-Path $env:ProgramData 'Microsoft\Windows Defender\Platform'
    $candidate = Get-ChildItem -LiteralPath $platform -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'MpCmdRun.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (!$candidate) { $candidate = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe' }
    if (!(Test-Path -LiteralPath $candidate)) { throw 'Enabled Defender has no accessible scan command.' }
    $result.SignatureVersion = $status.AntivirusSignatureVersion
    $result.SignatureUpdated = $status.AntivirusSignatureLastUpdated
    $log = $Report + '.log'
    $launch = @{
        FilePath = $candidate
        ArgumentList = @('-Scan','-ScanType','3','-File',('"' + $Executable + '"'),'-DisableRemediation')
        WindowStyle = 'Hidden'
        PassThru = $true
        RedirectStandardOutput = $log
        RedirectStandardError = $log + '.stderr'
    }
    $process = Start-Process @launch
    if (!$process.WaitForExit(120000)) {
        $process.Kill(); $process.WaitForExit()
        $result.Status = 'failed'; $result.Reason = 'Scan timed out.'
    } else {
        $result.ExitCode = $process.ExitCode
        if ($process.ExitCode -eq 0) { $result.Status = 'passed' }
        else { $result.Status = 'failed'; $result.Reason = 'Threat or scan error; see scan output.' }
    }
    if (!(Test-Path -LiteralPath $Executable) -or (Get-FileHash -LiteralPath $Executable).Hash -ne $result.Sha256) {
        $result.Status = 'failed'; $result.Reason = 'Release executable changed during scan.'
    }
} elseif (!$result.Reason) { $result.Reason = 'Defender is not enabled; security settings were left unchanged.' }
$result | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $Report -Encoding utf8
Write-Host "Defender scan: $($result.Status). $($result.Reason)"
if ($result.Status -eq 'failed') { throw 'Release scan failed.' }
