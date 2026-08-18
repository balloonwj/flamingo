/**
 *  ChatImageMenuTests.cpp
 *
 *  聊天气泡右键菜单模型（BalloonChatWnd/ChatMessageMenu.h）测试，Suite CIM。
 *
 *  本轮新增的是图片消息上的「图片另存为」「再次发送」两项。要紧的是它们的
 *  <u>出现条件</u>与<u>位置</u>：
 *    · 只在图片行、且本地文件已就绪时出现；上传中 / 下载中 / 失败一律不出现
 *      （与用户确认过：不显示，而不是置灰）；
 *    · 插在「复制」之后，不打乱其余各项的相对顺序；
 *    · 收到的图也给「再次发送」—— 只要本地有文件就能再发一遍。
 *  外加「另存为」默认文件名的推导：自己发的图沿用原文件名，收到的图落在缓存里
 *  文件名是 md5 且不带扩展名，得换成 IMG_<msgid>.png 才对用户有意义。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不建窗口也不碰文件系统。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"   // ChatMsgKind：核对模型里那份 kind 取值没漂

using namespace balloonchatwnd;

namespace {

//把行模型压成 id 序列，方便整体比对顺序；分隔线记为 0（枚举值从 1 起，不会撞上）。
std::vector<int> IdsOf(const std::vector<ChatMessageMenuEntry>& model)
{
    std::vector<int> ids;
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        ids.push_back(model[i].isSeparator ? 0 : (int)model[i].id);
    }
    return ids;
}

//行模型里有没有某一项。
bool HasItem(const std::vector<ChatMessageMenuEntry>& model, ChatMessageMenuId id)
{
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
        {
            return true;
        }
    }
    return false;
}

//某一项在行模型里的下标；没有返回 -1。
int IndexOf(const std::vector<ChatMessageMenuEntry>& model, ChatMessageMenuId id)
{
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
        {
            return (int)i;
        }
    }
    return -1;
}

const bool kHasPath = true;    //本地有图片文件
const bool kNoPath  = false;   //本地还没有图片文件
const bool kCanRecall    = true;    //自己发出、且在可撤回时限内
const bool kCannotRecall = false;   //对方发的、或已超时
const bool kCanPin    = true;    //服务器已给这条消息分配 msgid，可以 Pin
const bool kCannotPin = false;   //本端刚发出、回执还没回填，整项不出现
const bool kPinned    = true;    //本条当前已被钉住，那一项文案取「取消 Pin」
const bool kNotPinned = false;   //本条当前没被钉住，文案取「Pin」

} // anonymous namespace

TEST_CASE("CIM1 模型里的图片 kind 取值与 ChatMsgKind 一致")
{
    //ChatMessageMenu.h 为保持纯净没有引 ChatMockData.h（后者依赖 ATL 的 CString），
    //自己留了一份 kind 取值。这条用例是那份复制品的防漂移哨兵。
    CHECK(kChatMenuKindImage == (int)kMsgImage);
}

TEST_CASE("CIM2 文字消息不出现图片两项")
{
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath, kCannotRecall, kCannotPin, kNotPinned);

    CHECK_FALSE(HasItem(model, kChatMsgMenuSaveImageAs));
    CHECK_FALSE(HasItem(model, kChatMsgMenuResendImage));

    //文字消息的完整骨架：复制 / 引用回复 / 转发 / — / 删除 / — / 多选
    const std::vector<int> expect = {
        kChatMsgMenuCopy, kChatMsgMenuReply, kChatMsgMenuForward, 0,
        kChatMsgMenuDelete, 0, kChatMsgMenuMultiSelect
    };
    CHECK(IdsOf(model) == expect);
}

TEST_CASE("CIM3 图片就绪时两项紧跟在「复制」之后，其余顺序不变")
{
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgImage, kChatImageStateReady, kHasPath, kCannotRecall, kCannotPin, kNotPinned);

    const std::vector<int> expect = {
        kChatMsgMenuCopy, kChatMsgMenuSaveImageAs, kChatMsgMenuResendImage,
        kChatMsgMenuReply, kChatMsgMenuForward, 0,
        kChatMsgMenuDelete, 0, kChatMsgMenuMultiSelect
    };
    CHECK(IdsOf(model) == expect);

    //位置关系单独钉一遍：紧挨着「复制」，且在「引用回复」之前。
    CHECK(IndexOf(model, kChatMsgMenuSaveImageAs) == IndexOf(model, kChatMsgMenuCopy) + 1);
    CHECK(IndexOf(model, kChatMsgMenuResendImage) == IndexOf(model, kChatMsgMenuSaveImageAs) + 1);
    CHECK(IndexOf(model, kChatMsgMenuResendImage) < IndexOf(model, kChatMsgMenuReply));
}

TEST_CASE("CIM4 图片未就绪时两项不出现")
{
    //上传中 / 下载中 / 失败：这三种态下这张图要么还没传完、要么根本没有本地文件，
    //另存为与再次发送都无从谈起。按与用户确认的口径整项不显示，不置灰。
    const int notReady[] = { kChatImageStateUploading, kChatImageStateFailed,
                             kChatImageStateDownloading };
    for (std::size_t i = 0; i < sizeof(notReady) / sizeof(notReady[0]); ++i)
    {
        const std::vector<ChatMessageMenuEntry> model =
            BuildChatMessageMenuModel(kMsgImage, notReady[i], kHasPath, kCannotRecall, kCannotPin, kNotPinned);
        CHECK_FALSE(HasItem(model, kChatMsgMenuSaveImageAs));
        CHECK_FALSE(HasItem(model, kChatMsgMenuResendImage));
        CHECK_FALSE(ShouldShowImageActions(kMsgImage, notReady[i], kHasPath));
    }
}

TEST_CASE("CIM5 图片虽已就绪但没有本地路径时两项不出现")
{
    //收图的"下载完成填路径"与"状态置就绪"是两次赋值，中间那一瞬只信状态会误判。
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgImage, kChatImageStateReady, kNoPath, kCannotRecall, kCannotPin, kNotPinned);
    CHECK_FALSE(HasItem(model, kChatMsgMenuSaveImageAs));
    CHECK_FALSE(HasItem(model, kChatMsgMenuResendImage));
    CHECK_FALSE(ShouldShowImageActions(kMsgImage, kChatImageStateReady, kNoPath));
}

TEST_CASE("CIM6 文件与大表情消息不出现图片两项")
{
    const int otherKinds[] = { kMsgFile, kMsgEmoji };
    for (std::size_t i = 0; i < sizeof(otherKinds) / sizeof(otherKinds[0]); ++i)
    {
        const std::vector<ChatMessageMenuEntry> model =
            BuildChatMessageMenuModel(otherKinds[i], kChatImageStateReady, kHasPath, kCannotRecall, kCannotPin, kNotPinned);
        CHECK_FALSE(HasItem(model, kChatMsgMenuSaveImageAs));
        CHECK_FALSE(HasItem(model, kChatMsgMenuResendImage));
    }
}

TEST_CASE("CIM7 自己发的图在撤回时限内：撤回与图片两项同时出现且顺序稳定")
{
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgImage, kChatImageStateReady, kHasPath, kCanRecall, kCannotPin, kNotPinned);

    const std::vector<int> expect = {
        kChatMsgMenuCopy, kChatMsgMenuSaveImageAs, kChatMsgMenuResendImage,
        kChatMsgMenuReply, kChatMsgMenuForward, 0,
        kChatMsgMenuRecall, kChatMsgMenuDelete, 0, kChatMsgMenuMultiSelect
    };
    CHECK(IdsOf(model) == expect);
}

TEST_CASE("CIM8 所有菜单项 id 互不相同")
{
    //分发时按 id switch，撞号会让两个功能串线，而这种错在界面上看不出来。
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgImage, kChatImageStateReady, kHasPath, kCanRecall, kCannotPin, kNotPinned);

    std::vector<int> ids;
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator)
        {
            ids.push_back((int)model[i].id);
        }
    }
    const std::size_t before = ids.size();
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    CHECK(ids.size() == before);
    //这个组合下八项应当全在
    CHECK(before == 8u);
}

TEST_CASE("CIM9 另存为默认名：自己发的图沿用原文件名")
{
    CHECK(MakeImageSaveAsDefaultName(L"D:\\photos\\photo.jpg", 0) == L"photo.jpg");
    CHECK(MakeImageSaveAsDefaultName(L"D:\\photos\\假期 合影.png", 501) == L"假期 合影.png");
    //正斜杠路径同样认
    CHECK(MakeImageSaveAsDefaultName(L"D:/photos/a.bmp", 0) == L"a.bmp");
    //没有目录、只有文件名
    CHECK(MakeImageSaveAsDefaultName(L"shot.png", 0) == L"shot.png");
}

TEST_CASE("CIM10 另存为默认名：收到的缓存图换成 IMG_<msgid>.png")
{
    //下载缓存的文件名就是消息 md5、不带扩展名（见 net/ImageTransfer.cpp
    //的 buildImageCachePath），拿它当默认名对用户毫无意义。
    CHECK(MakeImageSaveAsDefaultName(
              L"C:\\Users\\me\\Users\\5308\\ChatImage\\53b2b498ab38fbf33997a2bc153fd0e8", 243)
          == L"IMG_243.png");
    CHECK(MakeImageSaveAsDefaultName(L"C:\\cache\\84", 1) == L"IMG_1.png");
}

TEST_CASE("CIM11 另存为默认名：畸形输入给兜底名且不崩")
{
    //路径为空
    CHECK(MakeImageSaveAsDefaultName(L"", 243) == L"IMG_243.png");
    //msgid 未知（本端刚发出、服务器还没回执）时省略编号那一段
    CHECK(MakeImageSaveAsDefaultName(L"", 0) == L"IMG.png");
    CHECK(MakeImageSaveAsDefaultName(L"C:\\cache\\84", 0) == L"IMG.png");
    //以分隔符结尾（取出来是空文件名）
    CHECK(MakeImageSaveAsDefaultName(L"C:\\cache\\", 7) == L"IMG_7.png");
    //点号在开头的隐藏文件名没有主干，不当作有效文件名
    CHECK(MakeImageSaveAsDefaultName(L"C:\\cache\\.gitignore", 7) == L"IMG_7.png");
    //负数 msgid 按未知处理
    CHECK(MakeImageSaveAsDefaultName(L"", -5) == L"IMG.png");
}

// ---------------------------------------------------------------------------
// CIM-PIN 系列  右键菜单里的「Pin / 取消 Pin」一项。
//               方案见 docs/消息Pin接入方案.md §7.1。
// ---------------------------------------------------------------------------

//取某个菜单项的显示文案；模型里没有该项时返回空串。
static const wchar_t* LabelOf(const std::vector<ChatMessageMenuEntry>& model,
                              ChatMessageMenuId id)
{
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
        {
            return model[i].label;
        }
    }
    return L"";
}

TEST_CASE("CIM-PIN1 可 Pin 时那一项出现在「转发」之后、第一条分隔线之前")
{
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCannotRecall, kCanPin, kNotPinned);

    //文字消息带 Pin 的完整骨架：复制 / 引用回复 / 转发 / Pin / — / 删除 / — / 多选
    const std::vector<int> expect = {
        kChatMsgMenuCopy, kChatMsgMenuReply, kChatMsgMenuForward, kChatMsgMenuPin, 0,
        kChatMsgMenuDelete, 0, kChatMsgMenuMultiSelect
    };
    REQUIRE(model.size() == expect.size());
    for (std::size_t i = 0; i < expect.size(); ++i)
    {
        if (expect[i] == 0)
        {
            CHECK(model[i].isSeparator);
        }
        else
        {
            CHECK_FALSE(model[i].isSeparator);
            CHECK((int)model[i].id == expect[i]);
        }
    }
}

TEST_CASE("CIM-PIN2 文案随当前是否已钉住取反")
{
    const std::vector<ChatMessageMenuEntry> off =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCannotRecall, kCanPin, kNotPinned);
    const std::vector<ChatMessageMenuEntry> on =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCannotRecall, kCanPin, kPinned);

    CHECK(std::wstring(LabelOf(off, kChatMsgMenuPin)) == L"Pin");
    CHECK(std::wstring(LabelOf(on, kChatMsgMenuPin)) == L"取消 Pin");
}

TEST_CASE("CIM-PIN3 不可 Pin 时整项不出现，而不是置灰")
{
    //服务器还没给这条消息分配 msgid（本端刚发出、回执还没回填）：发过去必被判
    //message_not_exist，故整项不显示 —— 与「撤回」超时即整项消失同一种处理，
    //菜单里不留点不动的项。
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCannotRecall, kCannotPin, kNotPinned);
    CHECK_FALSE(HasItem(model, kChatMsgMenuPin));

    //其余各项不受影响
    CHECK(HasItem(model, kChatMsgMenuCopy));
    CHECK(HasItem(model, kChatMsgMenuForward));
    CHECK(HasItem(model, kChatMsgMenuDelete));
    CHECK(HasItem(model, kChatMsgMenuMultiSelect));
}

TEST_CASE("CIM-PIN4 Pin 与撤回、图片两项互不干扰")
{
    //图片行 + 可撤回 + 可 Pin：图片两项在「复制」之后，Pin 在「转发」之后，
    //撤回在第一条分隔线之后，四者各就各位
    const std::vector<ChatMessageMenuEntry> model =
        BuildChatMessageMenuModel(kMsgImage, kChatImageStateReady, kHasPath,
                                  kCanRecall, kCanPin, kPinned);

    const std::vector<int> expect = {
        kChatMsgMenuCopy, kChatMsgMenuSaveImageAs, kChatMsgMenuResendImage,
        kChatMsgMenuReply, kChatMsgMenuForward, kChatMsgMenuPin, 0,
        kChatMsgMenuRecall, kChatMsgMenuDelete, 0, kChatMsgMenuMultiSelect
    };
    REQUIRE(model.size() == expect.size());
    for (std::size_t i = 0; i < expect.size(); ++i)
    {
        if (expect[i] == 0)
        {
            CHECK(model[i].isSeparator);
        }
        else
        {
            CHECK_FALSE(model[i].isSeparator);
            CHECK((int)model[i].id == expect[i]);
        }
    }
}

TEST_CASE("CIM-PIN5 Pin 项的存在与否不影响撤回项的判定")
{
    //撤回不可用、Pin 可用
    const std::vector<ChatMessageMenuEntry> pinOnly =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCannotRecall, kCanPin, kNotPinned);
    CHECK(HasItem(pinOnly, kChatMsgMenuPin));
    CHECK_FALSE(HasItem(pinOnly, kChatMsgMenuRecall));

    //撤回可用、Pin 不可用
    const std::vector<ChatMessageMenuEntry> recallOnly =
        BuildChatMessageMenuModel(kMsgText, kChatImageStateReady, kHasPath,
                                  kCanRecall, kCannotPin, kNotPinned);
    CHECK_FALSE(HasItem(recallOnly, kChatMsgMenuPin));
    CHECK(HasItem(recallOnly, kChatMsgMenuRecall));
}
