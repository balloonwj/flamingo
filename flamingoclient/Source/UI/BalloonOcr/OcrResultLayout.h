/**
 *  OcrResultLayout.h
 *
 *  「图片文字识别」结果窗口（OcrResultWindow）客户区控件树的构造接口、窗口尺寸与状态文字。控件树单独成文，
 *  是为了让布局单测在不建窗口的情况下按设计尺寸排版、检查底部按钮排得下（开发约定文档 UI 约定第 7 条）。
 *  窗口代码按下面的 OcrResultCtrlId 取回需要操作的控件。
 *
 *  版式（自上而下）：「识别语言」小标题 + 下拉框 + 右侧状态文字一行；可编辑、可选取的识别结果文字框
 *  （占满余下的高度，内容多时出现悬浮滚动条）；底部按钮行：左侧「打开图片」，右侧「重新识别 / 复制全部」。
 *
 *  典型用法（窗口建好之后）：
 *      SetTitleBarHeight(balloonocr::kOcrResultTitleBarH);
 *      SetClientContent(balloonocr::BuildOcrResultBody());
 *      ResizeClient(balloonocr::kOcrResultWindowWidth, balloonocr::kOcrResultWindowHeight);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include <string>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonocr {

//窗口整窗尺寸（逻辑像素，含标题栏；DuiFrameWindow 的 ResizeClient 设的就是整窗，见开发约定文档 UI 约定第 7 条）
const int kOcrResultWindowWidth  = 560;
const int kOcrResultWindowHeight = 480;

//可缩放时的最小整窗尺寸（逻辑像素，含标题栏）
const int kOcrResultMinWidth  = 420;
const int kOcrResultMinHeight = 320;

//标题栏高度（逻辑像素），在上面的整窗高度之内；内容区高度 = 整窗高度 - 本值
const int kOcrResultTitleBarH = 32;

/**
 *  结果窗口客户区控件的数字 id，窗口内唯一。从 101 起编：DuiFrameWindow 标题栏的最小化 / 最大化 / 关闭按钮
 *  固定使用 1 / 2 / 3，它们的点击通知与客户区控件走同一条 WM_DUI_NOTIFY（布局单测检查不撞号）。
 */
enum OcrResultCtrlId
{
    kOcrIdLanguageCaption = 101,   //「识别语言」小标题（DuiLabel）
    kOcrIdLanguage        = 102,   //识别语言下拉框（DuiComboBox，只读）；换一种语言即重新识别
    kOcrIdStatus          = 103,   //状态文字（DuiLabel）：正在识别、共几行、失败原因、已复制
    kOcrIdText            = 104,   //识别结果文字框（DuiRichEdit，多行、自动换行、可编辑）
    kOcrIdRetry           = 105,   //「重新识别」按钮
    kOcrIdCopy            = 106,   //「复制全部」按钮
    kOcrIdOpen            = 107,   //「打开图片」按钮（底部按钮行左侧）：选一张本地图片识别；系统菜单打开的空窗口靠它选图
};

/**
 *  构造结果窗口的客户区控件树。下拉框为空，由窗口填入可用的识别语言。
 *  @return 客户区根控件，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildOcrResultBody();

/**
 *  一次识别结束后状态行显示的文字（纯函数，配单元测试 OCRW3）。
 *  @param code      结果码：flamingoocr::OcrResultCode 或 ocr::OcrTaskCode
 *  @param lineCount 识别出的行数
 *  @return 状态文字（已按界面语言翻译）
 */
std::wstring OcrResultStatusText(int code, size_t lineCount);

/**
 *  正在识别时状态行显示的文字。
 */
std::wstring OcrRecognizingText();

/**
 *  窗口里还没有图片时（从系统菜单打开）状态行显示的文字。
 */
std::wstring OcrNoImageText();

/**
 *  把文字框里取出的文字整理成适合放进剪贴板的形式：单独的 \r 或 \n 一律换成 \r\n（RichEdit 内部用 \r 分段）。
 *  @param text 文字框里取出的文字
 *  @return 整理后的文字
 */
std::wstring NormalizeLineBreaks(const std::wstring& text);

} // namespace balloonocr
