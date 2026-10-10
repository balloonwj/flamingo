/**
 *  ScreenshotActions.h
 *
 *  截图浮层操作栏的按钮表：各按钮的控件 id、文字、图标、宽度与主按钮标记，以及选区内
 *  双击对应哪个按钮。操作栏有两套：
 *    · 从单聊 / 群聊窗发起（绑定会话）：重选 / 复制 / 保存 / [发送到对话框] / 发送 / 取消；
 *    · 在聊天窗之外按截图快捷键发起（不绑定会话）：重选 / 保存 / 取消 / [完成]，「完成」
 *      把截图放进剪贴板（docs/截图快捷键与隐藏当前窗口方案.md 第 1 节）。
 *  表与画布控件 CaptureCanvas 分开放，是为了让 FlamingoUnit 不编译整个画布也能断言按钮
 *  顺序（用例 SA1~SA4）。
 *
 *  典型用法：
 *      int count = 0;
 *      const ActionItem* items = GetActionItems(hasSendTarget, count);
 *      for (int i = 0; i < count; ++i) { ... items[i].m_id ... }
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include "ScreenshotToolbar.h"   // ToolIconKind

namespace balloonscreenshot {

// 操作工具栏各按钮的控件 id。遮罩窗口（ScreenCaptureOverlay）在 WM_DUI_NOTIFY 里
// 按这些 id 区分被点的按钮。本浮层是 DuiHost（无标题栏），不受 DuiFrameWindow 标题栏
// 占用 1/2/3 的限制，但仍从 ≥10 起编以求稳妥。2026-10-05 由 CaptureCanvas.h 移到这里。
enum
{
    kIdToolReselect = 10,   // 重选：清掉当前选区、回到可重新框选状态
    kIdToolCopy     = 11,   // 复制：把选区图复制到剪贴板（只出现在绑定会话的操作栏里）
    kIdToolSave     = 12,   // 保存：把选区图存成 PNG 文件
    kIdToolStage    = 13,   // 发送到对话框：把选区图内联插入聊天输入框（不立即发），绑定会话时的主按钮
    kIdToolSend     = 14,   // 发送：把选区图作为图片消息立即发到当前会话
    kIdToolCancel   = 15,   // 取消：放弃截图、关闭浮层
    kIdToolDone     = 16,   // 完成：不绑定会话时的主按钮，把选区图放进剪贴板，用户再自行粘贴
    kIdToolOcr      = 17,   // 识别文字：识别选区里的文字（用不带标注的原始像素），关闭浮层后打开识别结果窗口；
                            // 只在图片文字识别模块可用时出现，排在「保存」之后（docs/图片文字识别方案.md 第 8.5 节）
};

// 操作栏上一个按钮的描述。表为静态常量，文字用 TR_NOOP 标记，使用处再经 TR 翻译。
struct ActionItem
{
    UINT           m_id;              // 控件 id（见上面的枚举）
    const wchar_t* m_label;           // 按钮文字原文（TR_NOOP 标记）
    ToolIconKind   m_icon;            // 文字左边的小图标
    const wchar_t* m_tip;             // 悬停提示原文（TR_NOOP 标记）
    int            m_width;           // 按钮宽度（像素）
    bool           m_primary;         // 是否为橙色实心主按钮（每套表恰有一个）
    bool           m_groupGapBefore;  // 前面是否留分组间距（主按钮与普通按钮之间）
};

/**
 *  取操作栏的按钮表。
 *  @param hasSendTarget true=从聊天窗发起、可以发到该会话；false=不绑定会话，只能保存或放进剪贴板
 *  @param count         输出：按钮个数
 *  @param withOcr       是否带「识别文字」按钮（图片文字识别模块可用时为 true）；带时排在「保存」之后
 *  @return 静态表首地址（从左到右的顺序），调用方只读、不释放
 */
const ActionItem* GetActionItems(bool hasSendTarget, int& count, bool withOcr = false);

/**
 *  选区内双击等同于点哪个按钮。
 *  @param hasSendTarget 同 GetActionItems
 *  @return 绑定会话时为 kIdToolStage（发送到对话框），否则为 kIdToolDone（完成）
 */
UINT DoubleClickActionId(bool hasSendTarget);

} // namespace balloonscreenshot
