# Register IxView for the current user. Windows keeps the final default-app choice.
[CmdletBinding()]
param(
    [string]$ExecutablePath = '',
    [switch]$Unregister,
    [switch]$GenerateOnly,
    [string]$RegFilePath = (Join-Path ([IO.Path]::GetTempPath()) 'IxView-associations.reg'),
    [switch]$OpenSettings
)
$ErrorActionPreference = 'Stop'
if (-not $ExecutablePath) { $ExecutablePath = Join-Path $PSScriptRoot 'uxzView.exe' }
$displayName = [string][char]0x00fc + 'xzView'
$extensions = '.jpg','.jpeg','.jpe','.png','.gif','.bmp','.dib','.tif','.tiff','.webp','.svg','.svgz','.ico','.avif','.heic','.heif','.jxl','.dng','.cr2','.cr3','.nef','.arw','.orf','.raf','.rw2'
function RegString([string]$Value) { '"' + $Value.Replace('\','\\').Replace('"','\"') + '"' }
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('Windows Registry Editor Version 5.00')
$lines.Add('')
if ($Unregister) {
    foreach ($key in @('Software\Classes\IxView.Image','Software\Classes\Applications\uxzView.exe','Software\Classes\Applications\IxView.exe','Software\IxView\Capabilities')) {
        $lines.Add("[-HKEY_CURRENT_USER\$key]")
        $lines.Add('')
    }
    $lines.Add('[HKEY_CURRENT_USER\Software\RegisteredApplications]')
    $lines.Add('"IxView"=-')
    foreach ($extension in $extensions) {
        $lines.Add('')
        $lines.Add("[HKEY_CURRENT_USER\Software\Classes\$extension\OpenWithProgids]")
        $lines.Add('"IxView.Image"=-')
    }
} else {
    $executable = [IO.Path]::GetFullPath($ExecutablePath)
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or [IO.Path]::GetFileName($executable) -ine 'uxzView.exe') {
        throw 'ExecutablePath must point to an existing uxzView.exe.'
    }
    if ($executable.Contains('"') -or $executable.Contains("`r") -or $executable.Contains("`n")) { throw 'Invalid executable path.' }
    $command = '"' + $executable + '" "%1"'
    $icon = '"' + $executable + '",0'
    $lines.Add('[-HKEY_CURRENT_USER\Software\Classes\Applications\IxView.exe]')
    $lines.Add('')
    foreach ($key in @('Software\Classes\IxView.Image','Software\Classes\Applications\uxzView.exe')) {
        $lines.Add("[HKEY_CURRENT_USER\$key]")
        $lines.Add('"FriendlyAppName"=' + (RegString $displayName))
        if ($key.EndsWith('IxView.Image')) { $lines.Add('@=' + (RegString ($displayName + ' image'))) }
        $lines.Add('')
        $lines.Add("[HKEY_CURRENT_USER\$key\DefaultIcon]")
        $lines.Add('@=' + (RegString $icon))
        $lines.Add('')
        $lines.Add("[HKEY_CURRENT_USER\$key\shell\open\command]")
        $lines.Add('@=' + (RegString $command))
        $lines.Add('')
    }
    $lines.Add('[HKEY_CURRENT_USER\Software\Classes\Applications\uxzView.exe\SupportedTypes]')
    foreach ($extension in $extensions) { $lines.Add((RegString $extension) + '=""') }
    $lines.Add('')
    $lines.Add('[HKEY_CURRENT_USER\Software\IxView\Capabilities]')
    $lines.Add('"ApplicationName"=' + (RegString $displayName))
    $lines.Add('"ApplicationDescription"="Image viewer"')
    $lines.Add('"ApplicationIcon"=' + (RegString $icon))
    $lines.Add('')
    $lines.Add('[HKEY_CURRENT_USER\Software\IxView\Capabilities\FileAssociations]')
    foreach ($extension in $extensions) { $lines.Add((RegString $extension) + '="IxView.Image"') }
    $lines.Add('')
    $lines.Add('[HKEY_CURRENT_USER\Software\RegisteredApplications]')
    $lines.Add('"IxView"="Software\\IxView\\Capabilities"')
    foreach ($extension in $extensions) {
        $lines.Add('')
        $lines.Add("[HKEY_CURRENT_USER\Software\Classes\$extension\OpenWithProgids]")
        $lines.Add('"IxView.Image"=hex(0):')
    }
}
$lines.Add('')
[IO.File]::WriteAllText([IO.Path]::GetFullPath($RegFilePath), ($lines -join "`r`n"), [Text.Encoding]::Unicode)
Write-Output "Registry file: $RegFilePath"
if ($GenerateOnly) { return }
& "$env:SystemRoot\System32\reg.exe" import $RegFilePath
if ($LASTEXITCODE -ne 0) { throw 'Registry import failed.' }
if (-not ('IxViewShellNotification' -as [type])) {
    Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class IxViewShellNotification { [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b); }'
}
[IxViewShellNotification]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
if ($OpenSettings -and -not $Unregister) { Start-Process 'ms-settings:defaultapps' }
