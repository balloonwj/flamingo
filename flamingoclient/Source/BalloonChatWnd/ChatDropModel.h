/**
 *  ChatDropModel.h
 *
 *  「把文件拖进聊天窗」的落点归类（纯逻辑核心，可脱离窗口与磁盘单测）。
 *
 *  从资源管理器拖进来的是一串路径，里面什么都可能有：几个文件、一个文件夹、
 *  刚被删掉的残留项、0 字节的空文件。聊天窗要据此决定每一条各走哪条发送链路
 *  （图片消息还是文件消息），哪些发不了、发不了的原因是什么（好给用户一句提示），
 *  以及一次拖太多时要不要先弹确认。
 *
 *  分成三段，好让绝大部分逻辑不碰磁盘：
 *    · ProbeDropPath      —— 唯一碰磁盘的一步，把一条路径查成 DropPathInfo。
 *    · BuildDropPlan      —— 纯函数，拿一组 DropPathInfo 归类成 DropPlan。
 *    · BuildDropPlanFromPaths —— 前两者的串联，聊天窗实际调的就是它。
 *  单元测试主要打 BuildDropPlan（见 flamingoclient/Source/Tests/ChatDropModelTests.cpp，
 *  Suite CDM），不必在测试里真造文件、真建目录。
 *
 *  本期不支持发送文件夹（已与用户对齐）：文件夹一律归入跳过项，由聊天窗提示
 *  「暂不支持发送文件夹」。等后续做了打包能力再改这里的归类。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>
#include <vector>

namespace balloonchatwnd {

//一次拖放里可直接发送的文件数上限：多于这个数就先弹确认框列出清单，免得误把
//一整个目录拖进来、几十条消息瞬间刷屏。取 9 与消息区一屏能看到的条数相当。
const int kDropConfirmThreshold = 9;

/**
 *  一条可发送项该走哪条发送链路。
 */
enum DropSendKind
{
    kDropSendImage = 0,   // 图片消息（走 imgserver 上传 + msgtype=2），拖进来的是常见图片格式时取该值
    kDropSendFile  = 1    // 文件消息（走 fileserver 上传 + msgtype=3），非图片、或用户按住 Shift 强制按文件发时取该值
};

/**
 *  一条路径发不出去的原因。聊天窗据此给用户一句人话提示。
 */
enum DropSkipReason
{
    kDropSkipNotFound  = 0,   // 路径不存在：拖动过程中文件被移走 / 删除，或来源本就是虚拟项
    kDropSkipDirectory = 1,   // 是文件夹：本期不支持发送文件夹
    kDropSkipEmpty     = 2    // 0 字节文件：发送链路拒收空文件（见 CBalloonChatWindow::SendFileData）
};

/**
 *  一条路径查磁盘的结果。BuildDropPlan 的输入单元 —— 把"查磁盘"与"怎么归类"
 *  分开，归类那一半才能在测试里脱离真实文件系统。
 */
struct DropPathInfo
{
    std::wstring m_path;        // 原始路径（原样保留，提示与发送都用它）
    bool         m_exists;      // 该路径当前是否存在
    bool         m_isDirectory; // 是否是目录；m_exists 为 false 时无意义
    long long    m_sizeBytes;   // 文件字节数；目录或不存在时为 0

    DropPathInfo()
        : m_exists(false)
        , m_isDirectory(false)
        , m_sizeBytes(0)
    {
    }
};

/**
 *  一条可发送项。
 */
struct DropSendItem
{
    std::wstring m_path;   // 本地文件绝对路径
    int          m_kind;   // 走哪条发送链路，取值见 DropSendKind

    DropSendItem()
        : m_kind(kDropSendFile)
    {
    }
};

/**
 *  一条发不出去的项。
 */
struct DropSkippedItem
{
    std::wstring m_path;     // 本地路径
    int          m_reason;   // 发不出去的原因，取值见 DropSkipReason

    DropSkippedItem()
        : m_reason(kDropSkipNotFound)
    {
    }
};

/**
 *  一次拖放的处理计划。
 */
struct DropPlan
{
    //可发送项，<u>按拖入顺序</u>排列。顺序要保住 —— 用户一次拖三个文件进来，
    //期望消息也按那个顺序出现。
    std::vector<DropSendItem>    m_items;

    //发不出去的项及原因，同样按拖入顺序。聊天窗据它提示用户。
    std::vector<DropSkippedItem> m_skipped;

    //可发送项数量是否超过 kDropConfirmThreshold —— 超过则聊天窗先弹确认框。
    bool                         m_needConfirm;

    DropPlan()
        : m_needConfirm(false)
    {
    }
};

/**
 *  按扩展名判断一条路径是不是常见图片。纯函数、不碰磁盘、扩展名大小写不敏感。
 *
 *  口径与聊天窗「图片」按钮的选择对话框过滤器保持一致（png / jpg / jpeg / bmp / gif），
 *  两处不能漂移 —— 否则同一个文件"选出来"能当图片发、"拖进来"却当文件发。
 *    path：文件路径或文件名。
 *  @return 是图片返回 true。
 */
bool IsImageDropPath(const std::wstring& path);

/**
 *  查一条路径的磁盘状态。<u>本函数是本文件里唯一碰磁盘的一步</u>。
 *    path：待查路径；空串直接返回"不存在"。
 *  @return 查询结果；路径查不到时 m_exists 为 false，其余字段为默认值。
 */
DropPathInfo ProbeDropPath(const std::wstring& path);

/**
 *  把一组已查好的路径归类成处理计划。纯函数、无副作用、不碰磁盘。
 *
 *  归类规则：不存在 / 是目录 / 0 字节的进跳过项；其余按扩展名分到图片链路或文件
 *  链路；forceAsFile 为真时图片也走文件链路（用户按住 Shift 拖入，想把图片当原
 *  文件发而不是压成聊天图片）。
 *
 *    infos：       各路径的磁盘状态，顺序即用户拖入顺序。
 *    forceAsFile： true 表示图片也按普通文件发送。
 *  @return 处理计划；输入为空时返回空计划（m_needConfirm 为 false）。
 */
DropPlan BuildDropPlan(const std::vector<DropPathInfo>& infos, bool forceAsFile);

/**
 *  ProbeDropPath + BuildDropPlan 的串联，聊天窗接拖放回调时调的就是它。
 *    paths：       拖进来的路径列表，顺序即用户拖入顺序。
 *    forceAsFile： true 表示图片也按普通文件发送。
 *  @return 处理计划。
 */
DropPlan BuildDropPlanFromPaths(const std::vector<std::wstring>& paths, bool forceAsFile);

/**
 *  把跳过项汇总成一句可直接显示给用户的中文提示。纯函数、无副作用。
 *
 *  按原因分组计数而不是逐条罗列 —— 一次拖进来十几个文件夹时，逐条提示既占地方
 *  又没有额外信息量。同时存在多种原因时用顿号连接，如"2 个文件夹暂不支持发送、
 *  1 个文件已不存在"。
 *    skipped：跳过项列表。
 *  @return 提示文案；列表为空时返回空串（调用方据此决定要不要弹提示）。
 */
std::wstring DescribeDropSkipped(const std::vector<DropSkippedItem>& skipped);

} // namespace balloonchatwnd
