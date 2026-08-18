/**
 *  升级窗纯逻辑部分的实现，UpdateViewModel.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "UpdateViewModel.h"

namespace update {

namespace {

//进度百分比的上界。钳位用，避免 received 大于 total 这种异常输入算出超过 100 的值。
const int kPercentMax = 100;

/**
 *  把只含 ASCII 的窄串加宽成宽串。
 *
 *  这里刻意不用 MultiByteToWideChar / EncodeUtil：本文件是纯逻辑层，不引入 windows.h
 *  与业务模块，才能被测试工程零依赖地调用。而需要加宽的只有<u>版本号</u>——协议约定
 *  它是点分十进制（见 docs/协议设计-v2.md 10.12），本就只有数字和点。
 *  万一服务端记录里混进了非 ASCII 字节，逐字节加宽会显示成乱码，但不会越界或崩溃；
 *  这属于发布侧填错内容，不应由界面层负责修正。
 */
std::wstring WidenAscii(const std::string& s)
{
    std::wstring w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(s[i])));
    }
    return w;
}

/**
 *  拼版本行：「当前版本 3.2.1 → 最新版本 3.3.0」。
 *  currentVersion 为空串时退化成「最新版本 3.3.0」，不保留一个没有版本号的"当前版本 "。
 */
std::wstring MakeVersionLine(const std::wstring& currentVersion,
                             const std::wstring& latestVersion)
{
    if (currentVersion.empty())
    {
        return std::wstring(L"最新版本 ") + latestVersion;
    }
    return std::wstring(L"当前版本 ") + currentVersion
         + L" → 最新版本 " + latestVersion;
}

} // namespace

UpdateViewState BuildUpdateViewState(const UpdateTaskResult& r,
                                     const std::wstring& currentVersion)
{
    UpdateViewState st;
    const std::wstring latest = WidenAscii(r.m_latestVersion);

    //① 本次请求未完成（超时 / 未连接），或服务端返回了业务错误码：只能呈现失败。
    //   注意与"有更新但升级包不可用"区分：后者服务端是正常应答的。
    if (!r.m_transportOk || r.m_code != 0)
    {
        st.kind            = update_view_failed;
        st.showLaterButton = true;
        st.closable        = true;
        st.canDownload     = false;
        st.title           = L"检查更新失败，请稍后重试";
        st.versionLine.clear();
        return st;
    }

    //② 服务端报告有更新，但未下发完整的令牌 / MD5 / 大小：按无更新处理，不提供下载入口。
    //   放行到下面的"已是最新"分支即可——用户看到的是"当前已是最新版本"，
    //   而不是一个点下去必然失败的「立即更新」按钮。
    const bool reallyHasUpdate = r.m_hasUpdate && r.m_packageUsable;

    if (!reallyHasUpdate)
    {
        st.kind            = update_view_latest;
        st.showLaterButton = true;
        st.closable        = true;
        st.canDownload     = false;
        st.title           = latest.empty() ? std::wstring(L"当前已是最新版本")
                                            : (std::wstring(L"当前已是最新版本 ") + latest);
        st.versionLine.clear();
        return st;
    }

    //③ 有可用新版本。强制与否只差「稍后」按钮和能不能关窗。
    st.canDownload  = true;
    st.versionLine  = MakeVersionLine(currentVersion, latest);

    if (r.m_forced)
    {
        st.kind            = update_view_forced;
        st.showLaterButton = false;
        st.closable        = false;
        st.title           = std::wstring(L"必须升级到 ") + latest + L" 才能继续使用";
    }
    else
    {
        st.kind            = update_view_has_update;
        st.showLaterButton = true;
        st.closable        = true;
        st.title           = std::wstring(L"发现新版本 ") + latest;
    }

    return st;
}

int ComputeDownloadPercent(long long received, long long total)
{
    //服务端尚未告知总大小（或告知为 0）：无法计算百分比，返回 0，不能做除法
    if (total <= 0)
    {
        return 0;
    }

    //已接收字节数为负是不应出现的输入，但即便出现也不能让进度条回退
    if (received <= 0)
    {
        return 0;
    }

    //乘法在 64 位上做：3GiB 级别的包 received * 100 用 32 位会溢出成负数
    const long long percent = received * kPercentMax / total;

    if (percent >= kPercentMax)
    {
        return kPercentMax;
    }

    return static_cast<int>(percent);
}

bool ShouldBlockLogin(const UpdateTaskResult& r)
{
    //本次请求未完成、或服务端返回了错误码：放行。
    //检查更新链路自身故障时不应阻断登录，否则客户端自身的故障将直接影响用户使用。
    if (!r.m_transportOk || r.m_code != 0)
    {
        return false;
    }

    //升级包信息不完整时同样放行：此时并无可下载的升级包，阻断登录会使用户无法继续操作
    return r.m_hasUpdate && r.m_packageUsable && r.m_forced;
}

} // namespace update
