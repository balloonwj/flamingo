# keepawake_probe.ps1
#
# 「禁止机器休眠」功能所依赖的三项 Windows 行为的实测探针。
# 用途、判读方法与注意事项见同目录的 README.md。
#
# 用法（普通权限即可，powercfg 那一步需要管理员）：
#   powershell -ExecutionPolicy Bypass -File tools\keepawake_probe.ps1
#   powershell -ExecutionPolicy Bypass -File tools\keepawake_probe.ps1 -LiveScreenSaver
#
#   -LiveScreenSaver 会临时修改屏幕保护程序设置并在结束时还原，详见 README.md。
#
# 说明：脚本中的 C# 代码块一律使用英文注释 —— PowerShell 5.1 按 ANSI 读取 .ps1，
# UTF-8 中文注释会被解码成乱码，其中某些字节会使 C# 的行注释吞掉下一行真实代码。
#
# balloonwj@qq.com   2026-08-12

param(
    [switch]$LiveScreenSaver,
    [int]$NudgeIntervalSec = 20,
    [int]$ObserveSec = 100
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

public class KeepAwakeProbe {
  // ---- input idle timer (the screen saver and the auto-lock policy both watch it) ----
  [StructLayout(LayoutKind.Sequential)]
  public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
  [DllImport("kernel32.dll")] public static extern uint GetTickCount();

  public static long IdleMs() {
    LASTINPUTINFO lii = new LASTINPUTINFO();
    lii.cbSize = (uint)Marshal.SizeOf(typeof(LASTINPUTINFO));
    if (!GetLastInputInfo(ref lii)) { return -1; }
    unchecked { return (long)(uint)(GetTickCount() - lii.dwTime); }
  }

  // ---- synthesized input ----
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]
  public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public INPUTUNION u; }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] pInputs, int cbSize);

  // A zero-delta relative mouse move: the cursor does not move and no window receives
  // a click, but the input idle timer is reset. This is what the client sends.
  public static uint NudgeMouseZero() {
    INPUT[] inp = new INPUT[1];
    inp[0].type = 0;                 // INPUT_MOUSE
    inp[0].u.mi.dwFlags = 0x0001;    // MOUSEEVENTF_MOVE, dx = dy = 0
    return SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }

  // A VK_F15 keystroke. Kept only for comparison - the client does NOT use it,
  // because a synthesized keystroke is really delivered to the foreground window.
  public static uint NudgeF15() {
    INPUT[] inp = new INPUT[2];
    inp[0].type = 1;                 // INPUT_KEYBOARD
    inp[0].u.ki.wVk = 0x7E;          // VK_F15
    inp[1].type = 1;
    inp[1].u.ki.wVk = 0x7E;
    inp[1].u.ki.dwFlags = 0x0002;    // KEYEVENTF_KEYUP
    return SendInput(2, inp, Marshal.SizeOf(typeof(INPUT)));
  }

  // ---- power request (Windows 7 and later) ----
  [StructLayout(LayoutKind.Sequential)]
  public struct REASON_CONTEXT {
    public uint Version; public uint Flags; public IntPtr SimpleReasonString;
    public IntPtr pad1; public IntPtr pad2;   // room for the Detailed union branch
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr PowerCreateRequest(ref REASON_CONTEXT ctx);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool PowerSetRequest(IntPtr h, int type);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool PowerClearRequest(IntPtr h, int type);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern uint SetThreadExecutionState(uint esFlags);

  public const int PowerRequestDisplayRequired = 0;
  public const int PowerRequestSystemRequired  = 1;

  public static IntPtr CreateRequest(string reason) {
    REASON_CONTEXT ctx = new REASON_CONTEXT();
    ctx.Version = 0;                                  // POWER_REQUEST_CONTEXT_VERSION
    ctx.Flags = 0x1;                                  // POWER_REQUEST_CONTEXT_SIMPLE_STRING
    ctx.SimpleReasonString = Marshal.StringToHGlobalUni(reason);
    return PowerCreateRequest(ref ctx);
  }

  // ---- screen saver settings ----
  [DllImport("user32.dll", SetLastError=true)] public static extern bool SystemParametersInfo(uint uiAction, uint uiParam, ref int pvParam, uint fWinIni);
  [DllImport("user32.dll", SetLastError=true, EntryPoint="SystemParametersInfoW")]
  public static extern bool SystemParametersInfoSet(uint uiAction, uint uiParam, IntPtr pvParam, uint fWinIni);
  public const uint SPI_GETSCREENSAVETIMEOUT  = 0x000E;
  public const uint SPI_SETSCREENSAVETIMEOUT  = 0x000F;
  public const uint SPI_GETSCREENSAVEACTIVE   = 0x0010;
  public const uint SPI_SETSCREENSAVEACTIVE   = 0x0011;
  public const uint SPI_GETSCREENSAVERRUNNING = 0x0072;
  public const uint SPI_GETSCREENSAVESECURE   = 0x0076;
  public const uint SPIF_SENDCHANGE = 0x0002;

  public static int Query(uint action) { int v = -1; if (!SystemParametersInfo(action, 0, ref v, 0)) { return -1; } return v; }
  public static bool SetSaverTimeout(int seconds) { return SystemParametersInfoSet(SPI_SETSCREENSAVETIMEOUT, (uint)seconds, IntPtr.Zero, SPIF_SENDCHANGE); }
  public static bool SetSaverActive(bool on) { return SystemParametersInfoSet(SPI_SETSCREENSAVEACTIVE, (uint)(on ? 1 : 0), IntPtr.Zero, SPIF_SENDCHANGE); }
}
'@

$deskKey = 'HKCU:\Control Panel\Desktop'

Write-Output "=============================================================="
Write-Output " 0. 本机当前的屏保 / 锁屏设置"
Write-Output "=============================================================="
$saverExe = (Get-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
if ($null -eq $saverExe -or $saverExe -eq '') { $saverExe = '(未配置屏保程序)' }
$policySecs = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' -Name 'InactivityTimeoutSecs' -ErrorAction SilentlyContinue).InactivityTimeoutSecs
if ($null -eq $policySecs) { $policySecs = '(未配置)' }
Write-Output ("  屏保程序                      = {0}" -f $saverExe)
Write-Output ("  屏保是否启用                  = {0}" -f [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVEACTIVE))
Write-Output ("  屏保超时                      = {0} 秒" -f [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVETIMEOUT))
Write-Output ("  屏保恢复是否要求密码          = {0}   (1 才等同于锁屏)" -f [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVESECURE))
Write-Output ("  组策略计算机不活动限制        = {0}" -f $policySecs)

Write-Output ""
Write-Output "=============================================================="
Write-Output " 1. 系统电源请求能否创建并设置"
Write-Output "=============================================================="
$h = [KeepAwakeProbe]::CreateRequest("FlamingoIM keepawake probe")
Write-Output ("  PowerCreateRequest 句柄 = {0}" -f $h)
if ($h -eq [IntPtr]::Zero -or $h -eq [IntPtr](-1)) {
    Write-Output ("  创建失败，错误码 = {0}" -f [Runtime.InteropServices.Marshal]::GetLastWin32Error())
} else {
    Write-Output ("  PowerSetRequest(阻止休眠)   = {0}" -f [KeepAwakeProbe]::PowerSetRequest($h, [KeepAwakeProbe]::PowerRequestSystemRequired))
    Write-Output ("  PowerSetRequest(阻止关屏)   = {0}" -f [KeepAwakeProbe]::PowerSetRequest($h, [KeepAwakeProbe]::PowerRequestDisplayRequired))
}
# 回落方案：ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
$prev = [KeepAwakeProbe]::SetThreadExecutionState([uint32]2147483651)
Write-Output ("  SetThreadExecutionState 回落方案返回 0x{0:X}  (0 表示失败)" -f $prev)
[void][KeepAwakeProbe]::SetThreadExecutionState([uint32]2147483648)

Write-Output ""
Write-Output "  powercfg /requests（需要管理员权限）："
# 先判断权限再决定跑不跑：非管理员时 powercfg 会往 stderr 写一行提示，而 PowerShell 5.1
# 会把原生命令的 stderr 包成 ErrorRecord，在 $ErrorActionPreference='Stop' 下直接中断脚本。
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    $pc = (& powercfg /requests | Out-String)
    Write-Output ($pc.Trim() -split "`r?`n" | ForEach-Object { "    $_" })
} else {
    Write-Output "    （当前不是管理员，已跳过。要看这一节请用管理员身份重跑本脚本。）"
}

Write-Output ""
Write-Output "=============================================================="
Write-Output " 2. 电源请求是否影响输入空闲计时器（预期：不影响，空闲持续增长）"
Write-Output "=============================================================="
Write-Output "    这 20 秒内请不要触碰键盘鼠标，否则数据无效。"
for ($i = 0; $i -lt 10; $i++) {
    Write-Output ("  t={0,2}s  空闲={1,7} 毫秒" -f ($i * 2), [KeepAwakeProbe]::IdleMs())
    Start-Sleep -Milliseconds 2000
}

Write-Output ""
Write-Output "=============================================================="
Write-Output " 3. 合成鼠标移动事件能否重置输入空闲计时器（预期：每次合成后归零）"
Write-Output "=============================================================="
for ($i = 0; $i -lt 6; $i++) {
    if ($i % 2 -eq 0) {
        $sent = [KeepAwakeProbe]::NudgeMouseZero()
        Write-Output ("  t={0,2}s  已合成一次（SendInput 返回 {1}），此刻空闲={2} 毫秒" -f ($i * 2), $sent, [KeepAwakeProbe]::IdleMs())
    } else {
        Write-Output ("  t={0,2}s  空闲={1,7} 毫秒" -f ($i * 2), [KeepAwakeProbe]::IdleMs())
    }
    Start-Sleep -Milliseconds 2000
}

if ($h -ne [IntPtr]::Zero) {
    [void][KeepAwakeProbe]::PowerClearRequest($h, [KeepAwakeProbe]::PowerRequestSystemRequired)
    [void][KeepAwakeProbe]::PowerClearRequest($h, [KeepAwakeProbe]::PowerRequestDisplayRequired)
    [void][KeepAwakeProbe]::CloseHandle($h)
    Write-Output ""
    Write-Output "  电源请求已释放。"
}

if (-not $LiveScreenSaver) {
    Write-Output ""
    Write-Output "（未指定 -LiveScreenSaver，跳过屏保实测。该项会临时改动屏保设置，详见 README.md。）"
    Write-Output "done"
    return
}

Write-Output ""
Write-Output "=============================================================="
Write-Output " 4. 持续合成输入事件时屏保是否还会启动（预期：不启动）"
Write-Output "=============================================================="
$origSaverExe = (Get-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
$origTimeout  = [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVETIMEOUT)
$origActive   = [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVEACTIVE)
Write-Output ("  原设置：屏保程序=[{0}]，超时={1} 秒，启用={2}（结束时会还原）" -f $origSaverExe, $origTimeout, $origActive)
Write-Output ("  接下来约 {0} 秒请不要触碰键盘鼠标。" -f $ObserveSec)

try {
    Set-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -Value 'C:\Windows\System32\scrnsave.scr'
    [void][KeepAwakeProbe]::SetSaverActive($true)
    [void][KeepAwakeProbe]::SetSaverTimeout(60)
    Write-Output ("  已临时设为：空白屏保，超时 {0} 秒" -f [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVETIMEOUT))

    $maxIdle = 0
    $everRunning = 0
    for ($t = 0; $t -le $ObserveSec; $t += 5) {
        if ($t % $NudgeIntervalSec -eq 0) { [void][KeepAwakeProbe]::NudgeMouseZero() }
        $idle = [KeepAwakeProbe]::IdleMs()
        $run  = [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVERRUNNING)
        if ($idle -gt $maxIdle) { $maxIdle = $idle }
        if ($run -ne 0) { $everRunning = 1 }
        Write-Output ("  t={0,3}s  空闲={1,7} 毫秒  屏保是否运行中={2}" -f $t, $idle, $run)
        Start-Sleep -Milliseconds 5000
    }

    Write-Output ""
    Write-Output ("  最大空闲 = {0} 毫秒（应当接近合成间隔 {1} 秒；若远小于它，说明期间有真人输入，本轮数据无效）" -f $maxIdle, $NudgeIntervalSec)
    Write-Output ("  屏保是否启动过 = {0}（0 = 符合预期，合成输入确实挡住了屏保）" -f $everRunning)
}
finally {
    [void][KeepAwakeProbe]::SetSaverTimeout($origTimeout)
    [void][KeepAwakeProbe]::SetSaverActive($origActive -ne 0)
    if ($null -eq $origSaverExe -or $origSaverExe -eq '') {
        Remove-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue
    } else {
        Set-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -Value $origSaverExe
    }
    $nowExe = (Get-ItemProperty -Path $deskKey -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
    Write-Output ""
    Write-Output ("  已还原：屏保程序=[{0}]，超时={1} 秒，启用={2}" -f `
        $nowExe, [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVETIMEOUT), `
        [KeepAwakeProbe]::Query([KeepAwakeProbe]::SPI_GETSCREENSAVEACTIVE))
}

Write-Output "done"
