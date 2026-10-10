/**
 *  帖子群 DLL 发帖窗口的布局单元测试（标签 [threaddll]，源码编进 FlamingoUnit）：
 *    - TGD13 发帖窗口按设计尺寸排得下：三种模式、已 @ 名单行显示与否，底部按钮不被压扁、下方留白不变，
 *            图片行与名单行保持原高，正文输入框不低于最矮高度；图片行的格子在张数多时等分缩小。
 *  控件树由 BuildComposeBody 单独建出（不建窗口），按 layoutfit::MeasureAt 的办法检查（UI 约定第 7 条）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>

#include "../../Source/Tests/LayoutFitCheck.h"   // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../UI/ThreadCommentEdit.h"
#include "../UI/ThreadComposeLayout.h"

using namespace threadgroup;

namespace {

const int kDefaultDpi = 96;   //按 100% 缩放取界面默认字体

//矩形的高
int HeightOf(const RECT& rc)
{
    return rc.bottom - rc.top;
}

} // namespace

TEST_CASE("TGD13 发帖窗口按设计尺寸排得下", "[threaddll]")
{
    HFONT font = balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(kDefaultDpi);
    const int modes[] = { compose_mode_new_post, compose_mode_edit_post, compose_mode_addendum };
    const int modeCount = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
    for (int m = 0; m < modeCount; ++m)
    {
        for (int withMentions = 0; withMentions <= 1; ++withMentions)
        {
            INFO("mode = " << modes[m] << ", mention row shown = " << withMentions);
            ComposeControls controls;
            std::unique_ptr<balloonwjui::DuiVBox> root = BuildComposeBody(modes[m], font, controls);
            REQUIRE(root != nullptr);
            REQUIRE(controls.m_submit != NULL);
            REQUIRE(controls.m_body != NULL);
            REQUIRE(controls.m_images != NULL);
            REQUIRE(controls.m_mentions != NULL);
            //追加补充没有标题框、有说明行；其余模式反过来
            CHECK((controls.m_title == NULL) == (modes[m] == compose_mode_addendum));
            CHECK((controls.m_hint != NULL) == (modes[m] == compose_mode_addendum));
            if (withMentions != 0)
                controls.m_mentions->SetVisible(true);

            const int contentH = ComposeWindowHeight(modes[m]) - kComposeTitleBarHeight;
            const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), kIdComposeSubmit, kComposeWindowWidth, contentH);
            REQUIRE(r.m_found);
            INFO("button bottom = " << r.m_bottom << ", content height = " << contentH << ", height = " << r.m_height
                 << ", roomy height = " << r.m_roomyHeight << ", bottom gap = " << r.m_bottomGap
                 << ", roomy bottom gap = " << r.m_roomyBottomGap);
            CHECK(r.m_bottom <= contentH);
            CHECK(r.m_height == r.m_roomyHeight);
            CHECK(r.m_bottomGap == r.m_roomyBottomGap);

            //空间不够时被挤压的是正文输入框（weight 1）：图片行与名单行保持原高，正文不低于最矮高度
            const int bodyH = HeightOf(controls.m_body->GetRect());
            INFO("body height = " << bodyH);
            CHECK(bodyH >= kComposeMinBodyHeight);
            CHECK(HeightOf(controls.m_images->GetRect()) == kComposeImageRowHeight);
            if (withMentions != 0)
                CHECK(HeightOf(controls.m_mentions->GetRect()) == kComposeMentionRowHeight);
        }
    }

    //图片行的格子：放得下时为 76，九张图加「＋」时等分缩小
    const int rowWidth = kComposeWindowWidth - 2 * kComposePadX;
    const int fewTiles = 6;
    const int manyTiles = 10;
    CHECK(CThreadComposeImageRow::TileSize(1, rowWidth) == kComposeImageRowHeight);
    CHECK(CThreadComposeImageRow::TileSize(fewTiles, rowWidth) == kComposeImageRowHeight);
    const int smallTile = CThreadComposeImageRow::TileSize(manyTiles, rowWidth);
    CHECK(smallTile < kComposeImageRowHeight);
    CHECK(smallTile * manyTiles <= rowWidth);
}
