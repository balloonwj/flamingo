/**
 *  HistoryRowMenuTests.cpp
 *
 *  「消息记录」窗口列表行右键菜单模型（BalloonHistory/HistoryRowMenu.h）测试，Suite HRM。
 *
 *  这个菜单的全部难点在于"该出现哪几项"随三件事变化：本行是什么类型的消息、它的
 *  本地文件在不在、是不是正在下载。窗口侧只负责把模型灌进 DuiMenu，所以只要把模型
 *  钉死，菜单就不会在某个组合下给出点了必然出错的项。本文件逐条覆盖
 *  docs/消息记录内容交互方案.md §8 测试用例清单第 9~15 条，外加两条不变式：
 *    · 分隔线不连续、不落在首尾（对所有类型 × 本地有无 × 下载中的组合循环验证）；
 *    · msgid 未分配时「Pin」整项不出现，而不是置灰。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不建窗口、不查库、不碰文件系统。
 *  全局 _Module 由其它测试文件统一定义，本文件只是随 stdafx.h 引用其声明。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonHistory/HistoryRowMenu.h"
#include "../net/Protocol.h"   // ::message_type_*

#include <algorithm>
#include <string>
#include <vector>

using namespace balloonhistory;

namespace {

//分隔线在 IdsOf 结果里的记号。菜单项枚举值从 1 起，0 不会与任何一项撞上。
const int kSeparatorMark = 0;

//已分配 msgid 的样例值。取多少不重要，只要大于 kHistoryRowMsgIdUnassigned，
//这里用当天日期便于在失败输出里一眼认出是本文件造的数据。
const int64_t kAssignedMsgId = 20260805;

const bool kHasLocal = true;    //本行的图片 / 文件在本地存在
const bool kNoLocal  = false;   //本地没有该文件，需要下载

const bool kDownloading    = true;    //本行正在下载
const bool kNotDownloading = false;   //本行没有在下载

const bool kPinned    = true;    //本条已被钉住，那一项文案取「取消 Pin」
const bool kNotPinned = false;   //本条没被钉住，文案取「Pin」

//协议里 message_type 的取值区间，供不变式用例遍历全部类型。
const int kMsgTypeFirst = (int)::message_type_text;     //枚举首项 = 1
const int kMsgTypeLast  = (int)::message_type_system;   //枚举末项 = 17

//按各字段拼一个入参，省得每个用例都写五行赋值。
HistoryRowMenuContext MakeCtx(int msgType, bool hasLocalFile, bool downloading,
                              bool pinned, int64_t msgId)
{
    HistoryRowMenuContext ctx;
    ctx.m_msgType      = msgType;
    ctx.m_hasLocalFile = hasLocalFile;
    ctx.m_downloading  = downloading;
    ctx.m_pinned       = pinned;
    ctx.m_msgId        = msgId;
    return ctx;
}

//把行模型压成 id 序列，方便整体比对顺序；分隔线记为 kSeparatorMark。
std::vector<int> IdsOf(const std::vector<HistoryRowMenuItem>& model)
{
    std::vector<int> ids;
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        ids.push_back(model[i].m_isSeparator ? kSeparatorMark : (int)model[i].m_id);
    }
    return ids;
}

//行模型里有没有某一项。
bool HasItem(const std::vector<HistoryRowMenuItem>& model, HistoryRowMenuId id)
{
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return true;
        }
    }
    return false;
}

//取某个菜单项的显示文案；模型里没有该项时返回空串。
std::wstring LabelOf(const std::vector<HistoryRowMenuItem>& model, HistoryRowMenuId id)
{
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return model[i].m_label;
        }
    }
    return std::wstring();
}

//分隔线不变式：模型非空、首尾都不是分隔线、也没有连着两条分隔线。
//这三条一旦破了，弹出来的菜单会出现打头一条横线、结尾一条横线或中间两条挨着的
//横线 —— 都是肉眼一看就不对、但很容易在"某组整组消失"时悄悄冒出来的问题。
bool SeparatorInvariantHolds(const std::vector<HistoryRowMenuItem>& model)
{
    if (model.empty())
    {
        return false;
    }
    if (model.front().m_isSeparator)
    {
        return false;
    }
    if (model.back().m_isSeparator)
    {
        return false;
    }
    for (std::size_t i = 1; i < model.size(); ++i)
    {
        if (model[i].m_isSeparator && model[i - 1].m_isSeparator)
        {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

TEST_CASE("HRM1 文本消息：复制 / 定位 / Pin", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_text, kNoLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));

    const std::vector<int> expect = {
        kHistoryRowMenuCopy, kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(model) == expect);

    //文本行不该出现任何需要本地文件的项
    CHECK_FALSE(HasItem(model, kHistoryRowMenuDownload));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFolder));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuCopyImage));
}

TEST_CASE("HRM2 图片消息 + 本地有文件：复制图片 / 图片另存为 / 打开所在文件夹", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_image, kHasLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));

    const std::vector<int> expect = {
        kHistoryRowMenuCopyImage, kHistoryRowMenuSaveImageAs, kHistoryRowMenuOpenFolder,
        kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(model) == expect);

    //本地已有原图，就不该再给「下载」；文件行专有的三项也不该混进来
    CHECK_FALSE(HasItem(model, kHistoryRowMenuDownload));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFile));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuSaveFileAs));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuCopy));
}

TEST_CASE("HRM3 图片消息 + 本地无文件：只给下载", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_image, kNoLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));

    const std::vector<int> expect = {
        kHistoryRowMenuDownload, kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(model) == expect);

    //原图还没拉回来，三个要本地文件的项一个都不该出现
    CHECK_FALSE(HasItem(model, kHistoryRowMenuCopyImage));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuSaveImageAs));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFolder));
}

TEST_CASE("HRM4 文件消息 + 本地有文件：打开文件 / 打开所在文件夹 / 另存为", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_file, kHasLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));

    const std::vector<int> expect = {
        kHistoryRowMenuOpenFile, kHistoryRowMenuOpenFolder, kHistoryRowMenuSaveFileAs,
        kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(model) == expect);

    CHECK_FALSE(HasItem(model, kHistoryRowMenuDownload));
    //图片行专有的两项不该混进文件行
    CHECK_FALSE(HasItem(model, kHistoryRowMenuCopyImage));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuSaveImageAs));
}

TEST_CASE("HRM5 文件消息 + 本地无文件：只给下载", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_file, kNoLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));

    const std::vector<int> expect = {
        kHistoryRowMenuDownload, kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(model) == expect);

    CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFile));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFolder));
    CHECK_FALSE(HasItem(model, kHistoryRowMenuSaveFileAs));
}

TEST_CASE("HRM6 正在下载时只给取消下载，且优先于本地文件状态", "[historymenu]")
{
    //下载中是最高优先级：本地有没有文件都不看，「下载」「打开」一律不给 ——
    //这时文件还没落地，点了必然出错。与用户确认的口径是任意类型都照此处理。
    const int types[] = { ::message_type_image, ::message_type_file,
                          ::message_type_screenshot, ::message_type_text,
                          ::message_type_voice };
    const bool locals[] = { kHasLocal, kNoLocal };

    for (std::size_t t = 0; t < sizeof(types) / sizeof(types[0]); ++t)
    {
        for (std::size_t l = 0; l < sizeof(locals) / sizeof(locals[0]); ++l)
        {
            INFO("msgType=" << types[t] << " hasLocal=" << (locals[l] ? 1 : 0));

            const std::vector<HistoryRowMenuItem> model =
                BuildHistoryRowMenuModel(MakeCtx(types[t], locals[l], kDownloading,
                                                 kNotPinned, kAssignedMsgId));

            const std::vector<int> expect = {
                kHistoryRowMenuCancelDownload, kSeparatorMark,
                kHistoryRowMenuLocate, kHistoryRowMenuPin
            };
            CHECK(IdsOf(model) == expect);

            CHECK_FALSE(HasItem(model, kHistoryRowMenuDownload));
            CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFile));
            CHECK_FALSE(HasItem(model, kHistoryRowMenuOpenFolder));
        }
    }
}

TEST_CASE("HRM7 语音及其它未单独支持的类型：只有定位与 Pin", "[historymenu]")
{
    //这些类型在消息记录窗里还只是一行占位文字，没有可以"拿内容做点什么"的操作。
    //宁可少给项，也不给一个点下去没反应的项。
    const int types[] = { ::message_type_voice, ::message_type_video,
                          ::message_type_emoji, ::message_type_location,
                          ::message_type_card, ::message_type_shake,
                          ::message_type_merged, ::message_type_rtc,
                          ::message_type_system };

    for (std::size_t t = 0; t < sizeof(types) / sizeof(types[0]); ++t)
    {
        INFO("msgType=" << types[t]);

        //本地文件标志给 true 也不该多出任何项 —— 这些类型压根不走文件那条路
        const std::vector<HistoryRowMenuItem> model =
            BuildHistoryRowMenuModel(MakeCtx(types[t], kHasLocal, kNotDownloading,
                                             kNotPinned, kAssignedMsgId));

        const std::vector<int> expect = {
            kHistoryRowMenuLocate, kHistoryRowMenuPin
        };
        CHECK(IdsOf(model) == expect);
    }
}

TEST_CASE("HRM8 msgid 未分配时 Pin 整项不出现", "[historymenu]")
{
    //服务器还没给这条消息分配 msgid（本端刚发出、回执还没回填）：Pin 发过去必被判
    //message_not_exist，故整项不显示 —— 与气泡菜单「撤回」超时即整项消失同一种处理。
    const std::vector<HistoryRowMenuItem> textModel =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_text, kNoLocal, kNotDownloading,
                                         kNotPinned, kHistoryRowMsgIdUnassigned));
    CHECK_FALSE(HasItem(textModel, kHistoryRowMenuPin));
    //其余各项不受影响
    CHECK(HasItem(textModel, kHistoryRowMenuCopy));
    CHECK(HasItem(textModel, kHistoryRowMenuLocate));

    //连内容组也为空的类型：msgid 未分配后菜单只剩「定位到聊天窗」一项，
    //且不该因此留下一条孤零零的分隔线。
    const std::vector<HistoryRowMenuItem> voiceModel =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_voice, kNoLocal, kNotDownloading,
                                         kNotPinned, kHistoryRowMsgIdUnassigned));
    const std::vector<int> expect = { kHistoryRowMenuLocate };
    CHECK(IdsOf(voiceModel) == expect);
    CHECK(SeparatorInvariantHolds(voiceModel));

    //负数不是合法 msgid，一并按未分配处理
    const std::vector<HistoryRowMenuItem> negativeModel =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_text, kNoLocal, kNotDownloading,
                                         kNotPinned, -1));
    CHECK_FALSE(HasItem(negativeModel, kHistoryRowMenuPin));
}

TEST_CASE("HRM9 Pin 项文案随当前是否已钉住取反", "[historymenu]")
{
    const std::vector<HistoryRowMenuItem> off =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_text, kNoLocal, kNotDownloading,
                                         kNotPinned, kAssignedMsgId));
    const std::vector<HistoryRowMenuItem> on =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_text, kNoLocal, kNotDownloading,
                                         kPinned, kAssignedMsgId));

    CHECK(LabelOf(off, kHistoryRowMenuPin) == L"Pin");
    CHECK(LabelOf(on, kHistoryRowMenuPin) == L"取消 Pin");

    //已钉住时那一项照样出现在图片行、文件行上，且文案一致 ——
    //「取消 Pin」的权限不在客户端预判，一律给出，无权时由服务端拒绝并提示。
    const std::vector<HistoryRowMenuItem> imageOn =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_image, kHasLocal, kNotDownloading,
                                         kPinned, kAssignedMsgId));
    CHECK(LabelOf(imageOn, kHistoryRowMenuPin) == L"取消 Pin");
}

TEST_CASE("HRM10 分隔线不连续、不落在首尾（全组合）", "[historymenu]")
{
    //内容组会因消息类型整组消失，"要不要那条分隔线"就成了每个组合都得算对的事。
    //这里把所有类型 × 本地有无 × 下载中 × 已钉住 × msgid 有无跑一遍，
    //一共 (17 × 2 × 2 × 2 × 2) 个组合。
    const bool flags[] = { false, true };
    const int64_t msgIds[] = { kHistoryRowMsgIdUnassigned, kAssignedMsgId };

    for (int msgType = kMsgTypeFirst; msgType <= kMsgTypeLast; ++msgType)
    {
        for (std::size_t l = 0; l < sizeof(flags) / sizeof(flags[0]); ++l)
        {
            for (std::size_t d = 0; d < sizeof(flags) / sizeof(flags[0]); ++d)
            {
                for (std::size_t p = 0; p < sizeof(flags) / sizeof(flags[0]); ++p)
                {
                    for (std::size_t m = 0; m < sizeof(msgIds) / sizeof(msgIds[0]); ++m)
                    {
                        INFO("msgType=" << msgType
                             << " hasLocal=" << (flags[l] ? 1 : 0)
                             << " downloading=" << (flags[d] ? 1 : 0)
                             << " pinned=" << (flags[p] ? 1 : 0)
                             << " msgId=" << (long long)msgIds[m]);

                        const std::vector<HistoryRowMenuItem> model =
                            BuildHistoryRowMenuModel(MakeCtx(msgType, flags[l], flags[d],
                                                             flags[p], msgIds[m]));

                        CHECK(SeparatorInvariantHolds(model));
                        //任何组合下「定位到聊天窗」都在 —— 它是本窗口的主操作，
                        //菜单不该有"一项都没有"的时候。
                        CHECK(HasItem(model, kHistoryRowMenuLocate));
                        //分隔线行的文案必须是空串，免得窗口侧误把它当普通项画出来
                        for (std::size_t i = 0; i < model.size(); ++i)
                        {
                            if (model[i].m_isSeparator)
                            {
                                CHECK(model[i].m_label.empty());
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("HRM11 截图按图片行处理", "[historymenu]")
{
    //截图在协议里是图片的子类型（::message_type_screenshot），消息记录窗一直按图片
    //渲染缩略图。菜单要跟着按图片给项，否则截图行会出现"有缩略图却没有图片菜单"的割裂。
    CHECK(IsHistoryRowImageType(::message_type_screenshot));
    CHECK(IsHistoryRowImageType(::message_type_image));
    CHECK_FALSE(IsHistoryRowImageType(::message_type_file));
    CHECK_FALSE(IsHistoryRowImageType(::message_type_text));

    CHECK(IsHistoryRowFileType(::message_type_file));
    CHECK_FALSE(IsHistoryRowFileType(::message_type_image));

    const std::vector<HistoryRowMenuItem> withFile =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_screenshot, kHasLocal,
                                         kNotDownloading, kNotPinned, kAssignedMsgId));
    const std::vector<int> expectWithFile = {
        kHistoryRowMenuCopyImage, kHistoryRowMenuSaveImageAs, kHistoryRowMenuOpenFolder,
        kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(withFile) == expectWithFile);

    const std::vector<HistoryRowMenuItem> withoutFile =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_screenshot, kNoLocal,
                                         kNotDownloading, kNotPinned, kAssignedMsgId));
    const std::vector<int> expectWithoutFile = {
        kHistoryRowMenuDownload, kSeparatorMark,
        kHistoryRowMenuLocate, kHistoryRowMenuPin
    };
    CHECK(IdsOf(withoutFile) == expectWithoutFile);
}

TEST_CASE("HRM12 同一份菜单里各项 id 互不相同", "[historymenu]")
{
    //分发时按 id switch，撞号会让两个功能串线，而这种错在界面上看不出来。
    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(MakeCtx(::message_type_file, kHasLocal, kNotDownloading,
                                         kPinned, kAssignedMsgId));

    std::vector<int> ids;
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator)
        {
            ids.push_back((int)model[i].m_id);
        }
    }
    const std::size_t before = ids.size();
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    CHECK(ids.size() == before);
    //这个组合下五项应当全在：打开文件 / 打开所在文件夹 / 另存为 / 定位 / 取消 Pin
    CHECK(before == 5u);
}
