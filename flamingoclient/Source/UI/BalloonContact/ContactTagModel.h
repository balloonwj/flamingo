/**
 *  ContactTagModel.h
 *
 *  联系人标签的纯字符串运算与提交判定：归一（去首尾空白）、忽略大小写的查找
 *  与去重，以及「新增一个标签」「就地修改一枚标签」两次交互各自该如何处理的
 *  判定。
 *
 *  本文件里的函数全是纯函数 —— 不碰窗口、不碰网络、不读全局状态，便于
 *  FlamingoUnit 测试工程直接断言。真正的界面重建与发包（资料窗把整份标签列表
 *  回投给主面板，主面板再发 cmd_set_contact_tags）留在 CBalloonContactWindow
 *  与 CBalloonMainPanel。
 *
 *  判定函数的形态照同类的 BalloonMain/ContactRemarkModel.h 的
 *  DecideRemarkCommit：返回一个「动作枚举 + 归一后文本」的结果结构，调用方按
 *  动作分支执行。
 *
 *  典型用法（资料窗新增标签）：
 *      ballooncontact::TagAddDecision d =
 *          ballooncontact::DecideTagAdd(tags, input, true,
 *                                       ballooncontact::kContactTagMaxLen,
 *                                       ballooncontact::kContactTagMaxCount);
 *      if (d.m_action == ballooncontact::kTagAddAppend)
 *          tags.push_back(d.m_normalized);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include <vector>
#include <atlstr.h>   // CString

namespace ballooncontact {

// 单个标签的最大字符数。取 16 是被库结构卡死的：t_contact_tag 的 f_tag 列是
// varchar(16)（见 flamingoserver/table_mysql.sql），超了会被库悄悄截断，界面上就会
// 出现"改完显示的是全的、重登之后少一截"的怪事，故在提交前就拦住。
const int kContactTagMaxLen = 16;

// 单个联系人最多能贴多少个标签。10 个是产品定的上限：标签区最多占三行，
// 三行按 380 像素宽、每枚胶囊平均 70 像素估算正好容得下十来枚，再多就要滚动，
// 而标签本身是"一眼扫过去"的辅助信息，不该逼用户滚着找。
const int kContactTagMaxCount = 10;

/**
 *  新增一个标签的判定结果动作。
 */
enum TagAddAction
{
    kTagAddAppend    = 0,   // 归一后非空、未超长、未满、与已有标签都不重复 → 追加到列表末尾并提交
    kTagAddCancel    = 1,   // 用户什么都没输入（或只输了空白字符）→ 取消，不追加也不提交、不提示
    kTagAddTooLong   = 2,   // 归一后超过单个标签字符上限 → 拦截，提示后留给用户改
    kTagAddFull      = 3,   // 标签数已达上限 → 拦截，提示"标签最多 N 个"
    kTagAddDuplicate = 4,   // 与已有的某个标签重复（去首尾空白、忽略大小写后相同）→ 不追加，提示"该标签已存在"
    kTagAddOffline   = 5,   // 确实要追加、但当前不可发起联网操作 → 拦截并提示网络不可用
};

/**
 *  新增一个标签的判定结果。
 */
struct TagAddDecision
{
    TagAddAction m_action;      // 本次该做什么（见 TagAddAction）
    CString      m_normalized;  // 去首尾空白后的待追加文本（m_action == kTagAddAppend 时即要追加的内容）
    int          m_dupIndex;    // 重复时命中的那个已有标签的下标；其余情况恒为 -1

    TagAddDecision()
        : m_action(kTagAddCancel)
        , m_dupIndex(-1)
    {
    }
};

/**
 *  就地修改一枚标签的判定结果动作。
 */
enum TagEditAction
{
    kTagEditUpdate     = 0,   // 归一后是个新值、且与其它标签都不重复 → 用新文本替换该下标的标签并提交
    kTagEditNoChange   = 1,   // 归一后与原值等价（忽略大小写）→ 不改也不提交
    kTagEditRemove     = 2,   // 用户把内容全删光了 → 删除该下标的标签并提交
    kTagEditMerge      = 3,   // 归一后与另一枚标签重复 → 两枚合并成一枚（见 m_mergeIndex 的说明）并提交
    kTagEditTooLong    = 4,   // 归一后超过单个标签字符上限 → 拦截，提示后留给用户改
    kTagEditBadIndex   = 5,   // 下标越界（控件与列表不同步等异常）→ 什么都不做
    kTagEditOffline    = 6,   // 确实要改、但当前不可发起联网操作 → 拦截并提示网络不可用
};

/**
 *  就地修改一枚标签的判定结果。
 */
struct TagEditDecision
{
    TagEditAction m_action;      // 本次该做什么（见 TagEditAction）
    CString       m_normalized;  // 去首尾空白后的新文本（m_action == kTagEditUpdate / kTagEditMerge 时即要写入的内容）
    int           m_mergeIndex;  // 合并时"留下来的那一枚"在<u>入参列表</u>里的下标；其余情况恒为 -1。
                                 // 调用方的执行顺序是：先把该下标的文本置为 m_normalized，再删掉被编辑的那一枚 ——
                                 // 这样留下来的那一枚位置不变，同时保留用户这次输入的大小写

    TagEditDecision()
        : m_action(kTagEditNoChange)
        , m_mergeIndex(-1)
    {
    }
};

/**
 *  把一个标签文本归一：去掉首尾的空白字符（空格、制表符、换行等）。纯函数。
 *
 *  只处理首尾 —— 标签中间的空格是有意义的，「产品 设计」与「产品设计」是两个
 *  不同的标签，不能合并。
 *
 *    tag：原始标签文本；nullptr 视为空串（不崩溃）。
 *  返回：归一后的文本，可能为空串。
 */
CString NormalizeTag(LPCTSTR tag);

/**
 *  在标签列表里按"忽略大小写"查找一个标签。纯函数。
 *
 *  比较前两侧都先归一，故列表里带首尾空白的历史脏数据也能被正确命中。空串永远
 *  查不到 —— 空标签本来就不该存在于列表里，让它命中只会把判定引向错误分支。
 *
 *    tags：标签列表。
 *    tag：要查找的标签文本。
 *  返回：命中的下标；未命中返回 -1。
 */
int FindTagIndexIgnoreCase(const std::vector<CString>& tags, const CString& tag);

/**
 *  把一份标签列表整体归一：逐项去首尾空白、丢弃归一后为空的项、并按"忽略
 *  大小写"去重（重复项只保留<u>首次出现</u>的那一条，连同它的原始大小写）。
 *  纯函数。
 *
 *  本函数<u>不</u>限制条数 —— 条数是否超过 kContactTagMaxCount 由调用方判定，
 *  因为超限时该拦截还是该截断属于业务决策，不该藏在归一函数里。
 *
 *    tags：原始列表，顺序即显示顺序。
 *    outTags：出参，归一后的列表；函数内先清空。相对顺序与入参一致。
 */
void NormalizeTagList(const std::vector<CString>& tags,
                      std::vector<CString>& outTags);

/**
 *  判定一次"新增标签"该如何处理。纯函数、无副作用、不碰窗口/网络。
 *
 *  判定顺序是：先把所有与网络无关的判定做完（没输入 / 超长 / 已满 / 重复），
 *  最后才检查连接态。这么排有两个理由：一是用户什么都没输入就关掉输入框时，
 *  即便当前离线也不该弹"网络不可用"来烦他（与 DecideRemarkCommit 把"无变化"
 *  排在离线之前同一口径）；二是离线时输了个超长标签，先告诉他"太长了"比先
 *  告诉他"没网"更有用 —— 网络恢复后那个标签一样发不出去。
 *
 *    tags：当前已有的标签列表（未归一亦可，函数内部比较时会归一）。
 *    input：输入框里的原始文本（未 trim）。
 *    online：当前是否可以发起联网操作（由调用方从连接态换算，本函数不查网络）。
 *    maxLen：单个标签的字符上限（一般传 kContactTagMaxLen）。
 *    maxCount：标签条数上限（一般传 kContactTagMaxCount）。
 *  返回：判定结果（动作 + 归一后文本 + 重复时命中的下标）。
 */
TagAddDecision DecideTagAdd(const std::vector<CString>& tags,
                            const CString& input,
                            bool online,
                            int maxLen,
                            int maxCount);

/**
 *  判定一次"就地修改某枚标签"该如何处理。纯函数、无副作用、不碰窗口/网络。
 *
 *  判定顺序与 DecideTagAdd 同一口径：下标合法性 → 有没有真的改动 → 超长 →
 *  连接态 → 最终动作（删除 / 合并 / 更新）。把"没改动"排在离线之前的理由与
 *  DecideRemarkCommit 一致：用户点进编辑框又点出来、一个字没动时不该弹网络提示。
 *
 *  三种最终动作的区别：
 *    · 内容被全删光 → kTagEditRemove，该枚标签整个删掉；
 *    · 新内容与列表里另一枚标签等价 → kTagEditMerge，两枚并成一枚；
 *    · 其余 → kTagEditUpdate，就地替换文本。
 *
 *    tags：当前已有的标签列表。
 *    index：被修改的那一枚在 tags 里的下标；越界返回 kTagEditBadIndex。
 *    input：输入框里的原始文本（未 trim）。
 *    online：当前是否可以发起联网操作（由调用方从连接态换算，本函数不查网络）。
 *    maxLen：单个标签的字符上限（一般传 kContactTagMaxLen）。
 *  返回：判定结果（动作 + 归一后文本 + 合并时留下来那一枚的下标）。
 */
TagEditDecision DecideTagEdit(const std::vector<CString>& tags,
                              int index,
                              const CString& input,
                              bool online,
                              int maxLen);

} // namespace ballooncontact
