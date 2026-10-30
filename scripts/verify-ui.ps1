param([string]$Executable, [string]$ArtifactRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable=Join-Path $projectRoot 'StarLibrary.exe' }
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$checkExecutable=Join-Path (Split-Path -Parent $Executable) 'StarLibrary-ui-test.exe'
if (!(Test-Path -LiteralPath $checkExecutable)) { $checkExecutable=$Executable }
if (!$ArtifactRoot) { $ArtifactRoot=Join-Path $projectRoot 'artifacts' }
$artifactRoot=[IO.Path]::GetFullPath($ArtifactRoot)
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$reportPath=Join-Path $artifactRoot 'ui-tests.txt'
$testApp=Start-Process -FilePath $checkExecutable -ArgumentList @('--ui-test',('"'+$reportPath+'"')) -WindowStyle Hidden -PassThru
if (!$testApp.WaitForExit(15000) -or $testApp.ExitCode -ne 0) { throw 'Native UI integration checks failed.' }
Get-Content -LiteralPath $reportPath
$views=@(
    @{Name='library-dark';Args=@('--preview')},
    @{Name='library-light';Args=@('--preview','--light')},
    @{Name='cards-hover-dark';Args=@('--preview','--expanded')},
    @{Name='cards-hover-light';Args=@('--preview','--expanded','--light')},
    @{Name='card-action-hover-light';Args=@('--preview','--hover-command','--light')},
    @{Name='card-action-hover-dark';Args=@('--preview','--hover-command')},
    @{Name='card-action-pressed-light';Args=@('--preview','--press-command','--light')},
    @{Name='profile-dark';Args=@('--preview','--page','profile')},
    @{Name='profile-light';Args=@('--preview','--page','profile','--light')},
    @{Name='profile-expanded-light';Args=@('--preview','--page','profile','--expanded','--light','--scroll','230')},
    @{Name='profile-expanded-dark';Args=@('--preview','--page','profile','--expanded','--scroll','230')},
    @{Name='profile-compact';Args=@('--preview','--page','profile','--width','980','--height','700')},
    @{Name='profile-125-light';Args=@('--preview','--page','profile','--light','--width','1024','--height','700','--dpi','120')},
    @{Name='profile-125-dark';Args=@('--preview','--page','profile','--width','1024','--height','700','--dpi','120')},
    @{Name='discover-dark';Args=@('--preview','--page','discover')},
    @{Name='discover-light';Args=@('--preview','--page','discover','--light')},
    @{Name='discover-hover-light';Args=@('--preview','--page','discover','--expanded','--light')},
    @{Name='discover-1280-dark';Args=@('--preview','--page','discover','--width','1280','--height','800')},
    @{Name='discover-1280-light';Args=@('--preview','--page','discover','--light','--width','1280','--height','800')},
    @{Name='discover-1920-dark';Args=@('--preview','--page','discover','--width','1920','--height','1080')},
    @{Name='discover-1920-light';Args=@('--preview','--page','discover','--light','--width','1920','--height','1080')},
    @{Name='discover-compact';Args=@('--preview','--page','discover','--expanded','--width','980','--height','700')},
    @{Name='discover-125-light';Args=@('--preview','--page','discover','--light','--width','1024','--height','700','--dpi','120')},
    @{Name='discover-125-dark';Args=@('--preview','--page','discover','--width','1024','--height','700','--dpi','120')},
    @{Name='settings-dark';Args=@('--preview','--page','settings')},
    @{Name='settings-light';Args=@('--preview','--page','settings','--light')},
    @{Name='settings-1920-light';Args=@('--preview','--page','settings','--light','--width','1920','--height','1080')},
    @{Name='settings-1920-dark';Args=@('--preview','--page','settings','--width','1920','--height','1080')},
    @{Name='settings-lower-dark';Args=@('--preview','--page','settings','--scroll','360')},
    @{Name='settings-compact';Args=@('--preview','--page','settings','--width','980','--height','700')},
    @{Name='settings-125';Args=@('--preview','--page','settings','--light','--width','1024','--height','700','--dpi','120')},
    @{Name='library-1280';Args=@('--preview','--width','1280','--height','800')},
    @{Name='library-1280-light';Args=@('--preview','--light','--width','1280','--height','800')},
    @{Name='library-compact';Args=@('--preview','--width','980','--height','700')},
    @{Name='library-1920';Args=@('--preview','--width','1920','--height','1080')},
    @{Name='library-1920-light';Args=@('--preview','--light','--width','1920','--height','1080')},
    @{Name='library-125';Args=@('--preview','--width','1024','--height','700','--dpi','120')},
    @{Name='library-125-light';Args=@('--preview','--light','--width','1024','--height','700','--dpi','120')}
)
foreach ($view in $views) {
    $bmp=Join-Path $artifactRoot ($view.Name+'.bmp')
    $png=Join-Path $artifactRoot ($view.Name+'.png')
    $viewArgs=$view.Args+@('--capture',('"'+$bmp+'"'))
    for ($attempt=0; $attempt -lt 2; $attempt++) {
        $captureApp=Start-Process -FilePath $Executable -ArgumentList $viewArgs -WindowStyle Hidden -PassThru
        if ($captureApp.WaitForExit(20000)) {
            if ($captureApp.ExitCode -ne 0) { throw "Capture failed: $($view.Name), exit $($captureApp.ExitCode)" }
            break
        }
        # Only terminate the isolated preview this script just started.
        if (!$captureApp.HasExited) { $captureApp.Kill(); $captureApp.WaitForExit() }
        if ($attempt -eq 1) { throw "Capture timed out twice: $($view.Name)" }
        Write-Warning "Capture timed out; retrying isolated preview: $($view.Name)"
    }
    $image=[System.Drawing.Image]::FromFile($bmp)
    try {
        $dimensions="$($image.Width)x$($image.Height)"
        $image.Save($png,[System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $image.Dispose() }
    Write-Output "$($view.Name): $dimensions -> $png"
}
