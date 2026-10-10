/**
 *  合并转发的聊天记录在查看窗里的各行数据，MergedRecordRows.h
 *
 *  查看窗（CMergedRecordWindow）的列表直接复用消息记录窗的行控件（BuildListContent），
 *  本文件负责把一份合并记录（net::MergedRecord）转成那套控件要的 HistoryRowView：名字、
 *  时间、正文、文件卡片、图片尺寸与日期分组头。与界面无关的部分都在这里，单元测试见
 *  Tests/MergedRecordRowsTests.cpp。
 *
 *  图片缩略图的本地路径与显示状态要查登录账号的缓存目录，不在本文件里填，由查看窗按行
 *  补上；行与子消息一一对应（日期分组头记在行的 m_groupLabel 上，不单独占行），所以第
 *  i 行就是 record.m_messages[i]。
 *
 *  典型用法：
 *      std::vector<balloonhistory::HistoryRowView> rows;
 *      balloonhistory::BuildMergedRecordRows(record, fallbackNames, todayKey, rows);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include "HistoryWindowLayout.h"   // HistoryRowView

namespace net {
struct MergedRecord;
}

namespace balloonhistory {

/**
 *  决定一条子消息显示的发送者名字。
 *  转发当时记下的名字（fromname）优先：它反映的是记录产生时的身份，而且接收方未必认识
 *  原发送者、本地查不到名字。没有时查调用方备好的回退名字，再没有写"用户" + userid。
 *  @param fromNameUtf8  子消息的 fromname（UTF-8），可为空串
 *  @param fromid        子消息发送者 userid
 *  @param fallbackNames 回退名字表（userid → 名字），通常由查看窗经主面板解析好；可为空表
 *  @return 显示用的名字，不会为空串
 */
CString ResolveMergedSenderName(const std::string& fromNameUtf8, int64_t fromid,
                                const std::map<int64_t, CString>& fallbackNames);

/**
 *  取一条嵌套记录（子消息本身是合并记录）在查看窗列表里显示的文字：「[聊天记录] 标题」，
 *  标题取不到时只写「[聊天记录]」。
 *  @param mergedContentUtf8 嵌套记录的 content 原文（UTF-8），即子消息的 m_mergedContent
 *  @return 显示文字
 */
CString MergedNestedRowText(const std::string& mergedContentUtf8);

/**
 *  把一份合并记录转成查看窗列表的各行，顺序与子消息相同（服务端按原消息 msgid 升序打包，
 *  即时间先后）。
 *
 *  每行填写的字段：名字（见 ResolveMergedSenderName）、头像底色、"HH:MM" 时间、正文
 *  （文本保留内联表情标记）、原消息 msgid；文件行另填类型色块、文件名与大小，正文写成
 *  "名字 · 大小"；图片行置 m_hasImageThumb 并填原图宽高；嵌套记录置 m_isMergedCard 并
 *  填标题与条数，正文见 MergedNestedRowText。日期与上一行不同的行（以及第一行）带日期
 *  分组头。不填 Pin 状态，查看窗是只读的。
 *
 *  @param record        已解析的合并记录（net::MessageProtocolClient::parseMergedContent 的产物）
 *  @param fallbackNames 子消息没带 fromname 时按 userid 查的名字表；可为空表
 *  @param todayKey      "今天"的日期键（LocalDayKey 的取值形式），决定分组头写「今天」「昨天」还是日期
 *  @param out           [出参] 先清空再写入，与 record.m_messages 等长
 */
void BuildMergedRecordRows(const net::MergedRecord& record,
                           const std::map<int64_t, CString>& fallbackNames,
                           int64_t todayKey,
                           std::vector<HistoryRowView>& out);

/**
 *  把一条文件子消息构造成查看窗里那张聊天文件卡片的数据。卡片画在左侧、没有接收 / 拒绝
 *  流程（fileDirectDownload 为 true），下载所需的 msgid、fileid、md5、大小都带上。
 *  @param sub       文件子消息（msgtype 为文件）
 *  @param localPath 本地已有的完整文件路径；空串表示本地没有。由调用方按登记表与下载缓存查好
 *  @return 本地有文件时为「已完成」（带本地路径），否则为「未下载」
 */
/**
 *  按当前账号的语音缓存目录补上一个语音行的本机文件状态（语音消息第 4c 步）：本机有大小相符的文件时为就绪并带上
 *  路径，没有时标为需要下载。非语音行不改。BuildMergedRecordRows 不知道缓存目录，由查看窗逐行补。
 *  @param row      [入参 / 出参] 一行
 *  @param voiceDir 当前账号的 ChatVoice 目录（以反斜杠结尾）；空串时一律按本机没有处理
 */
void ResolveVoiceRowLocalState(HistoryRowView& row, const std::wstring& voiceDir);

balloonchatwnd::ChatMessage BuildMergedFileCard(const net::MessageNotify& sub,
                                                const std::wstring& localPath);

/**
 *  查看窗里文件下载的状态表：只记本窗口发起过下载的那些文件（按子消息 msgid），重建列表
 *  时用 Apply 把记下的状态套到卡片上。
 *
 *  状态切换：Start → 下载中（进度 0）；Progress → 更新进度（只对下载中的有效）；Finish →
 *  已完成（带本地路径）/ 传输失败（进度停在中断处）/ 已取消。失败与取消之后可以再次 Start。
 */
class MergedFileStateTable
{
public:
    /**
     *  记下一笔刚发起的下载：置为下载中、进度 0。已有记录时覆盖。
     *  @param msgid 文件子消息的 msgid
     */
    void Start(int64_t msgid);

    /**
     *  更新下载进度。只对下载中的记录有效，没有记录或不在下载中时什么都不做。
     *  @param msgid   文件子消息的 msgid
     *  @param percent 进度百分比，超出 0～100 时按边界处理
     *  @return 确实更新了一条记录时返回 true
     */
    bool Progress(int64_t msgid, int percent);

    /**
     *  记下下载结果。没有记录时什么都不做（不是本窗口发起的下载）。
     *  @param msgid     文件子消息的 msgid
     *  @param ok        是否成功
     *  @param localPath 成功时文件落地的本地完整路径；失败时忽略
     *  @param cancelled 失败是否因为用户取消（ok 为 false 时才有意义）
     *  @return 确实更新了一条记录时返回 true
     */
    bool Finish(int64_t msgid, bool ok, const std::wstring& localPath, bool cancelled);

    /**
     *  这条文件是否正由本窗口下载中。
     *  @param msgid 文件子消息的 msgid
     *  @return 有记录且处于下载中时返回 true
     */
    bool IsDownloading(int64_t msgid) const;

    /**
     *  把记下的状态套到卡片上（按 card.fileMsgid 找记录）：状态、进度，已完成时还有本地路径。
     *  没有记录时卡片不变。
     *  @param card [出入参] 卡片数据
     */
    void Apply(balloonchatwnd::ChatMessage& card) const;

    //清空全部记录（查看窗换了一份记录时调用）。
    void Clear();

private:
    //一条文件的下载状态
    struct Entry
    {
        int          m_state;       //文件态，取值见 balloonchatwnd::ChatFileState
        int          m_progress;    //进度百分比 0～100
        std::wstring m_localPath;   //已完成时的本地完整路径；其余状态为空

        Entry()
            : m_state(0)
            , m_progress(0)
        {
        }
    };

    std::map<int64_t, Entry> m_entries;   //文件子消息 msgid → 下载状态
};

} // namespace balloonhistory
