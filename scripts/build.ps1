param(
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [switch]$FastJsonNumbers,
    [switch]$Test,
    [switch]$NoInstall,
    [switch]$NoPack,
    [string]$UpxPath
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'pack.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe not found.' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'Install VS2022 C++ Build Tools and a Windows SDK.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$env:STAR_BUILD_VCVARS = $vcvars
Push-Location $PSScriptRoot
try { $environmentLines = & $env:ComSpec /d /c environment.cmd $Architecture }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'vcvarsall failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$configurationName = if ($FastJsonNumbers) { "$Architecture-fast-json" } else { $Architecture }
$outDir = Join-Path $projectRoot "build\$configurationName"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
Push-Location $projectRoot
try {
    & rc.exe /nologo /fo "$outDir\app.res" app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    $common = @('/nologo','/std:c17','/utf-8','/O1','/GL','/Gy','/Gw','/MT','/W4','/D_CRT_SECURE_NO_WARNINGS','/DYYJSON_DISABLE_FILE=1','/DYYJSON_DISABLE_UTILS=1','/DYYJSON_DISABLE_NON_STANDARD=1','/DYYJSON_DISABLE_INCR_READER=1','/DYYJSON_READER_DEPTH_LIMIT=64','/Isrc','/Ivendor\yyjson')
    if (!$FastJsonNumbers) { $common += '/DYYJSON_DISABLE_FAST_FP_CONV=1' }
    $coreSources = @('src\core.c','src\storage.c','src\network.c','src\json.c','src\avatar.c')
    $libraries = @('user32.lib','gdi32.lib','shell32.lib','ole32.lib','comdlg32.lib','comctl32.lib','dwmapi.lib','d2d1.lib','dwrite.lib','winhttp.lib','crypt32.lib','advapi32.lib','uxtheme.lib','imm32.lib','windowscodecs.lib')
    & cl.exe @common @coreSources src\app.c "/Fo$outDir\\" "/Fe$outDir\StarLibrary.unpacked.exe" "$outDir\app.res" /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT "/MAP:$outDir\StarLibrary.map" @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    $imports = & dumpbin.exe /nologo /dependents "$outDir\StarLibrary.unpacked.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Dependency inspection failed.' }
    $imports | Set-Content -LiteralPath "$outDir\dependencies-unpacked.txt" -Encoding utf8
    if ($imports -match 'VCRUNTIME\d+\.dll|MSVCP\d+\.dll|libgcc|libstdc\+\+|libwinpthread') { throw 'Unexpected distributable runtime dependency.' }
    if ($Test) {
        # Full UI self-checks share the production UI; only the test harness is omitted from distribution.
        $coreObjects = $coreSources | ForEach-Object { Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj') }
        & cl.exe @common /DSTAR_UI_CHECKS=1 src\app.c @coreObjects "/Fo$outDir\\" "/Fe$outDir\StarLibrary-ui-test.unpacked.exe" "$outDir\app.res" /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT @libraries
        if ($LASTEXITCODE -ne 0) { throw 'UI check compilation failed.' }
        & cl.exe @common @coreSources tests\test.c "/Fo$outDir\\" "/Fe$outDir\tests.unpacked.exe" /link /SUBSYSTEM:CONSOLE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO @libraries
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & "$outDir\tests.unpacked.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
    $names = @('StarLibrary')
    if ($Test) { $names += @('StarLibrary-ui-test', 'tests') }
    if (!$NoPack) { $upx = Get-StarUpx -ProjectRoot $projectRoot -Path $UpxPath }
    $packing = foreach ($name in $names) {
        if ($NoPack) { Copy-Item -LiteralPath "$outDir\$name.unpacked.exe" -Destination "$outDir\$name.exe" -Force }
        else { Compress-StarExecutable -Source "$outDir\$name.unpacked.exe" -Destination "$outDir\$name.exe" -Upx $upx }
    }
    if (!$NoPack) { $packing | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$outDir\packing.json" -Encoding utf8 }
    $exe = Get-Item -LiteralPath "$outDir\StarLibrary.exe"
    if ($exe.Length -gt 400000) { throw "Size gate failed: $($exe.Length) bytes > 400,000." }
    $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
    if ((Get-StarPeInfo -Path $exe.FullName).Machine -ne $expectedMachine) { throw 'Unexpected executable architecture.' }
    $imports = & dumpbin.exe /nologo /dependents $exe.FullName
    if ($LASTEXITCODE -ne 0) { throw 'Packed dependency inspection failed.' }
    $imports | Set-Content -LiteralPath "$outDir\dependencies.txt" -Encoding utf8
    if ($imports -match 'VCRUNTIME\d+\.dll|MSVCP\d+\.dll|libgcc|libstdc\+\+|libwinpthread') { throw 'Unexpected packed runtime dependency.' }
    if ($Test) {
        & "$outDir\tests.exe" | Tee-Object -FilePath "$outDir\core-results.txt"
        if ($LASTEXITCODE -ne 0) { throw 'Packed core tests failed.' }
        Invoke-StarUiCheck -Executable "$outDir\StarLibrary-ui-test.exe" -Report "$outDir\ui-results.txt"
        Invoke-StarUiCheck -Executable $exe.FullName -Report "$outDir\ui-release-results.txt"
    }
    if (!$FastJsonNumbers -and !$NoInstall) {
        Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $projectRoot 'StarLibrary.exe') -Force
    }
    [PSCustomObject]@{ Architecture=$Architecture; Bytes=$exe.Length; Limit=400000; Packed=(!$NoPack); Path=$exe.FullName } | Format-List
}
finally { Pop-Location }
