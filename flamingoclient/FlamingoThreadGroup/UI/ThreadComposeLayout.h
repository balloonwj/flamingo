/**
 *  发帖窗口的控件树（docs/帖子群方案.md 4.6，布局以第 0 期样稿 tmp/threadgroup/preview.html 的「② 发帖窗口」为准）。
 *  发帖、编辑帖子与追加补充共用一套排版，只在有没有标题框、说明行和按钮文字上不同。从上到下：
 *    - 追加补充：一行说明「补充会显示在帖子正文之后……」；
 *    - 发帖与编辑：标题输入框（右侧内嵌字数「0 / 60」）；
 *    - 正文输入框（Enter 换行、Ctrl+Enter 发布，表情显示为「[表情名]」）；
 *    - 图片行：已选图片的缩略图（右上角 ✕ 删除，上传中、上传失败叠一层说明）与末尾的「＋ 图片 n/9」；
 *    - 工具行：「表情」「图片」「@ 成员」，右侧字数；
 *    - 已 @ 名单行：「@李四 ✕」，没有 @ 人时隐藏；
 *    - 底部一行：出错说明（红字）、「取消」「发布 / 保存 / 补充」。
 *  控件树与窗口分开建，便于单元测试按设计尺寸排版（TGD13）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      ComposeControls controls;
 *      std::unique_ptr<balloonwjui::DuiVBox> body = threadgroup::BuildComposeBody(compose_mode_new_post, font, controls);
 *      root->AddChild(std::move(body), ...);
 *      controls.m_images->Rebuild(tiles, true, addText, font);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonwjui {
class DuiButton;
class DuiEdit;
class DuiLabel;
}

namespace threadgroup {

class CThreadCommentEdit;

//发帖窗口的三种模式
enum ComposeMode
{
    compose_mode_new_post  = 1,   //发新帖：有标题框，按钮「发布」
    compose_mode_edit_post = 2,   //编辑自己的帖子：有标题框，带入原帖内容，按钮「保存」
    compose_mode_addendum  = 3,   //楼主追加补充：没有标题框，上方一行说明，按钮「补充」
};

//---- 窗口尺寸，单位：像素（样稿第 0 期；DuiFrameWindow 的整窗尺寸，含标题栏，UI 约定第 7 条） ----
const int kComposeWindowWidth = 560;       //窗口宽
const int kComposePostHeight = 492;        //发帖与编辑帖子时的窗口高
const int kComposeAddendumHeight = 400;    //追加补充时的窗口高
const int kComposeTitleBarHeight = 32;     //标题栏高
const int kComposePadX = 18;               //内容区左右内边距
const int kComposeMinBodyHeight = 80;      //正文输入框最矮的高度（图片与名单行都占满时也不低于它）
const int kComposeImageRowHeight = 76;     //图片行高，也是缩略图与「＋ 图片」格子最大的边长
const int kComposeMentionRowHeight = 24;   //已 @ 名单行高

//---- 控件 id ----
const UINT kIdComposeTitle = 7801;         //标题输入框（DUIN_VALUECHANGED：刷新标题字数）
const UINT kIdComposeBody = 7802;          //正文输入框（DUIN_VALUECHANGED：刷新正文字数）
const UINT kIdComposeFace = 7803;          //「表情」
const UINT kIdComposeImage = 7804;         //「图片」
const UINT kIdComposeMention = 7805;       //「@ 成员」
const UINT kIdComposeCancel = 7806;        //「取消」
const UINT kIdComposeSubmit = 7807;        //「发布」「保存」「补充」
const UINT kIdComposeAddImage = 7808;      //图片行末尾的「＋ 图片 n/9」
const UINT kIdComposeThumbBase = 7820;     //图片行第 i 张缩略图为 kIdComposeThumbBase + i（DUIN_CLICK 的 extra 见 ComposeThumbClick）
const UINT kIdComposeThumbLast = 7829;     //缩略图 id 的上限（最多 9 张）
const UINT kIdComposeMentionBase = 7840;   //已 @ 名单第 i 个标签为 kIdComposeMentionBase + i（单击即从名单里去掉）
const UINT kIdComposeMentionLast = 7899;   //名单标签 id 的上限（最多 50 人，留出余量）

//缩略图 DUIN_CLICK 通知的 extra：点在了哪里
enum ComposeThumbClick
{
    compose_thumb_body   = 0,   //点了图本身：上传失败的图重新上传
    compose_thumb_remove = 1,   //点了右上角的 ✕：删除这张图
};

//图片行里一张缩略图要显示的内容
struct ComposeTileView
{
    HBITMAP m_bitmap;   //缩略图位图（借用，调用方保证比控件存活久）；还没有时为 NULL，画占位底色
    int     m_state;    //状态，取值见 ComposeImageState（ThreadComposeModel.h）

    ComposeTileView()
        : m_bitmap(NULL)
        , m_state(0)
    {
    }
};

/**
 *  图片行：已选图片的缩略图逐张排开，末尾是「＋ 图片 n/9」。张数多时缩小缩略图，始终排在一行里（窗口是固定尺寸）。
 */
class CThreadComposeImageRow : public balloonwjui::DuiHBox
{
public:
    CThreadComposeImageRow();

    /**
     *  按图片列表重建这一行。
     *  @param tiles    各张缩略图，按显示顺序（最多 kMaxImages 张）
     *  @param showAdd  是否显示末尾的「＋ 图片」（图片满了时不显示）
     *  @param addText  「＋ 图片」下面那行文字，如「图片 3/9」
     *  @param font     文字字体（借用）
     *  @param rowWidth 这一行的宽度，单位：像素（窗口宽减两侧内边距）
     */
    void Rebuild(const std::vector<ComposeTileView>& tiles, bool showAdd, const std::wstring& addText, HFONT font,
                 int rowWidth);

    /**
     *  一行放 count 个格子时每个格子的边长：放得下时为样稿的 76 像素，放不下时等分缩小。
     *  @param count    格子数（缩略图加上「＋ 图片」）
     *  @param rowWidth 这一行的宽度，单位：像素
     */
    static int TileSize(int count, int rowWidth);
};

/**
 *  已 @ 名单行：「@名字 ✕」标签逐个排开，单击即去掉；放不下的写成「等 N 人」。
 */
class CThreadMentionRow : public balloonwjui::DuiHBox
{
public:
    CThreadMentionRow();

    /**
     *  按名单重建这一行。
     *  @param names    @ 的人的显示名，按加入顺序
     *  @param font     文字字体（借用）
     *  @param rowWidth 这一行的宽度，单位：像素
     */
    void Rebuild(const std::vector<std::wstring>& names, HFONT font, int rowWidth);
};

//建好的控件树里窗口要用到的控件（都归控件树所有，借用）
struct ComposeControls
{
    balloonwjui::DuiLabel*      m_hint;      //追加补充时最上面的说明；其余模式为 NULL
    balloonwjui::DuiEdit*       m_title;     //标题输入框；追加补充时为 NULL
    CThreadCommentEdit*         m_body;      //正文输入框
    CThreadComposeImageRow*     m_images;    //图片行
    balloonwjui::DuiButton*     m_btnFace;   //「表情」
    balloonwjui::DuiButton*     m_btnImage;  //「图片」
    balloonwjui::DuiButton*     m_btnMention;//「@ 成员」
    balloonwjui::DuiLabel*      m_count;     //工具行右侧的正文字数
    CThreadMentionRow*          m_mentions;  //已 @ 名单行（没有 @ 人时隐藏）
    balloonwjui::DuiLabel*      m_error;     //底部一行左侧的出错说明
    balloonwjui::DuiButton*     m_cancel;    //「取消」
    balloonwjui::DuiButton*     m_submit;    //「发布」「保存」「补充」

    ComposeControls()
        : m_hint(NULL)
        , m_title(NULL)
        , m_body(NULL)
        , m_images(NULL)
        , m_btnFace(NULL)
        , m_btnImage(NULL)
        , m_btnMention(NULL)
        , m_count(NULL)
        , m_mentions(NULL)
        , m_error(NULL)
        , m_cancel(NULL)
        , m_submit(NULL)
    {
    }
};

/**
 *  建发帖窗口标题栏以下的控件树。
 *  @param mode 模式，取值见 ComposeMode
 *  @param font 文字字体（借用）；为 NULL 时按默认字体测量宽度（单元测试）
 *  @param out  [出参] 窗口要用到的控件
 *  @return 控件树的根（内容区，宽为 kComposeWindowWidth、高为窗口高减标题栏高）
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildComposeBody(int mode, HFONT font, ComposeControls& out);

/**
 *  某种模式的窗口高（整窗，含标题栏）。
 *  @param mode 模式，取值见 ComposeMode
 */
int ComposeWindowHeight(int mode);

/**
 *  底部主按钮的文字：「发布」「保存」「补充」。
 *  @param mode 模式，取值见 ComposeMode
 */
std::wstring ComposeSubmitText(int mode);

/**
 *  工具行右侧的字数文字：发帖与编辑为「62 / 5000」，追加补充为「0 / 2000 · 第 2 段，最多 20 段」。
 *  @param mode          模式，取值见 ComposeMode
 *  @param count         正文当前的字数（线上正文的码点数）
 *  @param addendumIndex 追加补充时这是第几段（1 起）；其余模式忽略
 */
std::wstring ComposeCountText(int mode, int count, int addendumIndex);

} // namespace threadgroup
