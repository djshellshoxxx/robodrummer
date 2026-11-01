# Hosted-runner smoke: same Inno recipe/payloads, isolated destinations and AppId.
# This does not establish acceptance on a physical clean Windows machine.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = [System.IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path $work) { throw 'Installer smoke directory must be new and isolated' }
New-Item -ItemType Directory -Path $work | Out-Null
$extract = Join-Path $work 'extracted'
$installRoot = Join-Path $work 'installed'
$out = Join-Path $work 'compiler'
New-Item -ItemType Directory -Path $out | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $extract
$stage = Join-Path $extract "RoboDrummer-$Version-windows-x64"
$report = Get-Content -Raw (Join-Path $stage 'SIGNING.json') | ConvertFrom-Json
$python = (Get-Command python).Source
$iscc = $env:ISCC
if (-not $iscc) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw 'ISCC.exe is required for installer smoke' }
$compilerArgs = @(
    "/DAppVersion=$Version", "/DAppNumericVersion=$($Version.Split('-')[0])",
    "/DStageDir=$stage", "/DOutDir=$out", '/DOutName=installer-smoke', "/DTestInstallRoot=$installRoot"
)
$mode = & $python (Join-Path $root 'scripts/ci/signing.py') policy windows-x64 $Version
if ($LASTEXITCODE -ne 0) { throw 'Installer signing configuration is invalid' }
if ($mode.Trim() -eq 'signed') {
    $signingScript = Join-Path $root 'scripts/ci/signing.py'
    $signCommand = '$q' + $python + '$q $q' + $signingScript + '$q windows-file $q' + $Version + '$q $f'
    $compilerArgs += @('/DEnableSigning=1', "/Srd_signing=$signCommand")
}
$compilerArgs += (Join-Path $root 'installer/RoboDrummer.iss')
# Inno may echo its SignTool command, which contains paths/version but no secrets.
& $iscc @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Isolated installer compilation failed' }
$installer = Join-Path $out 'installer-smoke.exe'
$installLog = Join-Path $work 'install.log'
$process = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$installLog`"") -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Installer failed (exit $($process.ExitCode))" }
$payloads = @(
    'app\RoboDrummer.exe', 'vst3\RoboDrummer.vst3\Contents\x86_64-win\RoboDrummer.vst3', 'clap\RoboDrummer.clap',
    'app\LICENSE', 'app\COPYRIGHT-TRADEMARK.md', 'app\INSTALL.md', 'app\PARAMETERS.md', 'app\HELP.md',
    'app\SIGNING.json', 'app\third-party-notices\JUCE-LICENSE.md', 'app\third-party-notices\CLAP-LICENSE', 'app\unins000.exe'
)
foreach ($relative in $payloads) {
    $file = Join-Path $installRoot $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) { throw "Missing installed payload: $relative" }
}
if ($report.mode -eq 'signed') {
    foreach ($relative in @('app\RoboDrummer.exe', 'vst3\RoboDrummer.vst3\Contents\x86_64-win\RoboDrummer.vst3', 'clap\RoboDrummer.clap', 'app\unins000.exe')) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $installRoot $relative)
        if ($signature.Status -ne 'Valid') { throw "Installed signature verification failed: $relative" }
    }
}
$uninstallLog = Join-Path $work 'uninstall.log'
$uninstaller = Join-Path $installRoot 'app\unins000.exe'
$process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$uninstallLog`"") -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Uninstaller failed (exit $($process.ExitCode))" }
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $remaining = @($payloads | Where-Object { Test-Path -LiteralPath (Join-Path $installRoot $_) })
    if ($remaining.Count -eq 0) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if ($remaining.Count -ne 0) { throw "Uninstall left managed payloads: $($remaining -join ', ')" }
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{4BDA1A16-9D95-4D84-9A21-5C465E9799D5}_is1'
if (Test-Path $uninstallKey) { throw 'Uninstall left the smoke application registration' }
"Hosted Windows installer/uninstaller smoke passed for $Version" | Tee-Object -FilePath (Join-Path $work 'result.txt')
