/**
 *  ChatMockData.cpp
 *
 *  单聊窗 mock 数据的定义。数据取自 design 的 data.js。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，故自带 ATL/WTL 前置
 *  依赖。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "ChatMockData.h"
#include "FileBubbleFormat.h"   // FormatFileSize / FileTypeLabelFromName（文件气泡大小文字与类型标签）

namespace balloonchatwnd {

namespace {

// 构造一条时间分隔消息。
ChatMessage MakeTime(LPCTSTR text)
{
    ChatMessage m;
    m.kind = kMsgTime;
    m.fromMe = false;
    m.text = text;
    m.imageW = m.imageH = 0;
    m.fileProgress = 0;
    m.hasQuote = false;
    m.senderAvatarColor = 0;   // 单聊消息无发送者信息
    m.read = false;
    return m;
}

// 构造一条文字 / emoji 消息。
ChatMessage MakeText(int kind, bool fromMe, LPCTSTR text)
{
    ChatMessage m;
    m.kind = kind;
    m.fromMe = fromMe;
    m.text = text;
    m.imageW = m.imageH = 0;
    m.fileProgress = 0;
    m.hasQuote = false;
    m.senderAvatarColor = 0;   // 单聊消息无发送者信息
    m.read = fromMe;   // 自己的消息：mock 一律已读
    return m;
}

// 构造一条图片消息。
ChatMessage MakeImage(bool fromMe, LPCTSTR caption, int w, int h)
{
    ChatMessage m;
    m.kind = kMsgImage;
    m.fromMe = fromMe;
    m.imageCaption = caption;
    m.imageW = w;
    m.imageH = h;
    m.fileProgress = 0;
    m.hasQuote = false;
    m.senderAvatarColor = 0;   // 单聊消息无发送者信息
    m.read = fromMe;
    return m;
}

// 构造一条文件消息。
ChatMessage MakeFile(bool fromMe, LPCTSTR name, LPCTSTR size,
                     LPCTSTR type, int progress)
{
    ChatMessage m;
    m.kind = kMsgFile;
    m.fromMe = fromMe;
    m.fileName = name;
    m.fileSize = size;
    m.fileType = type;
    m.fileProgress = progress;
    m.imageW = m.imageH = 0;
    m.hasQuote = false;
    m.senderAvatarColor = 0;   // 单聊消息无发送者信息
    m.read = fromMe;
    return m;
}

} // anonymous namespace

const ChatPeer& ChatPartner()
{
    static ChatPeer peer;
    static bool inited = false;
    if (!inited)
    {
        peer.name        = _T("苏沐辰");
        peer.avatarText  = _T("苏");
        peer.avatarColor = RGB(0xFF, 0x7A, 0x45);
        peer.signature   = _T("今天也要好好生活");
        peer.status      = kChatOnline;
        inited = true;
    }
    return peer;
}

const ChatPeer& ChatMe()
{
    static ChatPeer me;
    static bool inited = false;
    if (!inited)
    {
        me.name        = _T("林知夏");
        me.avatarText  = _T("L");
        me.avatarColor = RGB(0xFF, 0x7A, 0x45);
        me.signature   = _T("想做一件让自己骄傲的事");
        me.status      = kChatOnline;
        inited = true;
    }
    return me;
}

//假消息 msgid 的起始值。单聊 / 群聊各用一段，互不重叠 —— 两个调试窗口同时开着时，
//按 msgid 定位气泡的那些接口（SetPinnedByMsgid 等）才不会串到另一个窗口的行上。
const long long kMockSingleMsgIdBase = 900001;
const long long kMockGroupMsgIdBase  = 910001;

//给一批假消息按顺序补上服务器 msgid；时间分隔行（kMsgTime）跳过 —— 它不是真实
//消息，本来就不该有 msgid，右键菜单也不对它弹出。
//    msgs：待补的消息表（就地修改）
//    base：起始 msgid
static void AssignMockMsgIds(std::vector<ChatMessage>& msgs, long long base)
{
    long long next = base;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        if (msgs[i].kind == kMsgTime)
        {
            continue;
        }
        msgs[i].msgid = next;
        ++next;
    }
}

//把第一条真实消息标成"已 Pin"，使调试入口下也能看到气泡上的图钉标记，以及右键菜单里
//那一项取反成「取消 Pin」的样子。真实会话里这个标记由服务端推送维护，与本函数无关。
//    msgs：待标记的消息表（就地修改）
static void MarkFirstRealMessagePinned(std::vector<ChatMessage>& msgs)
{
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        if (msgs[i].kind == kMsgTime)
        {
            continue;
        }
        msgs[i].pinned = true;
        return;
    }
}

const std::vector<ChatMessage>& ChatMessages()
{
    // design MESSAGES.c1（与苏沐辰的会话）。
    static std::vector<ChatMessage> msgs;
    if (msgs.empty())
    {
        msgs.push_back(MakeTime(_T("今天 14:00")));
        msgs.push_back(MakeText(kMsgText, false,
            _T("在吗？刚刚看到你发的稿子，整体很喜欢～")));
        msgs.push_back(MakeText(kMsgText, false,
            _T("就是 banner 那块的字号感觉再大点会更舒服")));

        ChatMessage reply = MakeText(kMsgText, true, _T("好哒，我调整下再发你"));
        reply.hasQuote  = true;
        reply.quoteName = _T("苏沐辰");
        reply.quoteText = _T("就是 banner 那块的字号感觉再大点会更舒服");
        msgs.push_back(reply);

        msgs.push_back(MakeImage(true, _T("banner-v2"), 240, 140));
        msgs.push_back(MakeText(kMsgEmoji, false, _T("👍")));
        msgs.push_back(MakeText(kMsgText, false,
            _T("就这样！顺便把字体换成思源也试试")));
        msgs.push_back(MakeText(kMsgText, true, _T("OK，等会发文件给你")));
        msgs.push_back(MakeFile(true, _T("banner-final.fig"), _T("4.2 MB"),
            _T("fig"), 100));
        msgs.push_back(MakeText(kMsgText, false, _T("收到啦，下载中…")));
        msgs.push_back(MakeFile(false, _T("品牌色规范.pdf"), _T("1.8 MB"),
            _T("pdf"), 62));
        msgs.push_back(MakeText(kMsgText, false,
            _T("这是上次品牌讨论的总结，先发你看下")));

        // 一条带内联表情的消息 —— 正文里的 /f["<id>"] 会被排版引擎画成
        // Bin/Face 下的小图（见 ChatInlineText.{h,cpp}）。假数据里必须留这么一条：
        // 否则两个调试入口下永远走不到内联表情那条渲染分支，目检时看不出它有没有坏。
        msgs.push_back(MakeText(kMsgText, true,
            _T("收到 /f[\"5\"] 我这就看，晚点回你 /f[\"12\"]")));

        // 一条较长的消息 —— 用于验证气泡内文字自动折行（超过气泡最大
        // 宽度时应在气泡内换行、气泡随之变高，而不是被截断）。
        msgs.push_back(MakeText(kMsgText, false,
            _T("对了，关于这次品牌升级我整理了几点想法：主色调建议从橙色")
            _T("微调到更暖一些的珊瑚色，辅助色保留现在的灰绿搭配；字体方面")
            _T("思源黑体确实更适合长段落阅读，标题也可以试试更重的字重。")
            _T("你看周五之前能不能出一版对比稿？")));

        msgs.push_back(MakeText(kMsgText, true,
            _T("好的好的，我先按这个思路出一版，争取周四就给你看初稿，")
            _T("有问题我们再当面过一遍")));

        // 一条带网址的消息 —— 正文里的链接会被识别成可点的超链接（蓝色 + 下划线，
        // 见 ChatLinkDetect.{h,cpp}）。假数据里必须留这么一条：否则两个调试入口下
        // 永远走不到链接渲染那条分支，目检时看不出它有没有坏。这里同时覆盖三种写法：
        // 写明 https、只写 www.、以及紧挨中文（应断在"这几"之前）。
        //
        // 网址按<u>原样</u>写即可：正文格式里的转义只认"紧跟表情标记的双斜杠"
        // （见 BuildSendBody / ParseSendBody），网址里的 // 不受影响。
        msgs.push_back(MakeText(kMsgText, false,
            _T("参考资料我放这儿了 https://example.com/brand/guide?v=2 ")
            _T("还有 www.example.org 这几个都看一下")));

        //给各条补上假的服务器 msgid。真实会话里每条消息都带 msgid（见
        //CBalloonChatWindow::MakeRealTextMessage），而凡是"拿 msgid 说事"的界面
        //——右键菜单里的「Pin / 取消 Pin」就是——在 msgid 为 0 时整项不出现。
        //假数据不补上的话，调试入口里这类功能一律看不见，等于没法目检。
        AssignMockMsgIds(msgs, kMockSingleMsgIdBase);

        //把其中一条标成已 Pin，好让图钉标记与「取消 Pin」文案在调试入口下也能看到。
        MarkFirstRealMessagePinned(msgs);
    }
    return msgs;
}

ChatMessage MakeTextMessage(bool fromMe, const CString& text)
{
    // 复用匿名命名空间里的 MakeText（同一编译单元内可见）。
    return MakeText(kMsgText, fromMe, (LPCTSTR)text);
}

ChatMessage MakeImageMessage(bool fromMe, const CString& imagePath,
                             int dispW, int dispH,
                             const std::string& clientMsgId, int state)
{
    // 复用匿名命名空间里的 MakeImage（同一编译单元内可见）打底，再补真实
    // 渲染 / 发送态字段。caption 留空 —— 真实图片由 ChatBubble 解码 imagePath
    // 绘制，覆盖文字（上传中 % / 失败）由 imageState 决定。
    ChatMessage m = MakeImage(fromMe, _T(""), dispW, dispH);
    m.imagePath     = imagePath;
    m.clientMsgId   = clientMsgId;
    m.imageState    = state;
    m.imageProgress = 0;
    return m;
}

ChatMessage MakeDownloadingImageMessage(bool fromMe, int dispW, int dispH, long long msgid)
{
    //下载中：imagePath 暂空、state=3，记 msgid 供下载结果回填定位本行。
    ChatMessage m = MakeImage(fromMe, _T(""), dispW, dispH);
    m.imageState = 3;   // 下载中
    m.msgid      = msgid;
    return m;
}

ChatMessage MakeFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                            const CString& localPath, const std::string& clientMsgId, int state)
{
    //复用匿名命名空间里的 MakeFile 打底（置好基础字段），再补真实收发态字段。
    //大小文字与类型标签由字节数 / 文件名经 FileBubbleFormat 算出。
    CString sizeText(FormatFileSize(sizeBytes).c_str());
    CString typeText(FileTypeLabelFromName(std::wstring(fileName.GetString())).c_str());
    ChatMessage m = MakeFile(fromMe, (LPCTSTR)fileName, (LPCTSTR)sizeText, (LPCTSTR)typeText, 0);
    m.fileSizeBytes   = sizeBytes;
    m.fileLocalPath   = localPath;
    m.fileClientMsgId = clientMsgId;
    m.fileState       = state;
    return m;
}

ChatMessage MakeDownloadingFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                                       long long msgid, const std::string& fileId,
                                       const std::string& md5)
{
    //下载中：fileLocalPath 暂空、fileState=3，记 msgid / fileId / md5 供下载与回填用。
    return MakeIncomingFileMessage(fromMe, fileName, sizeBytes, msgid, fileId, md5, 3);
}

ChatMessage MakeIncomingFileMessage(bool fromMe, const CString& fileName, long long sizeBytes,
                                    long long msgid, const std::string& fileId,
                                    const std::string& md5, int state)
{
    //fileLocalPath 暂空（尚未落地），记 msgid / fileId / md5——「接收」与「重新下载」
    //都要靠这三样发起下载，下载结果也按 msgid 回填本行。
    CString sizeText(FormatFileSize(sizeBytes).c_str());
    CString typeText(FileTypeLabelFromName(std::wstring(fileName.GetString())).c_str());
    ChatMessage m = MakeFile(fromMe, (LPCTSTR)fileName, (LPCTSTR)sizeText, (LPCTSTR)typeText, 0);
    m.fileSizeBytes = sizeBytes;
    m.fileMsgid     = msgid;
    m.fileFileId    = fileId;
    m.fileMd5       = md5;
    m.fileState     = state;
    return m;
}

CString MockAutoReply(int index)
{
    // mock 自动回复文字池 —— 取自 design chat.jsx 的 pickAutoReply。
    static const LPCTSTR kReplies[] =
    {
        _T("收到～"),
        _T("好的，没问题"),
        _T("嗯嗯，明白了"),
        _T("哈哈，可以呀"),
        _T("稍等，我看一下"),
        _T("👍"),
    };
    const int kReplyCount = (int)(sizeof(kReplies) / sizeof(kReplies[0]));
    int i = index % kReplyCount;
    if (i < 0)
    {
        i = 0;
    }
    return CString(kReplies[i]);
}

// =====================================================================
// 群聊 mock 数据
// =====================================================================

namespace {

// 向成员列表追加一名成员。
void PushMember(std::vector<GroupMember>& vec, LPCTSTR name, LPCTSTR avatar,
                COLORREF color, bool isOwner, int status)
{
    GroupMember m;
    m.name        = name;
    m.avatarText  = avatar;
    m.avatarColor = color;
    m.isOwner     = isOwner;
    m.status      = status;
    vec.push_back(m);
}

// 构造一条群聊里"对方"发的文字消息（带发送者信息）。
ChatMessage MakeGroupText(LPCTSTR senderName, LPCTSTR senderAvatar,
                          COLORREF senderColor, LPCTSTR text)
{
    ChatMessage m = MakeText(kMsgText, false, text);
    m.senderName        = senderName;
    m.senderAvatarText  = senderAvatar;
    m.senderAvatarColor = senderColor;
    return m;
}

} // anonymous namespace

const GroupInfo& MockGroupInfo()
{
    static GroupInfo g;
    static bool inited = false;
    if (!inited)
    {
        g.name        = _T("FlamingoIM 项目组");
        g.memberCount = 28;
        g.notice      = _T("v3.2 版本即将发布，全员准备。");
        inited = true;
    }
    return g;
}

const std::vector<GroupMember>& MockGroupMembers()
{
    // design 08 边栏可见的 9 名成员；群主为谢知行。
    static std::vector<GroupMember> members;
    if (members.empty())
    {
        PushMember(members, _T("林知夏"),     _T("林"), RGB(0xFF, 0x7A, 0x45),
                   false, kChatOnline);
        PushMember(members, _T("苏沐辰"),     _T("苏"), RGB(0xFF, 0x7A, 0x45),
                   false, kChatOnline);
        PushMember(members, _T("设计组组长"), _T("设"), RGB(0x37, 0x95, 0xE8),
                   false, kChatAway);
        PushMember(members, _T("谢知行"),     _T("谢"), RGB(0x22, 0xA5, 0x65),
                   true,  kChatOnline);
        PushMember(members, _T("宋微之"),     _T("宋"), RGB(0x0E, 0xA5, 0xB8),
                   false, kChatOnline);
        PushMember(members, _T("方亦初"),     _T("方"), RGB(0xA6, 0x5E, 0xFF),
                   false, kChatOnline);
        PushMember(members, _T("唐砚秋"),     _T("唐"), RGB(0xE0, 0x6A, 0x8B),
                   false, kChatOffline);
        PushMember(members, _T("温野"),       _T("温"), RGB(0x4C, 0xAF, 0x50),
                   false, kChatOnline);
        PushMember(members, _T("前同事"),     _T("前"), RGB(0x8B, 0x5C, 0xF6),
                   false, kChatOffline);
    }
    return members;
}

const std::vector<ChatMessage>& MockGroupMessages()
{
    // design 08 的群会话片段。发送者头像底色与 MockGroupMembers 保持一致。
    static std::vector<ChatMessage> msgs;
    if (msgs.empty())
    {
        const COLORREF kXie  = RGB(0x22, 0xA5, 0x65);   // 谢知行
        const COLORREF kSong = RGB(0x0E, 0xA5, 0xB8);   // 宋微之
        const COLORREF kFang = RGB(0xA6, 0x5E, 0xFF);   // 方亦初

        msgs.push_back(MakeTime(_T("今天 14:20")));
        msgs.push_back(MakeGroupText(_T("谢知行"), _T("谢"), kXie,
            _T("各位下午好，简单同步下 v3.2 的进度")));
        msgs.push_back(MakeGroupText(_T("宋微之"), _T("宋"), kSong,
            _T("后端这边接口都准备好了，等前端联调")));
        msgs.push_back(MakeGroupText(_T("方亦初"), _T("方"), kFang,
            _T("我这边的群文件功能这周内可以提测")));
        // @提醒 的 mock：这一条 @ 的是林知夏（下标 0 即"自己"），故 m_isMe 为真，
        // 气泡里该显示成橙色高亮块。偏移是 UTF-16 字符口径：'@' 占 1 个、"林知夏"
        // 占 3 个，共 4 个字符，从 0 开始。
        ChatMessage atMe = MakeGroupText(_T("谢知行"), _T("谢"), kXie,
            _T("@林知夏 新版本的设计稿麻烦确认下"));
        {
            RenderSpan span;
            span.m_uid  = 1001;   // mock uid，仅用于占位；真实数据由协议层填
            span.m_off  = 0;
            span.m_len  = 4;
            span.m_isMe = true;
            atMe.ats.push_back(span);
        }
        msgs.push_back(atMe);

        // 对方气泡里 @ 别人：应显示成蓝色高亮块。
        ChatMessage atOther = MakeGroupText(_T("宋微之"), _T("宋"), kSong,
            _T("@方亦初 群文件那块辛苦你了"));
        {
            RenderSpan span;
            span.m_uid  = 1002;
            span.m_off  = 0;
            span.m_len  = 4;
            span.m_isMe = false;
            atOther.ats.push_back(span);
        }
        msgs.push_back(atOther);

        // 我方回复，引用谢知行的上一条。
        ChatMessage reply = MakeText(kMsgText, true,
            _T("好的，我今晚之前给反馈"));
        reply.hasQuote  = true;
        reply.quoteName = _T("谢知行");
        reply.quoteText = _T("@林知夏 新版本的设计稿麻烦确认下");
        msgs.push_back(reply);

        // 自己气泡里 @ 别人：浅橙底上的蓝块，验证第四格配色。
        ChatMessage selfAt = MakeText(kMsgText, true,
            _T("@谢知行 设计稿我看完了，没问题"));
        {
            RenderSpan span;
            span.m_uid  = 1003;
            span.m_off  = 0;
            span.m_len  = 4;
            span.m_isMe = false;
            selfAt.ats.push_back(span);
        }
        msgs.push_back(selfAt);

        //理由同 ChatMessages()：不补 msgid 的话，调试入口里「Pin / 取消 Pin」不出现。
        AssignMockMsgIds(msgs, kMockGroupMsgIdBase);
        MarkFirstRealMessagePinned(msgs);
    }
    return msgs;
}

ChatMessage MakeGroupAutoReply(int index)
{
    const std::vector<GroupMember>& members = MockGroupMembers();
    const int n = (int)members.size();

    // 下标 0 是林知夏（自己），回复在其余成员里轮转选取。
    int idx = 1;
    if (n > 1)
    {
        idx = 1 + (index % (n - 1));
    }
    if (idx < 0 || idx >= n)
    {
        idx = (n > 0) ? (n - 1) : 0;
    }

    const GroupMember& mem = members[idx];
    return MakeGroupText(mem.name, mem.avatarText, mem.avatarColor,
                         MockAutoReply(index));
}

} // namespace balloonchatwnd
