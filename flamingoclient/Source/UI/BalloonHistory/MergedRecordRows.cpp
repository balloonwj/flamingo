/**
 *  合并转发的聊天记录在查看窗里的各行数据，见 MergedRecordRows.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 HistoryWindowLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "MergedRecordRows.h"
#include "HistoryRowFormat.h"                  // FormatTimeShort / LocalDayKey / ColorForId
#include "HistoryView.h"                       // PreviewFromContent / FullTextFromContent / ParseContentBrief
#include "V2MsgQuery.h"                        // BuildContentJson（子消息转成归一化 content）
#include "../../net/MessageProtocolClient.h"   // net::MergedRecord / parseMergedContent
#include "../../net/Protocol.h"                // ::message_type_*
#include "../BalloonChatWnd/ChatFileStateModel.h"   // 文件态取值
#include "../BalloonChatWnd/ChatReceivedVoice.h"    // BuildStoredVoiceMessage：语音行的本机文件状态
#include "../BalloonChatWnd/ChatVoiceBubble.h"      // ApplyVoiceMessage：语音子消息做成语音条
#include "../BalloonChatWnd/ChatMergedCard.h"       // MergedDirectFilePlaceholder：在线直传文件子消息的说明文字
#include "../../Common/Code/CodeLexer.h"             // CodeLanguageFromId：代码片段子消息的语言
#include "../BalloonChatWnd/ChatCodeMessage.h"       // CodeNotesFromData / CodeLineCount：代码片段子消息的批注
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonhistory {

namespace {

//嵌套记录行的文字前缀，与会话列表摘要、聊天窗卡片的类型文字一致
const wchar_t* const kNestedRowPrefix = TR_NOOP(L"[聊天记录]");

//UTF-8 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (n <= 0)
    {
        return CString();
    }
    CString out;
    LPWSTR buf = out.GetBuffer(n);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), buf, n);
    out.ReleaseBuffer(n);
    return out;
}

// 填写一行的正文与内容块相关字段。
//   sub：子消息；row：[出参] 要填写的行。
void FillRowContent(const net::MessageNotify& sub, HistoryRowView& row)
{
    //嵌套记录：画成一张聊天记录卡片（标题 + 条数），单击标题打开下一层由查看窗处理。
    //正文仍写「[聊天记录] 标题」，复制与搜索按它取文字
    if (sub.m_msgtype == ::message_type_merged)
    {
        row.m_previewText = MergedNestedRowText(sub.m_mergedContent);
        row.m_fullText    = row.m_previewText;
        row.m_isMergedCard = true;
        net::MergedRecord nested;
        if (net::MessageProtocolClient::parseMergedContent(sub.m_mergedContent, nested))
        {
            row.m_mergedTitle = Utf8ToCStr(nested.m_title);
            row.m_mergedCount = (int)nested.m_messages.size();
        }
        return;
    }

    //在线直传的文件（2026-10-05 起）：服务器上没有副本，合并转发时文件本身没有随记录发出。只写一行说明文字，
    //不画可下载的文件卡片（下载必然失败）
    if (sub.m_msgtype == ::message_type_file && sub.m_fileDirect)
    {
        row.m_previewText = balloonchatwnd::MergedDirectFilePlaceholder(sub.m_fileName);
        row.m_fullText    = row.m_previewText;
        return;
    }

    //其余类型先转成与本地库相同的归一化 content，再走消息记录窗那套取文字的函数，
    //两个窗口对同一种消息的显示因此一致
    const std::string content = BuildContentJson(sub);
    row.m_previewText = Utf8ToCStr(PreviewFromContent(sub.m_msgtype, content));

    const HistoryContentBrief brief = ParseContentBrief(sub.m_msgtype, content);
    if (sub.m_msgtype == ::message_type_image || sub.m_msgtype == ::message_type_screenshot)
    {
        //图片行一律画预览图：本地路径与显示状态由查看窗按登录账号的缓存目录补上
        row.m_hasImageThumb = true;
        row.m_imageWidth    = brief.m_imageWidth;
        row.m_imageHeight   = brief.m_imageHeight;
        row.m_fullText      = row.m_previewText;
    }
    else if (sub.m_msgtype == ::message_type_file && !brief.m_fileName.empty())
    {
        //文件行：用聊天窗的文件卡片显示，可以直接下载、打开；本地路径由查看窗按登记表与
        //下载缓存补上，这里先按「未下载」构造。下面几项仍照常填写，复制与搜索按它们取文字
        row.m_chatFileCard = true;
        row.m_fileCard     = BuildMergedFileCard(sub, std::wstring());
        //类型色块 + 文件名 + 大小，正文写成"名字 · 大小"，与消息记录窗一致
        row.m_thumbFileExt = Utf8ToCStr(FileExtensionOf(brief.m_fileName));
        row.m_fileName     = Utf8ToCStr(brief.m_fileName);
        CString text = row.m_fileName;
        const std::string sizeText = FormatFileSize(brief.m_fileSize);
        if (!sizeText.empty())
        {
            row.m_fileSizeText = Utf8ToCStr(sizeText);
            text += _T(" · ");
            text += row.m_fileSizeText;
        }
        row.m_previewText = text;
        row.m_fullText    = text;
    }
    else if (sub.m_msgtype == ::message_type_voice)
    {
        //语音：画成与聊天窗同款的语音条；合并记录里没有已听状态，一律按已听、不画红点。本机文件状态由查看窗按
        //缓存目录补（ResolveVoiceRowLocalState），这里先按就绪构造。正文写「[语音] 1′05″」，复制与搜索按它取文字
        row.m_isVoice = true;
        row.m_voiceShowDot = false;
        row.m_voice = balloonchatwnd::ChatMessage();
        row.m_voice.fromMe = false;
        row.m_voice.msgid = (long long)sub.m_msgid;
        balloonchatwnd::ApplyVoiceMessage(row.m_voice, sub.m_voiceFileId, sub.m_voiceMd5, sub.m_voiceDuration,
                                          (long long)sub.m_voiceSize, sub.m_voiceWave, true);
        row.m_fullText = row.m_previewText;
    }
    else if (sub.m_msgtype == ::message_type_folder)
    {
        //文件夹传输记录：一行文字写明名字、规模与结局，与消息记录窗一致
        row.m_previewText = Utf8ToCStr(BuildFolderRowText(brief));
        row.m_fullText    = row.m_previewText;
    }
    else
    {
        //文本保留 /f["<id>"] 内联表情标记，交给行内容块画成表情；其余类型与预览相同
        row.m_fullText = Utf8ToCStr(FullTextFromContent(sub.m_msgtype, content));
        //代码片段：画成与聊天窗相同的代码卡片（docs/代码片段方案.md 第 3.4 节），代码取上面的正文
        if (sub.m_msgtype == ::message_type_text && sub.m_textFormat == net::text_format_code)
        {
            row.m_isCode = true;
            row.m_codeLang = codesnippet::CodeLanguageFromId(sub.m_codeLang);
            //批注按代码行数整理（docs/代码片段方案.md 第 10.5 节）
            row.m_codeNotes = balloonchatwnd::CodeNotesFromData(
                sub.m_codeNotes, balloonchatwnd::CodeLineCount(std::wstring((LPCWSTR)row.m_fullText)), sub.m_from);
        }
        //Markdown 消息：FlamingoMarkdown.dll 可用时画成与聊天窗相同的 Markdown 卡片（docs/Markdown消息方案.md 第 12 节），
        //点卡片打开预览窗口；DLL 不可用时仍按普通文字显示源文本。合并记录里不区分本人，预览窗口显示原发送者的名字
        else if (sub.m_msgtype == ::message_type_text && sub.m_textFormat == net::text_format_markdown)
        {
            row.m_isMarkdown = balloonchatwnd::MakeMarkdownCardInfo(row.m_fullText,
                                                                    std::wstring((LPCWSTR)row.m_senderName), false,
                                                                    sub.m_timestamp, sub.m_msgid, row.m_markdownCard);
        }
    }
}

} // namespace

CString ResolveMergedSenderName(const std::string& fromNameUtf8, int64_t fromid,
                                const std::map<int64_t, CString>& fallbackNames)
{
    if (!fromNameUtf8.empty())
    {
        return Utf8ToCStr(fromNameUtf8);
    }
    std::map<int64_t, CString>::const_iterator it = fallbackNames.find(fromid);
    if (it != fallbackNames.end() && !it->second.IsEmpty())
    {
        return it->second;
    }
    CString name;
    name.Format(TR(L"用户%lld"), (long long)fromid);
    return name;
}

CString MergedNestedRowText(const std::string& mergedContentUtf8)
{
    CString text(TR(kNestedRowPrefix));
    net::MergedRecord nested;
    if (net::MessageProtocolClient::parseMergedContent(mergedContentUtf8, nested)
        && !nested.m_title.empty())
    {
        text += _T(" ");
        text += Utf8ToCStr(nested.m_title);
    }
    return text;
}

balloonchatwnd::ChatMessage BuildMergedFileCard(const net::MessageNotify& sub,
                                                const std::wstring& localPath)
{
    const CString name = Utf8ToCStr(sub.m_fileName);
    balloonchatwnd::ChatMessage card;
    if (!localPath.empty())
    {
        //本地已有完整文件：已完成，可直接打开。md5 与 fileid 也带上，文件日后被删掉、
        //用户点「重新下载」时还要用
        card = balloonchatwnd::MakeFileMessage(false, name, sub.m_fileSize,
                                               CString(localPath.c_str()), std::string(),
                                               balloonchatwnd::kFileNormal);
        card.fileMsgid  = sub.m_msgid;
        card.fileFileId = sub.m_fileFileId;
        card.fileMd5    = sub.m_fileMd5;
    }
    else
    {
        //本地没有：未下载，等用户点「下载」
        card = balloonchatwnd::MakeIncomingFileMessage(false, name, sub.m_fileSize, sub.m_msgid,
                                                       sub.m_fileFileId, sub.m_fileMd5,
                                                       balloonchatwnd::kFilePendingAccept);
    }
    //聊天记录里的文件没有接收 / 拒绝流程，待下载时只给「下载」
    card.fileDirectDownload = true;
    return card;
}

void MergedFileStateTable::Start(int64_t msgid)
{
    Entry e;
    e.m_state    = balloonchatwnd::kFileDownloading;
    e.m_progress = 0;
    m_entries[msgid] = e;
}

bool MergedFileStateTable::Progress(int64_t msgid, int percent)
{
    std::map<int64_t, Entry>::iterator it = m_entries.find(msgid);
    if (it == m_entries.end() || it->second.m_state != balloonchatwnd::kFileDownloading)
    {
        return false;
    }
    const int kMaxPercent = 100;
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > kMaxPercent)
    {
        percent = kMaxPercent;
    }
    it->second.m_progress = percent;
    return true;
}

bool MergedFileStateTable::Finish(int64_t msgid, bool ok, const std::wstring& localPath,
                                  bool cancelled)
{
    std::map<int64_t, Entry>::iterator it = m_entries.find(msgid);
    if (it == m_entries.end())
    {
        return false;
    }
    if (ok)
    {
        it->second.m_state     = balloonchatwnd::kFileNormal;
        it->second.m_localPath = localPath;
    }
    else
    {
        //失败与取消都保留进度，进度条停在中断处
        it->second.m_state = cancelled ? balloonchatwnd::kFileCancelled
                                       : balloonchatwnd::kFileFailed;
        it->second.m_localPath.clear();
    }
    return true;
}

bool MergedFileStateTable::IsDownloading(int64_t msgid) const
{
    std::map<int64_t, Entry>::const_iterator it = m_entries.find(msgid);
    return it != m_entries.end() && it->second.m_state == balloonchatwnd::kFileDownloading;
}

void MergedFileStateTable::Apply(balloonchatwnd::ChatMessage& card) const
{
    std::map<int64_t, Entry>::const_iterator it = m_entries.find(card.fileMsgid);
    if (it == m_entries.end())
    {
        return;
    }
    card.fileState    = it->second.m_state;
    card.fileProgress = it->second.m_progress;
    if (it->second.m_state == balloonchatwnd::kFileNormal)
    {
        card.fileLocalPath = it->second.m_localPath.c_str();
    }
}

void MergedFileStateTable::Clear()
{
    m_entries.clear();
}

void BuildMergedRecordRows(const net::MergedRecord& record,
                           const std::map<int64_t, CString>& fallbackNames,
                           int64_t todayKey,
                           std::vector<HistoryRowView>& out)
{
    out.clear();
    out.reserve(record.m_messages.size());

    int64_t prevDayKey = 0;
    for (size_t i = 0; i < record.m_messages.size(); ++i)
    {
        const net::MessageNotify& sub = record.m_messages[i];

        HistoryRowView row;
        row.m_senderName  = ResolveMergedSenderName(sub.m_fromName, sub.m_from, fallbackNames);
        row.m_avatarColor = ColorForId(sub.m_from);
        row.m_timeText    = FormatTimeShort(sub.m_timestamp);
        row.m_msgid       = sub.m_msgid;
        FillRowContent(sub, row);

        //日期分组头：第一行恒有，此后每当日期与上一行不同就插一条。日期键换算失败（0）
        //时不插，免得出现一条空的分组头
        const int64_t dayKey = LocalDayKey(sub.m_timestamp);
        if (dayKey != 0 && (i == 0 || dayKey != prevDayKey))
        {
            row.m_groupLabel = Utf8ToCStr(FormatDateGroupLabel(dayKey, todayKey));
        }
        prevDayKey = dayKey;

        out.push_back(row);
    }
}

void ResolveVoiceRowLocalState(HistoryRowView& row, const std::wstring& voiceDir)
{
    if (!row.m_isVoice)
    {
        return;
    }
    const balloonchatwnd::ChatMessage& v = row.m_voice;
    const balloonchatwnd::ChatMessage resolved = balloonchatwnd::BuildStoredVoiceMessage(
        v.msgid, v.fromMe, v.voiceFileId, v.voiceMd5, v.voiceDuration, v.voiceSize, std::string(), v.voicePlayed,
        voiceDir);
    //只取本机文件的结论，波形、播放状态等保留原值
    row.m_voice.voiceState = resolved.voiceState;
    row.m_voice.voiceLocalPath = resolved.voiceLocalPath;
}

} // namespace balloonhistory
