<#
.SYNOPSIS
    Registers .xbe files and folders to launch in xemu for the current user.

.DESCRIPTION
    Double-clicking an .xbe boots it in xemu: the .xbe's folder is served as a
    virtual disc with the chosen .xbe as default.xbe (xemu -xbe-path).

    The right-click menu of an .xbe gains:
      Launch XBE             xemu -xbe-path <file>
      Launch XBE (Debug)     xemu -xbe-path <file> -device lpc47m157 -serial stdio

    The right-click menu of a folder, and of the empty space inside an open
    folder, gains:
      Launch Folder          xemu -folder-path <folder>
      Launch Folder (Debug)  xemu -folder-path <folder> -device lpc47m157 -serial stdio

    A launched folder is served as the disc as-is, so it needs a default.xbe.
    Debug launches run in a console window that stays open for the output.

    xemu is started from its own folder so xemu.log and the relative paths in
    xemu.toml resolve there. Everything is written under HKCU; no admin rights
    are needed. On Windows 11 the extra verbs are under "Show more options"
    unless -ClassicMenu is given.

    With no parameters a menu asks what to do; register-xbe.cmd opens it on
    double-click. Run it yourself from Explorer or a normal PowerShell window:
    a terminal inside a sandboxed or packaged app can have its registry writes
    redirected to a private copy that Explorer never sees.

.PARAMETER XemuPath
    Path to xemu.exe. Defaults to xemu.exe next to this script.

.PARAMETER Unregister
    Remove the association and menu entries.

.PARAMETER ClassicMenu
    Also switch Windows 11 to the classic right-click menu for this user, so
    the entries show on the first right-click, and restart Explorer.

.PARAMETER ModernMenu
    Switch back to the Windows 11 right-click menu and restart Explorer.

.EXAMPLE
    register-xbe.cmd

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File register-xbe.ps1 -XemuPath C:\xemu\xemu.exe

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File register-xbe.ps1 -ClassicMenu

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File register-xbe.ps1 -Unregister
#>
[CmdletBinding()]
param(
    [string]$XemuPath,
    [switch]$Unregister,
    [switch]$ClassicMenu,
    [switch]$ModernMenu
)

$ErrorActionPreference = 'Stop'

if ($PSBoundParameters.Count -eq 0) {
    Write-Host 'xemu launcher setup'
    Write-Host ''
    Write-Host '  1) Register .xbe and folder launchers'
    Write-Host '  2) Register, and use the classic right-click menu (Windows 11)'
    Write-Host '  3) Switch back to the Windows 11 right-click menu'
    Write-Host '  4) Remove the launchers'
    Write-Host '  Q) Quit'
    Write-Host ''
    switch ((Read-Host 'Choose').Trim()) {
        '1' { }
        '2' { $ClassicMenu = $true }
        '3' { $ModernMenu = $true }
        '4' { $Unregister = $true }
        default { return }
    }
}

# Windows PowerShell leaves $PSScriptRoot empty in param() defaults
if (-not $XemuPath) {
    $XemuPath = Join-Path $PSScriptRoot 'xemu.exe'
}

$ProgId = 'xemu.xbe'
$XbeVerbs = 'SystemFileAssociations\.xbe\shell'
$FolderVerbs = 'Directory\shell', 'Directory\Background\shell'
# An empty InprocServer32 for this CLSID turns off the Windows 11 menu
$ModernMenuClsid = 'CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}'
$Classes = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes')

function Set-Key([string]$Path, [hashtable]$Values) {
    $key = $Classes.CreateSubKey($Path)
    try {
        foreach ($name in $Values.Keys) {
            $key.SetValue($name, $Values[$name])
        }
    } finally {
        $key.Close()
    }
}

function Remove-Key([string]$Path) {
    $Classes.DeleteSubKeyTree($Path, $false)
}

function Set-Verb([string]$Path, [string]$Label, [string]$Command) {
    Set-Key $Path @{ 'MUIVerb' = $Label; 'Icon' = "`"$exe`",0" }
    Set-Key "$Path\command" @{ '' = $Command }
}

# xemu is started from its own folder. cmd treats & ^ etc. literally inside
# quotes, so the path Explorer passes in survives as-is.
function Get-LaunchCommand([string]$Flag, [string]$Target) {
    "cmd.exe /c start `"`" /d `"$dir`" `"$exe`" $Flag `"$Target`""
}

function Get-DebugCommand([string]$Flag, [string]$Target) {
    "cmd.exe /k pushd `"$dir`" && `"$exe`" $Flag `"$Target`" -device lpc47m157 -serial stdio"
}

function Update-Shell {
    Add-Type -Namespace Xemu -Name Shell -MemberDefinition @'
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
'@
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [Xemu.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Restart-Explorer {
    Stop-Process -Name explorer -Force
    Start-Sleep -Seconds 3
    # Windows normally restarts the shell by itself
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) {
        Start-Process explorer.exe
    }
}

try {
    if ($ModernMenu) {
        Remove-Key $ModernMenuClsid
        Restart-Explorer
        Write-Host 'Restored the Windows 11 right-click menu.'
        return
    }

    if ($Unregister) {
        foreach ($v in @($XbeVerbs) + $FolderVerbs) {
            Remove-Key "$v\xemu.launch"
            Remove-Key "$v\xemu.debug"
        }
        Remove-Key $ProgId

        $ext = $Classes.OpenSubKey('.xbe', $true)
        if ($ext) {
            try {
                if ($ext.GetValue('') -eq $ProgId) {
                    $ext.DeleteValue('')
                }
                $owp = $ext.OpenSubKey('OpenWithProgids', $true)
                if ($owp) {
                    $owp.DeleteValue($ProgId, $false)
                    $owp.Close()
                }
            } finally {
                $ext.Close()
            }
        }

        Update-Shell
        Write-Host 'Removed the xemu .xbe and folder launchers.'
        return
    }

    if (-not (Test-Path -LiteralPath $XemuPath -PathType Leaf)) {
        throw "xemu.exe not found at '$XemuPath'. Pass -XemuPath <path to xemu.exe>."
    }
    $exe = (Resolve-Path -LiteralPath $XemuPath).ProviderPath
    $dir = Split-Path -Parent $exe

    $launch = Get-LaunchCommand '-xbe-path' '%1'

    # Double-click an .xbe
    Set-Key $ProgId @{ '' = 'Xbox Executable' }
    Set-Key "$ProgId\DefaultIcon" @{ '' = "`"$exe`",0" }
    Set-Key "$ProgId\shell" @{ '' = 'open' }
    Set-Key "$ProgId\shell\open\command" @{ '' = $launch }
    Set-Key '.xbe' @{ '' = $ProgId }
    Set-Key '.xbe\OpenWithProgids' @{ $ProgId = '' }

    # Right-click an .xbe, shown even when another app owns .xbe
    Set-Verb "$XbeVerbs\xemu.launch" 'Launch XBE' $launch
    Set-Verb "$XbeVerbs\xemu.debug" 'Launch XBE (Debug)' (Get-DebugCommand '-xbe-path' '%1')

    # Right-click a folder, or the empty space inside an open folder
    foreach ($v in $FolderVerbs) {
        Set-Verb "$v\xemu.launch" 'Launch Folder' (Get-LaunchCommand '-folder-path' '%V')
        Set-Verb "$v\xemu.debug" 'Launch Folder (Debug)' (Get-DebugCommand '-folder-path' '%V')
    }

    if ($ClassicMenu) {
        Set-Key "$ModernMenuClsid\InprocServer32" @{ '' = '' }
    }

    Update-Shell
    Write-Host "Registered .xbe files and folders to launch with $exe"
    if ($ClassicMenu) {
        Restart-Explorer
        Write-Host 'Switched to the classic right-click menu.'
    } else {
        Write-Host 'If the menu entries do not show up, restart Explorer (or sign out and back in).'
    }
} finally {
    $Classes.Close()
}
