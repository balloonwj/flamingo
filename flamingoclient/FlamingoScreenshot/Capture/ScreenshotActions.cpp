/**
 *  ScreenshotActions.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ScreenshotActions.h"
#include "../../Source/Common/I18n/I18n.h"   // TR_NOOP：按钮文字只做标记，使用处再翻译

namespace balloonscreenshot {

namespace {

const int kActNormalW = 74;    // 2 字操作项宽（含小图标 + 文字）
const int kActStageW  = 132;   // 「发送到对话框」宽（含小图标 + 文字）
const int kActOcrW    = 100;   // 「识别文字」宽（含小图标 + 4 个字）

// 从聊天窗发起时的操作栏（顺序即从左到右；stage 为橙色主按钮、其前留分组间距）。
const ActionItem kChatActionItems[] =
{
    { kIdToolReselect, TR_NOOP(L"重选"),         kIconReselect, TR_NOOP(L"重新框选"),             kActNormalW, false, false },
    { kIdToolCopy,     TR_NOOP(L"复制"),         kIconCopy,     TR_NOOP(L"复制到剪贴板"),         kActNormalW, false, false },
    { kIdToolSave,     TR_NOOP(L"保存"),         kIconSave,     TR_NOOP(L"保存为图片"),           kActNormalW, false, false },
    { kIdToolStage,    TR_NOOP(L"发送到对话框"), kIconStage,    TR_NOOP(L"把截图插入聊天输入框"), kActStageW,  true,  true  },
    { kIdToolSend,     TR_NOOP(L"发送"),         kIconSend,     TR_NOOP(L"立即发送到当前会话"),   kActNormalW, false, false },
    { kIdToolCancel,   TR_NOOP(L"取消"),         kIconCancel,   TR_NOOP(L"取消截图（Esc）"),      kActNormalW, false, false },
};

// 不绑定会话时的操作栏：没有「复制」「发送」「发送到对话框」，最右边是橙色的「完成」。
const ActionItem kClipboardActionItems[] =
{
    { kIdToolReselect, TR_NOOP(L"重选"), kIconReselect, TR_NOOP(L"重新框选"),                       kActNormalW, false, false },
    { kIdToolSave,     TR_NOOP(L"保存"), kIconSave,     TR_NOOP(L"保存为图片"),                     kActNormalW, false, false },
    { kIdToolCancel,   TR_NOOP(L"取消"), kIconCancel,   TR_NOOP(L"取消截图（Esc）"),                kActNormalW, false, false },
    { kIdToolDone,     TR_NOOP(L"完成"), kIconDone,     TR_NOOP(L"放入剪贴板，可粘贴到任意程序"),   kActNormalW, true,  true  },
};

// 以上两套各自带「识别文字」的版本：图片文字识别模块可用时使用，「识别文字」排在「保存」之后。
const ActionItem kChatActionItemsWithOcr[] =
{
    { kIdToolReselect, TR_NOOP(L"重选"),         kIconReselect, TR_NOOP(L"重新框选"),             kActNormalW, false, false },
    { kIdToolCopy,     TR_NOOP(L"复制"),         kIconCopy,     TR_NOOP(L"复制到剪贴板"),         kActNormalW, false, false },
    { kIdToolSave,     TR_NOOP(L"保存"),         kIconSave,     TR_NOOP(L"保存为图片"),           kActNormalW, false, false },
    { kIdToolOcr,      TR_NOOP(L"识别文字"),     kIconOcr,      TR_NOOP(L"识别选区里的文字"),     kActOcrW,    false, false },
    { kIdToolStage,    TR_NOOP(L"发送到对话框"), kIconStage,    TR_NOOP(L"把截图插入聊天输入框"), kActStageW,  true,  true  },
    { kIdToolSend,     TR_NOOP(L"发送"),         kIconSend,     TR_NOOP(L"立即发送到当前会话"),   kActNormalW, false, false },
    { kIdToolCancel,   TR_NOOP(L"取消"),         kIconCancel,   TR_NOOP(L"取消截图（Esc）"),      kActNormalW, false, false },
};

const ActionItem kClipboardActionItemsWithOcr[] =
{
    { kIdToolReselect, TR_NOOP(L"重选"),     kIconReselect, TR_NOOP(L"重新框选"),                     kActNormalW, false, false },
    { kIdToolSave,     TR_NOOP(L"保存"),     kIconSave,     TR_NOOP(L"保存为图片"),                   kActNormalW, false, false },
    { kIdToolOcr,      TR_NOOP(L"识别文字"), kIconOcr,      TR_NOOP(L"识别选区里的文字"),             kActOcrW,    false, false },
    { kIdToolCancel,   TR_NOOP(L"取消"),     kIconCancel,   TR_NOOP(L"取消截图（Esc）"),              kActNormalW, false, false },
    { kIdToolDone,     TR_NOOP(L"完成"),     kIconDone,     TR_NOOP(L"放入剪贴板，可粘贴到任意程序"), kActNormalW, true,  true  },
};

} // anonymous namespace

const ActionItem* GetActionItems(bool hasSendTarget, int& count, bool withOcr)
{
    if (hasSendTarget)
    {
        if (withOcr)
        {
            count = (int)(sizeof(kChatActionItemsWithOcr) / sizeof(kChatActionItemsWithOcr[0]));
            return kChatActionItemsWithOcr;
        }
        count = (int)(sizeof(kChatActionItems) / sizeof(kChatActionItems[0]));
        return kChatActionItems;
    }
    if (withOcr)
    {
        count = (int)(sizeof(kClipboardActionItemsWithOcr) / sizeof(kClipboardActionItemsWithOcr[0]));
        return kClipboardActionItemsWithOcr;
    }
    count = (int)(sizeof(kClipboardActionItems) / sizeof(kClipboardActionItems[0]));
    return kClipboardActionItems;
}

UINT DoubleClickActionId(bool hasSendTarget)
{
    return hasSendTarget ? (UINT)kIdToolStage : (UINT)kIdToolDone;
}

} // namespace balloonscreenshot
