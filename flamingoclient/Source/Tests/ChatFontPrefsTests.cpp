/**
 *  ChatFontPrefsTests.cpp
 *
 *  聊天字体偏好（Session/ChatFontPrefs.h）的测试：
 *    CF1 —— 字体设置与配置文件里那串 ASCII 文本之间的编码 / 解码；
 *    CF3 —— 各会话自定义字体的增、改、删，以及整段导出 / 导入；
 *    CF4 —— 按会话取生效字体：会话自定义 > 全局默认 > 未设置；
 *    CF5 —— 系统设置「聊天」页的「默认聊天字体」预览按全局默认的字号、样式、颜色画出来；
 *    CF6 —— 「AF」字体浮层的「使用默认字体」按钮：会话有自定义字体时才可点。
 *
 *  两层字体的规则：全局默认在系统设置的「聊天」页设置；某个聊天窗经「AF」浮层改的
 *  字体只对这个会话生效。两者都存在账号配置里，重启后仍然有效。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不读写文件。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <utility>
#include <vector>

#include "../Session/ChatFontPrefs.h"
#include "../UI/BalloonChatWnd/FontSettingPopup.h"
#include "../UI/BalloonSettings/ChatFontPreview.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace chatfont;

namespace {

// 会话类型：单聊与群聊，取值与协议 conversation_type 一致。
const int kSingle = 1;
const int kGroup  = 2;

// 造一个设置过的字体。
net::TextFont MakeFont(int size, bool bold, uint32_t colorRgb)
{
    net::TextFont f;
    f.m_valid    = true;
    f.m_size     = size;
    f.m_bold     = bold;
    f.m_colorRgb = colorRgb;
    return f;
}

// 两个字体设置的各项是否完全相同。
bool SameFont(const net::TextFont& a, const net::TextFont& b)
{
    return a.m_valid == b.m_valid
        && a.m_size == b.m_size
        && a.m_bold == b.m_bold
        && a.m_italic == b.m_italic
        && a.m_underline == b.m_underline
        && a.m_strike == b.m_strike
        && a.m_colorRgb == b.m_colorRgb;
}

} // anonymous namespace

TEST_CASE("CF1 字体设置与配置文本来回转换不丢信息", "[chatfont]")
{
    //各项都设上的字体：编码后再解码，每一项都一致
    net::TextFont full = MakeFont(20, true, 0x1E90FF);
    full.m_italic    = true;
    full.m_underline = true;
    full.m_strike    = true;
    const CString text = EncodeFont(full);
    //只含 ASCII 数字与逗号，与配置文件的读写方式兼容
    for (int i = 0; i < text.GetLength(); ++i)
    {
        CHECK(((text[i] >= _T('0') && text[i] <= _T('9')) || text[i] == _T(',')));
    }
    net::TextFont back;
    REQUIRE(DecodeFont(text, back));
    CHECK(SameFont(back, full));

    //未设置：编码成 "0"，解码回来仍是未设置
    net::TextFont unset;
    CHECK(EncodeFont(unset) == CString(_T("0")));
    net::TextFont backUnset = MakeFont(30, true, 0x123456);
    REQUIRE(DecodeFont(_T("0"), backUnset));
    CHECK_FALSE(backUnset.m_valid);

    //字号越界时夹在 8..72 之间，颜色只取低 24 位
    net::TextFont clamped;
    REQUIRE(DecodeFont(_T("1,200,0,0,0,0,4294967295"), clamped));
    CHECK(clamped.m_size == 72);
    CHECK(clamped.m_colorRgb == 0xFFFFFFu);
    REQUIRE(DecodeFont(_T("1,2,0,0,0,0,0"), clamped));
    CHECK(clamped.m_size == 8);

    //格式不对的文本一律拒绝，出参不动
    net::TextFont untouched = MakeFont(16, false, 0x333333);
    CHECK_FALSE(DecodeFont(_T(""), untouched));
    CHECK_FALSE(DecodeFont(_T("abc"), untouched));
    CHECK_FALSE(DecodeFont(_T("1,14"), untouched));
    CHECK_FALSE(DecodeFont(_T("1,x,0,0,0,0,1"), untouched));
    CHECK(SameFont(untouched, MakeFont(16, false, 0x333333)));
}

TEST_CASE("CF3 会话自定义字体的增改删与整段导出导入", "[chatfont]")
{
    ChatFontPrefs prefs;
    const net::TextFont a = MakeFont(18, true, 0x00AA00);
    const net::TextFont b = MakeFont(12, false, 0x0000FF);

    //增：单聊与群聊的 ID 数值相同也互不串
    prefs.SetOverride(kSingle, 5057, a);
    prefs.SetOverride(kGroup, 5057, b);
    CHECK(prefs.HasOverride(kSingle, 5057));
    CHECK(prefs.HasOverride(kGroup, 5057));
    CHECK_FALSE(prefs.HasOverride(kSingle, 5073));
    net::TextFont got;
    REQUIRE(prefs.GetOverride(kSingle, 5057, got));
    CHECK(SameFont(got, a));
    REQUIRE(prefs.GetOverride(kGroup, 5057, got));
    CHECK(SameFont(got, b));

    //改
    prefs.SetOverride(kGroup, 5057, a);
    REQUIRE(prefs.GetOverride(kGroup, 5057, got));
    CHECK(SameFont(got, a));

    //删：删掉不存在的返回 false；传一个未设置的字体等同于删除
    CHECK(prefs.RemoveOverride(kGroup, 5057));
    CHECK_FALSE(prefs.RemoveOverride(kGroup, 5057));
    CHECK_FALSE(prefs.HasOverride(kGroup, 5057));
    prefs.SetOverride(kSingle, 5057, net::TextFont());
    CHECK_FALSE(prefs.HasOverride(kSingle, 5057));

    //整段导出再导入：内容一致
    prefs.SetOverride(kSingle, 5057, a);
    prefs.SetOverride(kGroup, 268435458LL, b);
    std::vector<std::pair<CString, CString> > entries;
    prefs.ExportOverrides(entries);
    CHECK(entries.size() == 2);
    ChatFontPrefs reloaded;
    reloaded.ImportOverrides(entries);
    REQUIRE(reloaded.GetOverride(kSingle, 5057, got));
    CHECK(SameFont(got, a));
    REQUIRE(reloaded.GetOverride(kGroup, 268435458LL, got));
    CHECK(SameFont(got, b));

    //导入时，键或值格式不对的行跳过，其余照常导入；导入是整体替换
    std::vector<std::pair<CString, CString> > dirty;
    dirty.push_back(std::make_pair(CString(_T("2_268435459")), EncodeFont(a)));
    dirty.push_back(std::make_pair(CString(_T("bad-key")), EncodeFont(a)));
    dirty.push_back(std::make_pair(CString(_T("1_5073")), CString(_T("garbage"))));
    dirty.push_back(std::make_pair(CString(_T("1_5081")), CString(_T("0"))));
    reloaded.ImportOverrides(dirty);
    CHECK(reloaded.HasOverride(kGroup, 268435459LL));
    CHECK_FALSE(reloaded.HasOverride(kSingle, 5073));
    CHECK_FALSE(reloaded.HasOverride(kSingle, 5081));
    CHECK_FALSE(reloaded.HasOverride(kSingle, 5057));
    std::vector<std::pair<CString, CString> > after;
    reloaded.ExportOverrides(after);
    CHECK(after.size() == 1);
}

TEST_CASE("CF4 按会话取生效字体：会话自定义优先，其次全局默认", "[chatfont]")
{
    ChatFontPrefs prefs;
    const net::TextFont def  = MakeFont(14, false, 0x1A1A1A);
    const net::TextFont mine = MakeFont(20, true, 0xFF0000);

    //两者都没有：未设置（发出的消息不带字体）
    CHECK_FALSE(prefs.Resolve(kSingle, 5057).m_valid);

    //只有全局默认
    prefs.SetDefault(def);
    CHECK(SameFont(prefs.Resolve(kSingle, 5057), def));

    //会话有自定义：用自定义；别的会话仍用全局默认
    prefs.SetOverride(kSingle, 5057, mine);
    CHECK(SameFont(prefs.Resolve(kSingle, 5057), mine));
    CHECK(SameFont(prefs.Resolve(kGroup, 5057), def));

    //全局默认清除后：有自定义的会话不受影响，其余会话变回未设置
    prefs.SetDefault(net::TextFont());
    CHECK(SameFont(prefs.Resolve(kSingle, 5057), mine));
    CHECK_FALSE(prefs.Resolve(kSingle, 5073).m_valid);
}

namespace {

// 「使用默认字体」回调：把计数器加一。userdata 为 int 计数器。
void CountUseDefault(void* userdata)
{
    ++*(int*)userdata;
}

// 字体变化回调：把计数器加一。userdata 为 int 计数器。
void CountFontChange(void* userdata, const net::TextFont& /*font*/)
{
    ++*(int*)userdata;
}

} // anonymous namespace

TEST_CASE("CF6 字体浮层的「使用默认字体」：会话有自定义字体时才可点", "[chatfont]")
{
    //系统设置页打开浮层时不装这个回调：没有这个按钮，点了也不会有反应
    CFontSettingPopup settingsPopup;
    CHECK_FALSE(settingsPopup.HasUseDefaultButton());
    CHECK_FALSE(settingsPopup.HandleClick(CFontSettingPopup::kIdUseDefault));

    //聊天窗打开浮层时装上回调
    int useDefaultCalls = 0;
    int changeCalls = 0;
    CFontSettingPopup popup;
    popup.SetChangeCallback(&CountFontChange, &changeCalls);
    popup.SetUseDefaultCallback(&CountUseDefault, &useDefaultCalls);
    CHECK(popup.HasUseDefaultButton());

    //会话没有自定义字体：按钮不可点
    popup.SetUseDefaultEnabled(false);
    CHECK_FALSE(popup.IsUseDefaultEnabled());
    CHECK_FALSE(popup.HandleClick(CFontSettingPopup::kIdUseDefault));
    CHECK(useDefaultCalls == 0);

    //会话有自定义字体：可点，点一次回调一次，不会顺带触发字体变化回调
    popup.SetUseDefaultEnabled(true);
    CHECK(popup.IsUseDefaultEnabled());
    CHECK(popup.HandleClick(CFontSettingPopup::kIdUseDefault));
    CHECK(useDefaultCalls == 1);
    CHECK(changeCalls == 0);

    //点其它设置项照常走字体变化回调，不会误触发"使用默认字体"
    const UINT kUnknownId = 1;
    CHECK_FALSE(popup.HandleClick(kUnknownId));
    CHECK(useDefaultCalls == 1);
    CHECK(changeCalls == 0);

    //宿主据回调删掉本会话的自定义字体后，这个会话改用全局默认
    ChatFontPrefs prefs;
    const net::TextFont def  = MakeFont(14, false, 0x1A1A1A);
    const net::TextFont mine = MakeFont(20, true, 0xFF0000);
    prefs.SetDefault(def);
    prefs.SetOverride(kGroup, 268435458LL, mine);
    CHECK(prefs.RemoveOverride(kGroup, 268435458LL));
    CHECK(SameFont(prefs.Resolve(kGroup, 268435458LL), def));
}

namespace {

// CF5 离屏画布尺寸（逻辑像素）：宽放得下预览文字，高放得下 20px 的字。
const int kPreviewW = 200;
const int kPreviewH = 40;

// CF5 预览用的颜色（0xRRGGBB）与对应的 COLORREF。取字体浮层调色板里的红。
const uint32_t kPreviewRgb = 0xE54B4B;
const COLORREF kPreviewColor = RGB(0xE5, 0x4B, 0x4B);

// 判定"文字像素"的亮度上限：白底比它亮，文字比它暗。
const int kPreviewInkMax = 0xE0;

/**
 *  把标签画到白底离屏位图上，统计给定颜色的像素个数，以及非白像素覆盖的高度。
 *    label：    要画的标签；
 *    color：    要统计的颜色；
 *    outCount： [出参] 恰为 color 的像素个数；
 *    outInkH：  [出参] 非白像素覆盖的行数（文字高度），没有文字时为 0。
 *  返回：画布建成返回 true。
 */
bool RenderLabel(balloonwjui::DuiLabel& label, COLORREF color, int& outCount, int& outInkH)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kPreviewW;
    bi.bmiHeader.biHeight      = -kPreviewH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    RECT all = { 0, 0, kPreviewW, kPreviewH };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    label.SetRect(all);
    label.OnPaint(memDC, all);

    const unsigned int target = ((unsigned int)GetRValue(color) << 16)
                              | ((unsigned int)GetGValue(color) << 8)
                              |  (unsigned int)GetBValue(color);
    const unsigned int* px = (const unsigned int*)bits;
    outCount = 0;
    int top = -1;
    int bottom = -1;
    for (int y = 0; y < kPreviewH; ++y)
    {
        for (int x = 0; x < kPreviewW; ++x)
        {
            const unsigned int p = px[y * kPreviewW + x] & 0x00FFFFFFu;
            if (p == target)
            {
                ++outCount;
            }
            const int r = (int)((p >> 16) & 0xFF);
            const int g = (int)((p >> 8) & 0xFF);
            const int b = (int)(p & 0xFF);
            if (r <= kPreviewInkMax || g <= kPreviewInkMax || b <= kPreviewInkMax)
            {
                if (top < 0)
                {
                    top = y;
                }
                bottom = y;
            }
        }
    }
    outInkH = (top < 0) ? 0 : (bottom - top + 1);

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return true;
}

} // anonymous namespace

TEST_CASE("CF5 设置页「默认聊天字体」预览按全局默认的字号、样式、颜色画出来", "[chatfont]")
{
    balloonwjui::DuiLabel label;
    HFONT previewFont = NULL;

    //设置过：20px 粗体红字，显示预览文字
    balloonsettings::ApplyChatFontPreview(&label, MakeFont(20, true, kPreviewRgb), previewFont);
    CHECK(previewFont != NULL);
    CHECK(label.GetText() == CString(balloonsettings::kChatFontPreviewText));
    int bigCount = 0;
    int bigInkH = 0;
    REQUIRE(RenderLabel(label, kPreviewColor, bigCount, bigInkH));
    INFO("20px 红字：颜色像素 " << bigCount << "，文字高 " << bigInkH);
    CHECK(bigCount > 0);

    //换成 12px：文字明显变矮，颜色不变
    balloonsettings::ApplyChatFontPreview(&label, MakeFont(12, false, kPreviewRgb), previewFont);
    CHECK(previewFont != NULL);
    int smallCount = 0;
    int smallInkH = 0;
    REQUIRE(RenderLabel(label, kPreviewColor, smallCount, smallInkH));
    INFO("12px 红字：颜色像素 " << smallCount << "，文字高 " << smallInkH);
    CHECK(smallCount > 0);
    CHECK(smallInkH < bigInkH);

    //未设置：显示「未设置」、灰字，退回标签默认字体，预览字体句柄已释放并置空
    balloonsettings::ApplyChatFontPreview(&label, net::TextFont(), previewFont);
    CHECK(previewFont == NULL);
    CHECK(label.GetFont() == NULL);
    CHECK(label.GetText() == CString(balloonsettings::kChatFontUnsetText));
    CHECK(label.GetTextColor() == balloonsettings::kChatFontUnsetColor);
    int unsetCount = 0;
    int unsetInkH = 0;
    REQUIRE(RenderLabel(label, kPreviewColor, unsetCount, unsetInkH));
    CHECK(unsetCount == 0);
    CHECK(unsetInkH > 0);
}
