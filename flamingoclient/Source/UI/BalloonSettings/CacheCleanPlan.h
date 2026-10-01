/**
 *  「清理本地缓存」的清理计划，CacheCleanPlan.h
 *
 *  算出"点了清理之后到底要删哪几个目录"，以及"某个目录允许不允许删"。纯逻辑：
 *  只拼路径、只做边界判断，不扫描磁盘、不删任何东西，因此可以完全用单元测试覆盖
 *  （见 flamingoclient/Source/Tests/CacheCleanPlanTests.cpp，Suite CCP）。
 *
 *  <u>可清与不可清的分界</u>：缓存是可再生的副本，删了最多重新下载一次；聊天记录、
 *  公告、文件落点登记表与账号配置是本地唯一副本，删了就没了。故本模块产出的计划
 *  <u>只</u>包含前者，且提供一支独立的边界判断函数供真正执行删除前再核一遍。
 *
 *      <数据根>Users\<账号>\
 *        ├─ <账号>.ini        不清
 *        ├─ ChatLog\          不清（msg.db 聊天记录、announce.db 公告、filelocal.db 登记表）
 *        ├─ ChatFile\         可清：聊天文件的下载缓存
 *        ├─ ChatImage\        可清：聊天图片缓存
 *        └─ Avatar\ UserThumb\ 可清：头像缓存
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <string>
#include <vector>

namespace balloonsettings {

/**
 *  可清理的缓存类别。对话框里一个类别一行，各带一个开关。
 */
enum CacheCategory
{
    kCacheChatFile  = 0,   // 聊天文件的下载缓存（按内容 MD5 分目录）
    kCacheChatImage = 1,   // 聊天图片缓存
    kCacheAvatar    = 2,   // 头像缓存（Avatar 与 UserThumb 两个目录合成一项，用户不必区分）

    kCacheCategoryCount = 3   // 类别总数（哨兵，便于循环与断言）
};

/**
 *  清理计划里的一项，对应对话框里的一行。
 */
struct CacheCleanEntry
{
    int m_category;                        // 类别，取值见 CacheCategory
    const wchar_t* m_title;                // 显示名（宽字符常量串，进程级生命周期）
    std::vector<std::wstring> m_dirs;      // 该类别包含的目录，绝对路径、结尾带反斜杠

    CacheCleanEntry()
        : m_category(kCacheChatFile)
        , m_title(NULL)
    {
    }
};

/**
 *  按数据根目录与登录账号构造清理计划。
 *
 *    dataRoot：本地数据根目录（g_szDataRoot），须以反斜杠结尾；为空时返回空计划。
 *    account： 登录账号；内部经 net::SanitizeAccountForPath 净化。为空、或净化后为空
 *              时返回空计划 —— 那表示还没登录，没有可清的东西。
 *
 *  @return 三个类别的计划；任一前置条件不满足时返回空 vector。
 *          产出的目录<u>一定</u>落在 <dataRoot>Users\<净化后账号>\ 之下，且不含
 *          ChatLog 与 .ini。
 */
std::vector<CacheCleanEntry> BuildCacheCleanPlan(const std::wstring& dataRoot,
                                                 const std::wstring& account);

/**
 *  删除前的最后一道边界检查：这个目录允许删吗。
 *
 *  与 BuildCacheCleanPlan 分开是有意的 —— 计划是本模块自己产出的、本该安全，但真正
 *  执行删除的代码路径上可能混进别处传来的路径（将来加"清理其它账号"一类功能时尤其
 *  如此）。删除是不可恢复的动作，多核一遍不亏。判据与 BalloonAuth/AccountRemoval
 *  同源。
 *
 *    dir：     待删目录（绝对路径）。
 *    dataRoot：本地数据根目录。
 *
 *  @return 满足下列<u>全部</u>条件才返回 true：
 *          · dataRoot 与 dir 都非空；
 *          · dir 落在 <dataRoot>Users\ 之下（严格更深，等于 Users\ 本身不算 ——
 *            那会把所有账号的数据连同聊天记录一起删掉）；
 *          · dir 里没有<u>整段</u>为 ".." 的片段（防止用上跳绕出边界）。判的是路径段
 *            而非"出现过两个点" —— 账号净化后可能留下 "_.._Windows" 这类目录名，
 *            它在 Windows 上合法，不该被误拒。
 */
bool IsSafeToDeleteCacheDir(const std::wstring& dir, const std::wstring& dataRoot);

/**
 *  把字节数格式化成对话框里显示的占用量文本。
 *
 *  取整规则贴合"用户想知道值不值得清"这一诉求，不追求精确：小于 1 KB 的一律显示
 *  具体字节数（"0 字节"要能与"很小但不为空"区分开）；再往上按 KB / MB / GB 取一位
 *  小数。不用 TB —— 聊天缓存到不了那个量级，真到了显示成四位数的 GB 也读得懂。
 *
 *    bytes：字节数。
 *  @return 形如 "0 字节" / "512 字节" / "1.5 KB" / "23.4 MB" / "1.2 GB"。
 */
std::wstring FormatByteSize(unsigned long long bytes);

} // namespace balloonsettings
