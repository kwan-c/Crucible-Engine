param(
    [string]$ToolsDirectory = (Join-Path $PSScriptRoot '../build/setup-tools')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$version = '6.12.0.206'
$checksum = '4125F57D97CFA88257915EDC969E913DE198CD8E22396A29849037479A0AC368'
$toolsPath = [IO.Path]::GetFullPath($ToolsDirectory)
$imagePath = Join-Path $toolsPath "mono-sdk-$version"
$sdkPath = Join-Path $imagePath 'Mono'
$installerPath = Join-Path $toolsPath "mono-$version-x64-0.msi"
New-Item -ItemType Directory -Path $toolsPath -Force | Out-Null

if (!(Test-Path -LiteralPath (Join-Path $sdkPath 'lib/mono/4.5/mscorlib.dll'))) {
    if (!(Test-Path -LiteralPath $installerPath)) {
        $url = "https://download.mono-project.com/archive/6.12.0/windows-installer/mono-$version-x64-0.msi"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $installerPath
    }
    if ((Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash -ne $checksum) {
        throw 'Mono installer checksum mismatch. Remove the cached installer and retry.'
    }
    $signature = Get-AuthenticodeSignature -FilePath $installerPath
    if ($signature.Status -ne 'Valid') { throw 'Mono installer signature is not valid.' }
    # Administrative extraction produces a local SDK image, not a system installation.
    $logPath = Join-Path $toolsPath 'mono-extract.log'
    $arguments = @('/a', ('"' + $installerPath + '"'), '/qn',
        ('TARGETDIR="' + $imagePath + '"'), '/L*v', ('"' + $logPath + '"'))
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $processHandle = $process.Handle
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Mono extraction failed ($($process.ExitCode)); see $logPath" }
}
foreach ($required in @('bin/mono-2.0-sgen.dll', 'lib/mono-2.0-sgen.lib',
                         'include/mono-2.0/mono/jit/jit.h', 'etc/mono/config')) {
    if (!(Test-Path -LiteralPath (Join-Path $sdkPath $required))) { throw "Incomplete SDK: $required" }
}
# The Windows image omits these upstream notices; keep the matching tag's copies
# beside the local SDK so runtime deployment carries them too.
foreach ($notice in @('LICENSE', 'PATENTS.TXT')) {
    $noticePath = Join-Path $sdkPath $notice
    if (!(Test-Path -LiteralPath $noticePath)) {
        Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/mono/mono/mono-$version/$notice" -OutFile $noticePath
    }
}
Write-Output "Local Mono $version SDK: $sdkPath"
Write-Output ('Configure with -DCRUCIBLE_ENABLE_MONO=ON "-DCRUCIBLE_MONO_ROOT=' + $sdkPath + '"')
