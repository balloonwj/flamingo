/**
 *  机器人管理界面各窗口共用的尺寸、颜色与小控件建法（都由 balloonui 现成控件组合而成，不自绘）：
 *  文字标签、按钮、复选框、输入框、下拉框、机器人头像（字形 + 底色的 DuiAvatar）、卡片容器，以及测量文字宽高。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      HFONT font = flamingobot::WindowFont(GetDpi());
 *      int width = 0;
 *      std::unique_ptr<balloonwjui::DuiButton> ok = flamingobot::MakeButton(TR(L"保存"), kIdSave,
 *                                                   balloonwjui::DuiButton::Variant::Primary, font, width);
 *      row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(ok.release()), Hint().Fixed(width, kBotButtonHeight));
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

namespace flamingobot {

//---- 尺寸，单位：像素 ----
const int kBotTitleBarHeight = 32;     //各窗口的标题栏高
const int kBotPadX = 16;               //内容区左右内边距
const int kBotPadTop = 12;             //内容区上内边距
const int kBotPadBottom = 14;          //内容区下内边距
const int kBotRowGap = 8;              //上下相邻两行之间的距离
const int kBotButtonHeight = 28;       //按钮高
const int kBotEditHeight = 28;         //单行输入框、下拉框高
const int kBotLabelHeight = 20;        //单行文字高
const int kBotSectionHeight = 24;      //分组标题行高
const int kBotFieldLabelWidth = 96;    //表单左侧说明文字的宽度（只增不减，译文更长时按实际宽度加宽）
const int kBotCheckGlyphWidth = 32;    //复选框的方框与两侧间距占的宽度（balloonui DuiButton 的方框 16 加两侧各 6，再留 4）
const int kBotButtonTextPad = 14;      //按钮文字两侧的内边距
const int kBotMinButtonWidth = 64;     //按钮最窄的宽度
const int kBotCardRadius = 8;          //卡片圆角
const int kBotCardPad = 10;            //卡片内边距
const int kBotAvatarSize = 36;         //列表里机器人头像的边长
const int kBotAvatarCorner = 8;        //机器人头像的圆角（圆角方形，与人的圆形头像区分）
const int kBotScrollGutter = 8;        //可滚动内容区右侧给悬浮滚动条留的空白，免得滚动条盖住行末的按钮

//---- 颜色 ----
const COLORREF kBotBg = RGB(0xFF, 0xFF, 0xFF);           //内容区底色
const COLORREF kBotCardBg = RGB(0xF7, 0xF8, 0xFA);       //卡片底色
const COLORREF kBotCardBorder = RGB(0xE6, 0xE8, 0xEB);   //卡片描边
const COLORREF kBotText = RGB(0x1A, 0x1A, 0x1A);         //正文
const COLORREF kBotSubText = RGB(0x6B, 0x6B, 0x6B);      //说明、简介
const COLORREF kBotFaint = RGB(0x9A, 0x9A, 0x9A);        //更弱的提示
const COLORREF kBotError = RGB(0xD9, 0x53, 0x4F);        //出错说明
const COLORREF kBotOk = RGB(0x2E, 0x7D, 0x32);           //「生效中」等正常状态
const COLORREF kBotDefaultAvatar = RGB(45, 108, 223);    //头像没有设置底色时的默认色（与 DuiAvatar 的品牌蓝一致）

//头像底色的预设项（机器人编辑窗的「底色」下拉框）
struct BotColorPreset
{
    const char*    m_hex;   //"#RRGGBB"
    const wchar_t* m_name;  //名字（TR_NOOP 标记，使用处再调 TR）
};

/**
 *  头像底色的预设项列表。
 *  @param count [出参] 项数
 *  @return 预设项数组（静态存储，调用方不释放）
 */
const BotColorPreset* ColorPresets(size_t& count);

/**
 *  某个颜色文字在预设项里的下标；不在预设里（含空串）时返回 0（第一项）。
 */
int ColorPresetIndex(const std::string& hex);

/**
 *  窗口用的界面字体（balloonui 按 DPI 给的默认字体，借用，不要释放）。
 *  @param dpi 窗口的 DPI；窗口还没建出来时传 96
 */
HFONT WindowFont(int dpi);

/**
 *  一段单行文字的宽度，单位：像素。
 *  @param font 字体（借用）；为 NULL 时按屏幕 DC 的默认字体测量
 */
int MeasureTextWidth(HFONT font, const std::wstring& text);

/**
 *  一段文字按给定宽度自动换行后的高度，单位：像素；空串为一行高。
 */
int MeasureWrappedHeight(HFONT font, const std::wstring& text, int width);

/**
 *  建一个文字标签。
 *  @param align DrawText 对齐选项；多行文字传 DT_WORDBREAK
 */
std::unique_ptr<balloonwjui::DuiLabel> MakeLabel(const std::wstring& text, COLORREF color, HFONT font, DWORD align);

/**
 *  建一个按钮。
 *  @param width [出参] 正好放下文字的宽度（不小于 kBotMinButtonWidth）
 */
std::unique_ptr<balloonwjui::DuiButton> MakeButton(const std::wstring& text, UINT id,
                                                   balloonwjui::DuiButton::Variant variant, HFONT font, int& width);

/**
 *  建一个复选框。
 *  @param width [出参] 方框加文字的宽度
 */
std::unique_ptr<balloonwjui::DuiButton> MakeCheckBox(const std::wstring& text, UINT id, bool checked, HFONT font,
                                                     int& width);

/**
 *  建一个输入框。
 *  @param multiLine 多行（Enter 换行、自动换行）
 *  @param maxChars  最多字数；0 表示不限
 */
std::unique_ptr<balloonwjui::DuiEdit> MakeEdit(UINT id, const std::wstring& placeholder, const std::wstring& text,
                                               HFONT font, bool multiLine, int maxChars);

/**
 *  建一个下拉框（不可编辑）。
 *  @param items 各项文字
 *  @param sel   选中的下标；越界时选第一项
 */
std::unique_ptr<balloonwjui::DuiComboBox> MakeCombo(UINT id, const std::vector<std::wstring>& items, int sel);

/**
 *  建一个机器人头像：圆角方形，字形 + 底色。
 *  @param glyph 头像字形（1～2 个字）；为空时取名称的第一个字
 *  @param name  机器人名称
 *  @param color 底色 "#RRGGBB"；为空或不合法时用默认色
 */
std::unique_ptr<balloonwjui::DuiAvatar> MakeBotAvatar(const std::wstring& glyph, const std::wstring& name,
                                                      const std::wstring& color);

/**
 *  按字形、名称、底色更新一个已有的机器人头像（编辑窗里改了字形或底色时）。
 */
void ApplyBotAvatar(balloonwjui::DuiAvatar* avatar, const std::wstring& glyph, const std::wstring& name,
                    const std::wstring& color);

/**
 *  头像上要画的字：字形非空时取字形（两个字时中间加空格，让 DuiAvatar 两个字都画出来），否则取名称的第一个字。
 */
std::wstring AvatarInitials(const std::wstring& glyph, const std::wstring& name);

/**
 *  建一个卡片容器：圆角浅灰底、细描边、内边距 kBotCardPad。
 */
std::unique_ptr<balloonwjui::DuiVBox> MakeCard();

/**
 *  建一个分组标题行：加粗的标题，右侧可选一个按钮。
 *  @param buttonText 右侧按钮的文字；为空表示没有按钮
 *  @param buttonId   右侧按钮的控件 id
 *  @param button     [出参，可为 NULL] 右侧按钮（借用）
 */
std::unique_ptr<balloonwjui::DuiHBox> MakeSectionRow(const std::wstring& title, const std::wstring& buttonText,
                                                     UINT buttonId, HFONT font, balloonwjui::DuiButton** button);

/**
 *  建一个「说明文字 + 控件」的表单行：左侧固定宽度的说明，右侧控件占满剩余宽度。
 *  @param labelWidth 左侧说明的宽度
 *  @param field      右侧控件（所有权转入）
 */
std::unique_ptr<balloonwjui::DuiHBox> MakeFieldRow(const std::wstring& label, HFONT font, int labelWidth,
                                                   std::unique_ptr<balloonwjui::DuiControl> field);

/**
 *  表单左侧说明文字的宽度：不小于 kBotFieldLabelWidth，译文更长时按最长的一条加宽（只增不减，UI 约定第 8 条）。
 */
int FieldLabelWidth(HFONT font, const std::vector<std::wstring>& labels);

//底部一行里的一个按钮
struct FooterButtonSpec
{
    std::wstring                     m_text;     //按钮文字
    UINT                             m_id;       //控件 id
    balloonwjui::DuiButton::Variant  m_variant;  //样式
};

//BuildScrollFrame 建出来的、窗口要用到的控件（都归控件树所有，借用）
struct ScrollFrameControls
{
    balloonwjui::DuiScrollView*          m_scroll;   //可滚动的内容区
    balloonwjui::DuiVBox*                m_content;  //内容区里的竖排容器（窗口往里加控件）
    balloonwjui::DuiLabel*               m_status;   //底部一行左侧的说明（出错时红字）
    std::vector<balloonwjui::DuiButton*> m_buttons;  //底部一行的按钮，与传入的顺序相同

    ScrollFrameControls()
        : m_scroll(NULL)
        , m_content(NULL)
        , m_status(NULL)
    {
    }
};

/**
 *  建窗口标题栏以下的骨架：上面是可滚动的内容区（悬浮式滚动条，UI 约定第 1 条），下面固定一行「说明 + 按钮」。
 *  内容区的竖排容器按子控件的固定高度自动算总高（SetAutoContentHeight），子控件一律用固定高度加入。
 *  @param font    字体（借用）
 *  @param buttons 底部的按钮，从左到右
 *  @param out     [出参] 窗口要用到的控件
 *  @return 控件树的根
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildScrollFrame(HFONT font, const std::vector<FooterButtonSpec>& buttons,
                                                       ScrollFrameControls& out);

/**
 *  内容区的内容变了（增删了子控件、改了子控件的高度）之后重新排版：按 UI 约定第 12 条对滚动视图 ForceLayout，
 *  否则超出原高度的控件画得出来却点不到；随后再对内容本身 ForceLayout，否则内容总高度不变时新建的子控件
 *  不会排版、显示不出来（用例 BOTL4）。
 */
void RelayoutScroll(balloonwjui::DuiScrollView* scroll);

/**
 *  在底部一行的说明里显示一段文字。
 *  @param isError 出错说明用红字，普通说明用灰字
 */
void SetStatusText(balloonwjui::DuiLabel* status, const std::wstring& text, bool isError);

//删除容器里的全部子控件
void RemoveAllChildren(balloonwjui::DuiControl* box);

//输入框的文字
std::wstring EditText(const balloonwjui::DuiEdit* edit);

} // namespace flamingobot
