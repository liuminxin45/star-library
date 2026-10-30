# UPX is a build-time tool only. The distributed application needs no installer.
$script:StarUpxVersion = '5.3.0'
$script:StarUpxArchiveHash = 'd127996c76615e431be810e9c8d2910dbe3dd400600c5c0c50276fc5a17a9705'
$script:StarUpxExecutableHash = 'ff30a651c9e41a2bbb363825fac9e7e23d92871c1ac82813a264e3b8d1cd302b'

function Get-StarUpx {
    param([Parameter(Mandatory=$true)][string]$ProjectRoot, [string]$Path)
    if ($Path) {
        $tool = (Resolve-Path -LiteralPath $Path).Path
        if ((Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash -ne $script:StarUpxExecutableHash) {
            throw 'UPX executable SHA-256 does not match the pinned official version.'
        }
        return $tool
    }
    $cache = [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'build\tools'))
    $tool = Join-Path $cache "upx-$script:StarUpxVersion-win64\upx.exe"
    if (Test-Path -LiteralPath $tool) { return Get-StarUpx -ProjectRoot $ProjectRoot -Path $tool }
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $staging = Join-Path $cache ('download-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging | Out-Null
    try {
        $archive = Join-Path $staging 'upx.zip'
        $url = "https://github.com/upx/upx/releases/download/v$script:StarUpxVersion/upx-$script:StarUpxVersion-win64.zip"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive -TimeoutSec 120
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $script:StarUpxArchiveHash) {
            throw 'UPX archive SHA-256 does not match the pinned official release.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $staging
        $extracted = Join-Path $staging "upx-$script:StarUpxVersion-win64"
        $verified = Get-StarUpx -ProjectRoot $ProjectRoot -Path (Join-Path $extracted 'upx.exe')
        $destination = Split-Path -Parent $tool
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        # Retain the upstream LICENSE/COPYING and manuals in the local tool cache.
        foreach ($file in Get-ChildItem -LiteralPath $extracted -File) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destination $file.Name) -Force
        }
        return Get-StarUpx -ProjectRoot $ProjectRoot -Path $tool
    } finally {
        $resolvedStaging = [IO.Path]::GetFullPath($staging)
        if (!$resolvedStaging.StartsWith($cache + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to remove a staging directory outside the build tool cache.'
        }
        if (Test-Path -LiteralPath $resolvedStaging) { Remove-Item -LiteralPath $resolvedStaging -Recurse -Force }
    }
}

function Get-StarPeInfo {
    param([Parameter(Mandatory=$true)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Invalid DOS/PE header.' }
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($pe -lt 64 -or $pe -gt $bytes.Length - 96 -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) {
        throw 'Invalid PE header offset or signature.'
    }
    [PSCustomObject]@{
        Machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
        DllCharacteristics = [BitConverter]::ToUInt16($bytes, $pe + 24 + 70)
        Version = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
    }
}

function Compress-StarExecutable {
    param([Parameter(Mandatory=$true)][string]$Source,
          [Parameter(Mandatory=$true)][string]$Destination,
          [Parameter(Mandatory=$true)][string]$Upx)
    $Source = (Resolve-Path -LiteralPath $Source).Path
    $Destination = [IO.Path]::GetFullPath($Destination)
    if ($Source -eq $Destination) { throw 'Compression requires a separate source and destination.' }
    if ((Get-FileHash -LiteralPath $Upx -Algorithm SHA256).Hash -ne $script:StarUpxExecutableHash) {
        throw 'UPX executable SHA-256 does not match the pinned official version.'
    }
    $original = Get-StarPeInfo -Path $Source
    $originalHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $folder = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $nonce = [Guid]::NewGuid().ToString('N')
    $packed = Join-Path $folder ("pack-$nonce.exe")
    $roundtrip = Join-Path $folder ("roundtrip-$nonce.exe")
    try {
        & $Upx --no-env --best --lzma --exact --strip-relocs=0 -o $packed $Source | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'UPX compression failed.' }
        & $Upx --no-env -t $packed | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'UPX integrity check failed.' }
        & $Upx --no-env -d -o $roundtrip $packed | Out-Host
        if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $roundtrip -Algorithm SHA256).Hash -ne $originalHash) {
            throw 'UPX roundtrip does not restore the original executable exactly.'
        }
        if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $originalHash) {
            throw 'Uncompressed source changed during packing.'
        }
        $result = Get-StarPeInfo -Path $packed
        if ($result.Machine -ne $original.Machine -or $result.DllCharacteristics -ne $original.DllCharacteristics -or
            $result.Version -ne $original.Version) { throw 'Packing changed architecture, security flags or version resources.' }
        $packedHash = (Get-FileHash -LiteralPath $packed -Algorithm SHA256).Hash
        # Only publish the destination after all file-integrity checks succeed.
        Move-Item -LiteralPath $packed -Destination $Destination -Force
        [PSCustomObject]@{
            Tool = "UPX $script:StarUpxVersion"
            Options = '--no-env --best --lzma --exact --strip-relocs=0'
            SourceBytes = (Get-Item -LiteralPath $Source).Length
            PackedBytes = (Get-Item -LiteralPath $Destination).Length
            SourceSha256 = $originalHash.ToLowerInvariant()
            PackedSha256 = $packedHash.ToLowerInvariant()
            ExactRoundtrip = $true
            Machine = $result.Machine
            DllCharacteristics = $result.DllCharacteristics
            Version = $result.Version
        }
    } finally {
        foreach ($temporary in @($packed, $roundtrip)) {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
}

function Invoke-StarUiCheck {
    param([string]$Executable, [string]$Report)
    $runReport = $Report + '.' + [Guid]::NewGuid().ToString('N') + '.txt'
    $data = $runReport + '.data'
    $process = Start-Process -FilePath $Executable -ArgumentList @('--ui-test', ('"' + $runReport + '"'),
        '--data-dir', ('"' + $data + '"')) -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(60000)) {
        $process.Kill(); $process.WaitForExit()
        throw 'Native UI checks timed out.'
    }
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $runReport)) { throw 'Native UI checks failed.' }
    $text = Get-Content -LiteralPath $runReport -Raw -Encoding utf8
    if ($text -notmatch 'UI RESULT PASS' -or $text -match '(?m)^FAIL ') { throw 'Native UI report did not pass.' }
    Move-Item -LiteralPath $runReport -Destination $Report -Force
    if (Test-Path -LiteralPath ($runReport + '.bmp')) {
        Move-Item -LiteralPath ($runReport + '.bmp') -Destination ($Report + '.bmp') -Force
    }
    Write-Host $text
}
