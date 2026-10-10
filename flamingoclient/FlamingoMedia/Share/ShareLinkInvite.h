/**
 *  屏幕共享分享链接的文字，ShareLinkInvite.h
 *
 *  第 25 期（docs/屏幕共享与录制方案.md 第 12.4 节）：主持人可以为进行中的共享生成分享链接「flamingo://screenshare/<链接码>」，
 *  复制或发到任意会话；同企业、已登录的人在 Flamingo 聊天里点它即可加入。本文件是与窗口无关的纯函数，便于单元测试：
 *    - 由链接码拼出链接、从链接里取出链接码。exe 在聊天里只负责认出这种链接（Source/UI/BalloonChatWnd/ChatLinkDetect），
 *      点击后把整条链接交给 DLL 的 JoinScreenShareByLink，由本文件的 ParseShareLinkUrl 取出链接码；两处的前缀各写一份，
 *      由单元测试核对一致；
 *    - 主持人「复制分享链接」「发送邀请到…」时的邀请文字：标题、链接，房间有密码且没有打开免输密码时再加一行密码；
 *    - 凭链接加入前确认框的正文：标题、发起人、描述，强制模式时接上强制模式的说明。
 *  单元测试见 Source/Tests/ShareLinkInviteTests.cpp（标签 [sharelink]）。
 *
 *  典型用法：
 *      const std::wstring url = screenshare::MakeShareLinkUrl(code);
 *      flamingo::CopyTextToClipboard(screenshare::MakeShareInviteText(title, url, password));
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace screenshare {

//链接码的长度（字符），与服务端 ScreenShareLinkTable::kCodeLength 一致
const size_t kShareLinkCodeLength = 10;

/**
 *  链接码的格式是否合法：正好 kShareLinkCodeLength 个字符，且都取自服务端使用的 56 个字母数字（不含 0 O o 1 l I）。
 *  客户端只做格式检查，链接是否有效由服务端判定。
 *  @param code 待检查的链接码
 */
bool IsValidShareLinkCode(const std::wstring& code);

/**
 *  由链接码拼出分享链接「flamingo://screenshare/<链接码>」。
 *  @param code 链接码（UTF-8，来自服务端 1752 的应答）
 *  @return 分享链接；链接码格式不合法时返回空串
 */
std::wstring MakeShareLinkUrl(const std::string& code);

/**
 *  从分享链接里取出链接码：前缀（不区分大小写）后面正好是一个合法的链接码，不允许带别的字符。
 *  @param url     链接文字（聊天里识别出的链接，按不可信输入处理）
 *  @param outCode [出参] 链接码（UTF-8）；不合法时不改
 *  @return 是分享链接且链接码格式合法时返回 true
 */
bool ParseShareLinkUrl(const std::wstring& url, std::string& outCode);

/**
 *  邀请文字（「复制分享链接」放进剪贴板的、「发送邀请到…」发出的同一段文字）：
 *  第一行「邀请你观看屏幕共享「标题」：<链接>」；password 不为空时第二行「密码：xxxxxx」。
 *  @param title    共享标题；为空时写「邀请你观看屏幕共享：<链接>」
 *  @param url      分享链接
 *  @param password 要附上的密码：房间有密码且没有打开「点链接加入免输密码」时为密码，否则传空串
 */
std::wstring MakeShareInviteText(const std::wstring& title, const std::wstring& url, const std::wstring& password);

/**
 *  凭链接加入之前确认框的正文：「加入 发起人 的屏幕共享？」，下面写标题与描述；需要密码时说明加入后要输密码；
 *  强制模式时再接上强制模式的说明（与卡片「加入」按钮的确认一致）。
 *  @param hostName 发起人显示名；为空时写「对方」
 *  @param title    共享标题；为空时不写
 *  @param desc     共享描述；为空时不写
 *  @param locked   加入是否需要密码（打开了免输密码时服务端已回 0）
 *  @param forced   是否强制模式
 */
/**
 *  估算一段文字在确认小窗里占几行（2026-10-07，方案第 14.5 节：凭链接加入改用确认小窗，提问区按文字定高）：按换行符分段，
 *  每段按每行能放下的字数折行（空段算一行），结果限制在 [minLines, maxLines]。
 *  @param text         文字
 *  @param charsPerLine 一行大约放得下的字数（按中文字宽算），须大于 0
 *  @param minLines     至少几行
 *  @param maxLines     至多几行（超出的部分由小窗以省略号结尾）
 */
int EstimateShareTextLines(const std::wstring& text, int charsPerLine, int minLines, int maxLines);

std::wstring MakeShareLinkJoinConfirmText(const std::wstring& hostName, const std::wstring& title,
                                          const std::wstring& desc, bool locked, bool forced);

} // namespace screenshare
