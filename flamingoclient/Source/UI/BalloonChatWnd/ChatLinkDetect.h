/**
 *  ChatLinkDetect.h
 *
 *  聊天正文里的超链接识别（纯逻辑核心，不碰 UI、不碰网络）。
 *
 *  输入是一条消息的<u>扁平文本</u>（见 ChatInlineText.h 的口径说明：全文按 UTF-16
 *  字符计数，一个内联表情占一个字符位、用 U+FFFC 占位），输出是若干 LinkSpan ——
 *  每段在扁平文本里的字符区间，加上规范化后可直接打开的 URL。气泡与消息记录窗
 *  拿这些区间去做两件事：把那几个字画成链接色 + 下划线（复用 @提醒 那套按字符区间
 *  取矩形的机制），以及点击时按区间做命中测试。
 *
 *  ------------------------------------------------------------------
 *  识别口径（与用户对齐过的取舍）
 *
 *  · 认 `http://` 与 `https://` 开头的串（大小写不敏感）。
 *  · 认 `www.` 开头的串，打开时补 `https://`。
 *  · <u>不认裸域名</u>（如 `example.com`）。中文语境里误报太多 —— "报表2.0"、
 *    "版本1.5.doc" 这类都会被当成域名。
 *  · 非 ASCII 字符（含中文、中文标点、全角符号）一律<u>终止</u>本条链接。于是
 *    "详见https://a.com/b看看" 里链接恰好断在"看"之前。代价是<u>字面写着中文的
 *    路径不会被完整识别</u>（如 `https://a.com/文档`只识别到 `/`）；要带中文路径
 *    请用百分号编码（`%E6%96%87%E6%A1%A3`），那种形式是纯 ASCII，能完整识别。
 *    这个取舍是有意的：中文紧跟链接远比中文出现在路径里常见，前者判错会把正文
 *    吃进链接，后者最多少认几个字。
 *  · 尾随的英文标点（. , ; : ! ? ' "）剥掉；右括号 ) ] } 仅在链接内没有与之配对的
 *    左括号时才剥 —— `(https://a.com)` 要剥，`https://a.com/x(y)` 不能剥。
 *  · file: / javascript: / data: 这些协议一律不识别，UNC 路径（\\host\share）
 *    也不识别 —— 一条消息不该能让对方点开本地程序或访问内网共享。
 *  ------------------------------------------------------------------
 *
 *  单元测试见 flamingoclient/Source/Tests/ChatLinkDetectTests.cpp（Suite CLD）。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>
#include <vector>

namespace balloonchatwnd {

/**
 *  一条链接在正文里的写法。决定打开时要不要补协议。
 */
enum LinkScheme
{
    kLinkSchemeHttp  = 0,   // 正文里就写着 http://，原样打开
    kLinkSchemeHttps = 1,   // 正文里就写着 https://，原样打开
    kLinkSchemeBare  = 2,   // 正文里是 www. 开头没写协议，打开时补 https://
    // 屏幕共享的分享链接「flamingo://screenshare/<10 位链接码>」（docs/屏幕共享与录制方案.md 第 12.4.4 节）：
    // 点击后在客户端里凭链接加入共享，不交给浏览器；链接码不合规的不识别
    kLinkSchemeScreenShare = 3,
    // 加群链接「flamingo://joingroup?code=<12 位随机码>」（docs/邀请入群方案.md §12）：点击后在客户端里查群并加入，
    // 不交给浏览器；随机码不合规的不识别。与屏幕共享分享链接同属站内链接
    kLinkSchemeJoinGroup = 4
};

/**
 *  一段被识别出来的链接。
 */
struct LinkSpan
{
    int          m_off;      // 起始字符下标（扁平文本口径）
    int          m_len;      // 字符数；m_off + m_len 为区间右开端点
    std::wstring m_url;      // 规范化后的完整 URL，可直接交给 ShellExecute
    int          m_scheme;   // 正文里的写法，取值见 LinkScheme

    LinkSpan()
        : m_off(0)
        , m_len(0)
        , m_scheme(kLinkSchemeHttps)
    {
    }
};

/**
 *  从扁平文本里识别出所有链接。纯函数、无副作用。
 *
 *  区间按 m_off 升序返回、互不重叠。识别口径见文件头。
 *    flatText：消息正文的扁平文本（UTF-16，表情已用 U+FFFC 占位）。
 *  @return 链接区间表；没有链接时返回空表。
 */
std::vector<LinkSpan> DetectLinks(const std::wstring& flatText);

/**
 *  从扁平文本里识别链接，分别控制网址与分享链接（屏幕共享第 25 期）。
 *    flatText：消息正文的扁平文本。
 *    webLinks：是否识别网址（http / https / www.）；用户关掉「识别链接」时传 false。
 *    shareLinks：是否识别屏幕共享的分享链接（kLinkSchemeScreenShare）；它是站内功能，聊天气泡里不受「识别链接」开关影响，
 *                消息记录窗里传 false（那里点了也无法加入）。
 *  @return 链接区间表，按 m_off 升序、互不重叠。
 */
std::vector<LinkSpan> DetectLinks(const std::wstring& flatText, bool webLinks, bool shareLinks);

/**
 *  是不是屏幕共享的分享链接（第 25 期）：前缀 flamingo://screenshare/（大小写不敏感）之后正好是 10 位链接码，
 *  链接码只用数字 2~9 与去掉 I O l o 的字母（与服务端、FlamingoMedia/Share/ShareLinkInvite 一致）。
 *    url：链接文字。
 *  @return 是合规的分享链接返回 true。
 */
bool IsScreenShareLinkUrl(const std::wstring& url);

/**
 *  是不是合规的加群链接（docs/邀请入群方案.md §12）：flamingo://joingroup?code= 之后是 12 位随机码
 *  （判定与 net::GroupLinkProtocolClient::parseJoinUrl 相同）。
 *    url：链接文字。
 *    code：[出参，可为 NULL] 合规时填入小写的随机码。
 *  @return 是合规的加群链接返回 true。
 */
bool IsJoinGroupLinkUrl(const std::wstring& url, std::string* code);

/**
 *  一个 URL 是否允许直接打开。打开链接前<u>必须</u>过这一关 —— DetectLinks 的
 *  产物固然安全，但气泡右键"打开链接"之类的路径日后可能引入别的来源。
 *
 *  只放行 http:// 与 https://（大小写不敏感）。
 *    url：待检查的完整 URL。
 *  @return 允许打开返回 true。
 */
bool IsSafeLinkUrl(const std::wstring& url);

/**
 *  进程级「聊天内容里是否自动识别链接」开关。<u>默认开</u>。
 *
 *  之所以做成进程级快照而不是每个气泡去读配置文件：气泡在测量阶段就要知道要不要
 *  识别，而那是每条消息、每次重排都会走的热路径，不该每次去碰 CUserConfig。登录后
 *  由主面板按该账号的配置调一次 Setter，用户在系统设置里改动时再调一次，随后让已
 *  打开的聊天窗重新测量重绘即可。
 *
 *  只在 UI 线程读写，故未加锁。
 *  @return 当前是否识别链接。
 */
bool ChatLinkOptionEnabled();

/**
 *  设置上面那个进程级开关。
 *    enabled：true 识别链接、false 一律当普通文字。
 */
void SetChatLinkOptionEnabled(bool enabled);

} // namespace balloonchatwnd
