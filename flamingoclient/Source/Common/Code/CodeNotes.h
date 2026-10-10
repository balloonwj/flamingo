/**
 *  CodeNotes.h —— 代码片段的批注（docs/代码片段方案.md 第 10.5 节）：数据、整理校验、代码改动后的重新定位。
 *
 *  一条批注是「起止行 + 说明文字 + 作者」，行号从 1 起、包含两端。同一作者的两条批注的行范围不能相交，不同作者的可以
 *  （发出之后双方都能追加批注，见第 11 节）；每段代码最多 kCodeNotesMax 条，每条说明最多 kCodeNoteTextMaxChars 个字。
 *  发送代码窗口编辑批注、改代码之后用 RemapCodeNotes 让批注跟着行走；收到的批注先经 SanitizeCodeNotes 整理再显示；
 *  追加批注时用 MergeAnnotationRound 得出要发出的批注。全部是纯函数，FlamingoUnit 直接断言（[codenotes]）。
 *
 *  典型用法（发送代码窗口在代码改动后）：
 *      int removed = 0;
 *      m_notes = codesnippet::RemapCodeNotes(m_notes, m_notesText, newText, removed);
 *      m_notesText = newText;
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

namespace codesnippet {

const int kCodeNotesMax = 50;            //每段代码最多几条批注（用户 2026-10-06 确认）
const int kCodeNoteTextMaxChars = 200;   //每条说明最多几个字，按 UTF-16 字符计（用户 2026-10-06 确认）
const int kCodeNotesRemapMaxLines = 2000;   //重新定位时新旧行数之积超过它的平方（如两边都超过 2000 行）就不做按行比较

/**
 *  一条批注。
 */
struct CodeNote
{
    int          m_from;       //起始行，从 1 起
    int          m_to;         //结束行，从 1 起，包含这一行；不小于 m_from
    std::wstring m_text;       //说明文字
    long long    m_authorId;   //写这条批注的人的 userid；0 表示作者就是这条消息的发送者，或是本机正在编写、还没有发出的批注
                               //（协议 notes[].userid 缺省时，收到后由 CodeNotesFromData 补成消息发送者）
    bool         m_isNew;      //是否为这条消息的发送者在这一轮追加批注时新增或修改的批注（协议 notes[].new，第 11.4 节）

    CodeNote()
        : m_from(0)
        , m_to(0)
        , m_authorId(0)
        , m_isNew(false)
    {
    }

    CodeNote(int from, int to, const std::wstring& text, long long authorId = 0, bool isNew = false)
        : m_from(from)
        , m_to(to)
        , m_text(text)
        , m_authorId(authorId)
        , m_isNew(isNew)
    {
    }
};

/**
 *  两段行范围（从 1 起、含两端）是否相交。
 */
bool CodeNoteRangesOverlap(int from1, int to1, int from2, int to2);

/**
 *  整理批注：去掉起止行越界、颠倒或说明为空白的条目，说明超过 kCodeNoteTextMaxChars 时截断，按起始行、结束行、作者排序后
 *  去掉与同一作者的前一条相交的条目（不同作者的批注可以相交），最多留 kCodeNotesMax 条（按总数，不按作者）。
 *  @param notes     批注
 *  @param lineCount 代码总行数；不大于 0 时不检查是否越过末行
 *  @return 整理后的批注，按起始行升序
 */
std::vector<CodeNote> SanitizeCodeNotes(const std::vector<CodeNote>& notes, int lineCount);

/**
 *  代码改动之后重新定位批注。新旧文字按行做最长公共子序列比较（比较时忽略行首行尾空白，所以重新缩进不影响定位）：
 *  批注的首行在新文字里有对应行时跟着它走，没有时（这一行被改过）从前面最近一个对应行的下一行算起；末行同理。
 *  这样改了被批注那一行的内容，批注仍在；整段被删掉时批注随之删除。开头与末尾相同的行直接对应，只比较中间改动过的
 *  部分，所以每输入一次就调用一次也不费时；中间部分新旧行数之积超过 kCodeNotesRemapMaxLines 的平方时不做比较，
 *  按原行号保留，越过新末行的删除。
 *  @param notes   改动之前的批注
 *  @param oldText 改动之前的代码（换行可以是 CRLF、CR 或 LF）
 *  @param newText 改动之后的代码
 *  @param removed [出参] 因所在的行被删掉而删除的条数
 *  @return 重新定位并整理后的批注
 */
std::vector<CodeNote> RemapCodeNotes(const std::vector<CodeNote>& notes, const std::wstring& oldText,
                                     const std::wstring& newText, int& removed);

/**
 *  文字开头有几行空白行（只含空格与 Tab 的行也算）。发送时代码会去掉这些行，批注要随之上移。
 */
int LeadingBlankLines(const std::wstring& text);

/**
 *  把批注整体平移若干行（delta 为负时上移），移出第 1 行之前的部分截掉、整条都移出的删除。
 *  @param notes 批注
 *  @param delta 平移的行数
 *  @return 平移后的批注
 */
std::vector<CodeNote> ShiftCodeNotes(const std::vector<CodeNote>& notes, int delta);

/**
 *  发送时的批注：发送的代码去掉了开头与末尾只含空白的行（与 NormalizeCodeNewlines 相同），批注随之上移，
 *  越过新末行的部分截掉，整条落在被去掉的行里的删除，最后整理。
 *  @param notes   编辑框里的批注，行号指 rawText 的行（从 1 起）
 *  @param rawText 编辑框里的文字（换行可以是 CRLF、CR 或 LF）
 *  @return 行号指发送的代码的批注，已整理
 */
std::vector<CodeNote> CodeNotesForSending(const std::vector<CodeNote>& notes, const std::wstring& rawText);

/**
 *  第 line 行（从 1 起）落在哪条批注里。
 *  @return 批注的下标；不在任何批注里时返回 -1
 */
int FindCodeNoteAtLine(const std::vector<CodeNote>& notes, int line);

/**
 *  本人能否修改、删除这条批注：作者是本人，或作者为 0（本机正在编写、还没有发出的批注）。
 *  @param note   批注
 *  @param selfId 本人 userid
 */
bool CanEditCodeNote(const CodeNote& note, long long selfId);

/**
 *  追加批注时（docs/代码片段方案.md 第 11 节）要发出的批注：以原消息上的批注为底，别人的批注一律按原样保留（即使 edited 里
 *  改过或删掉了），本人的批注取 edited 里的结果。本人的批注里，起止行与说明都能在原消息本人的批注里找到相同一条的不算新，
 *  其余（新增的、改过的）标为「新」；别人的批注一律清掉「新」（上一轮的「新」只对上一条消息有意义）。
 *  @param original  原消息上的批注，作者已补全（m_authorId 不为 0）
 *  @param edited    窗口里编辑后的批注；其中作者为 0 的视为本人新写的
 *  @param selfId    本人 userid
 *  @param lineCount 代码总行数；不大于 0 时不检查是否越过末行
 *  @return 整理后的批注，按起始行升序；本人的批注作者都是 selfId
 */
std::vector<CodeNote> MergeAnnotationRound(const std::vector<CodeNote>& original, const std::vector<CodeNote>& edited,
                                           long long selfId, int lineCount);

/**
 *  标为「新」的批注有几条。追加批注时为 0 表示没有新增或修改，不能发送；代码卡片顶部据此写「其中 M 条是新的」。
 */
int CountNewCodeNotes(const std::vector<CodeNote>& notes);

/**
 *  为第 from～to 行添加批注时，要改为修改的那一条：与这几行相交、且本人能修改（CanEditCodeNote）的第一条。
 *  别人的批注不算，在别人批注的行上添加是新建一条（docs/代码片段方案.md 第 11.3 节）。
 *  @param notes  批注
 *  @param from   起始行（从 1 起）
 *  @param to     结束行（从 1 起，含）
 *  @param selfId 本人 userid；平常写代码时为 0（本机写的批注作者都是 0）
 *  @return 批注下标；没有时返回 -1，表示新建
 */
int FindEditableCodeNoteInRange(const std::vector<CodeNote>& notes, int from, int to, long long selfId);

/**
 *  把作者为 0 的批注的作者补成 authorId，其余不变。原消息里没写作者的批注，作者就是原消息的发送者。
 *  @param notes    批注
 *  @param authorId 补上的作者 userid
 *  @return 补过作者的批注，顺序不变
 */
std::vector<CodeNote> FillCodeNoteAuthors(const std::vector<CodeNote>& notes, long long authorId);

} // namespace codesnippet
