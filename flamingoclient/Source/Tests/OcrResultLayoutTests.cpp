/**
 *  「图片文字识别」结果窗口的布局与文字单测（UI/BalloonOcr/OcrResultLayout），OcrResultLayoutTests.cpp
 *
 *  按开发约定文档 UI 约定第 7 条配「按设计尺寸排得下」的检查（docs/图片文字识别方案.md 第 10.2 节「布局」）：
 *    OCRW1 按默认尺寸与最小尺寸扣掉标题栏后都排得下：底部两个按钮、第一行的下拉框与状态行保持原高、
 *          下内边距没有被占用，识别结果文字框还有足够的高度；
 *    OCRW2 各控件按 id 都能取到、类型与窗口代码取回时一致，且没有 id 与标题栏按钮相同；
 *    OCRW3 状态文字：各结果码都有文字、成功时报行数；剪贴板换行整理成 \r\n。
 *    OCRW4 空窗口（系统菜单打开）：有「请打开一张图片」的提示；没有图片来源时不能重新识别（docs/图片文字识别方案.md 第 14 节）。
 *  OCRW1、OCRW2 同时检查底部按钮行左侧的「打开图片」按钮。
 *
 *  标签 [ocrhost][layoutfit]。框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "LayoutFitCheck.h"
#include "../UI/BalloonOcr/OcrResultLayout.h"
#include "../Feature/Ocr/OcrRecognizer.h"
#include "../../FlamingoOcr/FlamingoOcrApi.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

using namespace balloonwjui;
using namespace balloonocr;

namespace {

//DuiFrameWindow 标题栏三个按钮固定使用的控件 id：1 最小化、2 最大化、3 关闭
const UINT kCaptionIdFirst = 1;
const UINT kCaptionIdLast  = 3;

//最小尺寸下，识别结果文字框至少要留出的高度（像素）：约五行文字
const int kTextMinUsableH = 100;

//递归收集控件树里所有 id 落在标题栏按钮范围内的控件 id（0 表示没设 id，不算）
void CollectCaptionIdClashes(const DuiControl* ctrl, std::vector<UINT>& clashes)
{
    if (ctrl == nullptr)
        return;
    const UINT id = ctrl->GetCtrlId();
    if (id >= kCaptionIdFirst && id <= kCaptionIdLast)
        clashes.push_back(id);
    for (size_t i = 0; i < ctrl->Children().size(); ++i)
        CollectCaptionIdClashes(ctrl->Children()[i].get(), clashes);
}

//按给定整窗尺寸检查：固定高度的控件保持原高、按钮在内容区之内且下方留白不被占用、文字框够高
void CheckFitsAt(int windowWidth, int windowHeight)
{
    const int contentH = windowHeight - kOcrResultTitleBarH;
    std::unique_ptr<DuiControl> root = BuildOcrResultBody();
    REQUIRE(root != nullptr);
    INFO("window " << windowWidth << " x " << windowHeight);

    const UINT buttons[] = { (UINT)kOcrIdOpen, (UINT)kOcrIdRetry, (UINT)kOcrIdCopy };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        INFO("button id = " << buttons[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), buttons[i], windowWidth, contentH);
        REQUIRE(r.m_found);
        INFO("bottom = " << r.m_bottom << ", height = " << r.m_height << ", roomy height = " << r.m_roomyHeight
             << ", bottom gap = " << r.m_bottomGap << ", roomy bottom gap = " << r.m_roomyBottomGap);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }

    const UINT fixed[] = { (UINT)kOcrIdLanguage, (UINT)kOcrIdStatus, (UINT)kOcrIdLanguageCaption };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
    {
        INFO("control id = " << fixed[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), fixed[i], windowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_height == r.m_roomyHeight);
    }

    //文字框占满中间的弹性区：够高，且不压到按钮行
    const layoutfit::FitResult text = layoutfit::MeasureAt(root.get(), kOcrIdText, windowWidth, contentH);
    REQUIRE(text.m_found);
    INFO("text height = " << text.m_height);
    CHECK(text.m_height >= kTextMinUsableH);
    const RECT rcText = root->FindCtrlById(kOcrIdText)->GetRect();
    const RECT rcCopy = root->FindCtrlById(kOcrIdCopy)->GetRect();
    CHECK(rcText.bottom <= rcCopy.top);
    CHECK(rcText.left >= 0);
    CHECK(rcText.right <= windowWidth);
}

} // namespace

TEST_CASE("OCRW1 结果窗口按默认尺寸与最小尺寸都排得下", "[ocrhost][layoutfit]")
{
    CheckFitsAt(kOcrResultWindowWidth, kOcrResultWindowHeight);
    CheckFitsAt(kOcrResultMinWidth, kOcrResultMinHeight);
    CHECK(kOcrResultMinWidth <= kOcrResultWindowWidth);
    CHECK(kOcrResultMinHeight <= kOcrResultWindowHeight);
}

TEST_CASE("OCRW2 结果窗口的控件按 id 都能取到、类型正确、不与标题栏按钮撞号", "[ocrhost][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildOcrResultBody();
    REQUIRE(root != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kOcrIdLanguageCaption)) != nullptr);
    CHECK(dynamic_cast<DuiComboBox*>(root->FindCtrlById(kOcrIdLanguage)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kOcrIdStatus)) != nullptr);
    DuiRichEdit* text = dynamic_cast<DuiRichEdit*>(root->FindCtrlById(kOcrIdText));
    REQUIRE(text != nullptr);
    CHECK(text->IsMultiLine());
    CHECK(text->IsWordWrap());
    CHECK_FALSE(text->IsReadOnly());   //识别错的字要能就地修改
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdRetry)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdCopy)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdOpen)) != nullptr);

    //「打开图片」在按钮行左侧，「重新识别」「复制全部」在右侧
    RECT rc = { 0, 0, kOcrResultWindowWidth, kOcrResultWindowHeight - kOcrResultTitleBarH };
    root->ForceLayout(rc);
    const RECT rcOpen = root->FindCtrlById(kOcrIdOpen)->GetRect();
    const RECT rcRetry = root->FindCtrlById(kOcrIdRetry)->GetRect();
    const RECT rcCopy = root->FindCtrlById(kOcrIdCopy)->GetRect();
    CHECK(rcOpen.right < rcRetry.left);
    CHECK(rcRetry.right <= rcCopy.left);
    CHECK(rcOpen.top == rcRetry.top);

    std::vector<UINT> clashes;
    CollectCaptionIdClashes(root.get(), clashes);
    CHECK(clashes.empty());
}

TEST_CASE("OCRW3 状态文字与剪贴板换行整理", "[ocrhost]")
{
    //各结果码都有非空文字；成功且有文字时带行数
    const int codes[] = { flamingoocr::ocr_ok, flamingoocr::ocr_bad_argument, flamingoocr::ocr_unavailable,
                          flamingoocr::ocr_language_unsupported, flamingoocr::ocr_cancelled,
                          flamingoocr::ocr_engine_error, flamingoocr::ocr_out_of_memory,
                          ocr::ocr_task_decode_failed, ocr::ocr_task_no_engine };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
        INFO("code = " << codes[i]);
        CHECK_FALSE(OcrResultStatusText(codes[i], 3).empty());
    }
    CHECK(OcrResultStatusText(flamingoocr::ocr_ok, 12).find(L"12") != std::wstring::npos);
    CHECK(OcrResultStatusText(flamingoocr::ocr_ok, 0) != OcrResultStatusText(flamingoocr::ocr_ok, 1));
    CHECK(OcrResultStatusText(ocr::ocr_task_decode_failed, 0) != OcrResultStatusText(flamingoocr::ocr_engine_error, 0));
    CHECK_FALSE(OcrRecognizingText().empty());

    //换行整理：RichEdit 内部用 \r 分段，剪贴板里要 \r\n；已有的 \r\n 不重复
    CHECK(NormalizeLineBreaks(L"a\rb") == L"a\r\nb");
    CHECK(NormalizeLineBreaks(L"a\nb") == L"a\r\nb");
    CHECK(NormalizeLineBreaks(L"a\r\nb") == L"a\r\nb");
    CHECK(NormalizeLineBreaks(L"a\r\rb\n") == L"a\r\n\r\nb\r\n");
    CHECK(NormalizeLineBreaks(L"").empty());
}

TEST_CASE("OCRW4 空窗口：提示打开图片，没有图片来源时不能重新识别", "[ocrhost]")
{
    //系统菜单打开的空窗口在状态行提示用户先打开一张图片
    CHECK_FALSE(OcrNoImageText().empty());
    CHECK(OcrNoImageText() != OcrRecognizingText());

    //图片来源：文件路径非空，或内存图像宽高都大于 0 且有像素
    ocr::OcrRequest empty;
    CHECK_FALSE(ocr::HasImageSource(empty));

    ocr::OcrRequest file;
    file.m_filePath = L"C:\\a.png";
    CHECK(ocr::HasImageSource(file));

    ocr::OcrRequest image;
    image.m_image.m_width = 2;
    image.m_image.m_height = 2;
    CHECK_FALSE(ocr::HasImageSource(image));   //宽高有了但没有像素
    image.m_image.m_pixels.assign(2 * 2 * 4, 0xFF);
    CHECK(ocr::HasImageSource(image));
    image.m_image.m_height = 0;
    CHECK_FALSE(ocr::HasImageSource(image));
}
