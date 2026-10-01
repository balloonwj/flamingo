/**
 *  KeepAwakeSession.cpp
 *
 *  「禁止机器休眠」Win32 API 封装层的实现。设计与日志约定见 KeepAwakeSession.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h，工程里标 PrecompiledHeader = NotUsing
 *  （与同目录的 AutoAwayPolicy.cpp 同约定）。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "KeepAwakeSession.h"

#include <tchar.h>          // _T（本 .cpp 不走 stdafx.h，须自行引入）

#include "../../Common/Log/IULog.h"

namespace balloonmain {

namespace {

// 传给 PowerCreateRequest 的理由说明。管理员执行 `powercfg /requests` 时显示的就是这句，
// 因此需写明是哪个程序、因何阻止休眠。REASON_CONTEXT 要求非 const 指针，故用数组而非
// 字符串常量指针。
WCHAR g_szPowerRequestReason[] = L"FlamingoIM：用户开启了「禁止机器休眠」";

// SetThreadExecutionState 回落路径使用的标志组合。
//   ES_CONTINUOUS       —— 该状态持续有效，直到下次显式修改（不加它则只生效一次）
//   ES_SYSTEM_REQUIRED  —— 阻止系统休眠
//   ES_DISPLAY_REQUIRED —— 阻止关闭显示器
const DWORD kExecStateKeepAwake = ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED;

// 组策略「交互式登录: 计算机不活动限制」（无操作多久后自动锁屏）所在的注册表位置。
// 企业环境中的自动锁屏多由它下发，排查时必须知道其当前取值。
const TCHAR* const kInactivityPolicyKey   = _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System");
const TCHAR* const kInactivityPolicyValue = _T("InactivityTimeoutSecs");

// 毫秒与秒的换算基数。日志中的空闲时长以秒为单位。
const DWORD kMsPerSecond = 1000;

// 读取组策略的计算机不活动限制（秒）。未配置时返回 -1，与"配置为 0（不限制）"相区分。
int QueryInactivityPolicySecs()
{
    HKEY hKey = NULL;
    if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, kInactivityPolicyKey, 0, KEY_QUERY_VALUE, &hKey)
        != ERROR_SUCCESS)
    {
        return -1;
    }

    DWORD dwType  = 0;
    DWORD dwValue = 0;
    DWORD cbData  = sizeof(dwValue);
    const LONG r = ::RegQueryValueEx(hKey, kInactivityPolicyValue, NULL, &dwType,
                                     (LPBYTE)&dwValue, &cbData);
    ::RegCloseKey(hKey);

    if (r != ERROR_SUCCESS || dwType != REG_DWORD)
    {
        return -1;
    }
    return (int)dwValue;
}

// 查询一项 SystemParametersInfo 的布尔或整数设置。查询失败时返回 -1。
int QuerySpi(UINT uiAction)
{
    int value = -1;
    if (!::SystemParametersInfo(uiAction, 0, &value, 0))
    {
        return -1;
    }
    return value;
}

} // anonymous namespace

CKeepAwakeSession::CKeepAwakeSession()
    : m_hPowerRequest(NULL)
    , m_execStateHeld(false)
{
}

CKeepAwakeSession::~CKeepAwakeSession()
{
    //析构时释放作为最后保障。进程正常退出时系统也会回收电源请求，此处显式释放一次，
    //是为了让"哪个程序在阻止休眠"这件事在日志上有始有终。
    Release();
}

bool CKeepAwakeSession::IsHolding() const
{
    return (m_hPowerRequest != NULL) || m_execStateHeld;
}

bool CKeepAwakeSession::Acquire()
{
    if (IsHolding())
    {
        return true;
    }

    //优先使用 PowerRequest：进程级，且能在 `powercfg /requests` 中连同理由一并显示。
    REASON_CONTEXT ctx;
    ::ZeroMemory(&ctx, sizeof(ctx));
    ctx.Version                    = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags                      = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString  = g_szPowerRequestReason;

    HANDLE hRequest = ::PowerCreateRequest(&ctx);
    if (hRequest != NULL && hRequest != INVALID_HANDLE_VALUE)
    {
        //两项分别对应"阻止休眠"与"阻止关屏"，缺一不可：只设置 System 一项时显示器仍会关闭。
        const BOOL okSystem  = ::PowerSetRequest(hRequest, PowerRequestSystemRequired);
        const BOOL okDisplay = ::PowerSetRequest(hRequest, PowerRequestDisplayRequired);
        if (okSystem || okDisplay)
        {
            m_hPowerRequest = hRequest;
            LOG_INFO("keep-awake: power request acquired (handle 0x%p, systemRequired=%d, displayRequired=%d).",
                     hRequest, (int)okSystem, (int)okDisplay);
            return true;
        }

        //句柄创建成功但两项都未设置上，等同于未生效，释放后改走回落路径。
        LOG_ERROR("keep-awake: both PowerSetRequest calls failed (error %u), falling back to SetThreadExecutionState.",
                  ::GetLastError());
        ::CloseHandle(hRequest);
    }
    else
    {
        LOG_ERROR("keep-awake: PowerCreateRequest failed (error %u), falling back to SetThreadExecutionState.",
                  ::GetLastError());
    }

    //回落路径。注意它绑定在调用线程上，因此本类只能在主线程使用（见头文件说明）。
    if (::SetThreadExecutionState(kExecStateKeepAwake) != 0)
    {
        m_execStateHeld = true;
        LOG_INFO("keep-awake: holding via SetThreadExecutionState (system and display required).");
        return true;
    }

    LOG_ERROR("keep-awake: both paths failed, sleep is not prevented this time.");
    return false;
}

void CKeepAwakeSession::Release()
{
    if (m_hPowerRequest != NULL)
    {
        ::PowerClearRequest(m_hPowerRequest, PowerRequestSystemRequired);
        ::PowerClearRequest(m_hPowerRequest, PowerRequestDisplayRequired);
        ::CloseHandle(m_hPowerRequest);
        m_hPowerRequest = NULL;
        LOG_INFO("keep-awake: power request released.");
    }

    if (m_execStateHeld)
    {
        //只保留 ES_CONTINUOUS 即撤销此前设置的两项要求，恢复正常的休眠与关屏行为。
        ::SetThreadExecutionState(ES_CONTINUOUS);
        m_execStateHeld = false;
        LOG_INFO("keep-awake: SetThreadExecutionState cleared.");
    }
}

DWORD CKeepAwakeSession::QuerySystemIdleMs()
{
    LASTINPUTINFO lii;
    ::ZeroMemory(&lii, sizeof(lii));
    lii.cbSize = sizeof(lii);
    if (!::GetLastInputInfo(&lii))
    {
        return 0;
    }

    //两个 32 位 tick 相减在无符号运算下是模 2^32 的正确差值，回绕（开机约 49.7 天）时
    //同样正确；若最后一次输入的 tick 晚于当前 tick，差值会接近 2^32，此处按"刚刚发生过
    //输入"返回 0。
    const DWORD kMaxSaneIdleMs = 0x80000000;   //约 24.8 天，超过该值一律视为刚发生过输入
    const DWORD nowTick = ::GetTickCount();
    const DWORD diff    = nowTick - lii.dwTime;
    return (diff >= kMaxSaneIdleMs) ? 0 : diff;
}

bool CKeepAwakeSession::Nudge()
{
    const DWORD idleBeforeMs = QuerySystemIdleMs();

    //位移量为 0 的鼠标移动事件：光标不会移动、不会投递到任何窗口，但足以将输入空闲
    //计时器重置为 0。
    INPUT input;
    ::ZeroMemory(&input, sizeof(input));
    input.type       = INPUT_MOUSE;
    input.mi.dx      = 0;
    input.mi.dy      = 0;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;

    const UINT sent = ::SendInput(1, &input, sizeof(INPUT));

    //每次都记录：事后需要依据这条日志判断合成是否在执行、频率是否足够。合成前的空闲
    //时长尤为关键 —— 它应当稳定在一个合成间隔左右；若持续增长，说明合成没有生效。
    LOG_INFO("keep-awake: synthesized a mouse move, system idle %u sec before it, SendInput returned %u.",
             (unsigned int)(idleBeforeMs / kMsPerSecond), (unsigned int)sent);

    if (sent == 0)
    {
        LOG_ERROR("keep-awake: SendInput failed (error %u), screen lock is not deferred this time.",
                  ::GetLastError());
        return false;
    }
    return true;
}

void CKeepAwakeSession::LogIdlePolicySnapshot()
{
    //SPI_GETSCREENSAVEACTIVE：是否启用屏幕保护程序；SPI_GETSCREENSAVETIMEOUT：多少秒后启动；
    //SPI_GETSCREENSAVESECURE：屏保恢复时是否要求密码（要求密码才等同于锁屏）。
    const int saverActive  = QuerySpi(SPI_GETSCREENSAVEACTIVE);
    const int saverTimeout = QuerySpi(SPI_GETSCREENSAVETIMEOUT);
    const int saverSecure  = QuerySpi(SPI_GETSCREENSAVESECURE);
    const int policySecs   = QueryInactivityPolicySecs();

    LOG_INFO("keep-awake: idle policy snapshot - screensaverActive=%d, screensaverTimeout=%d sec, "
             "screensaverSecure=%d, inactivityPolicy=%d sec (-1 means not configured).",
             saverActive, saverTimeout, saverSecure, policySecs);
}

} // namespace balloonmain
