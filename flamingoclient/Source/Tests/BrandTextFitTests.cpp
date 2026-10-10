/**
 *  登录窗与注册窗品牌文字的排版单元测试（LB1～LB9、RB1～RB8），BrandTextFitTests.cpp
 *
 *  产品名称可以在 flamingo.ini 里改成任意长度，本组用例检查两个窗口在名称变长时的处理：
 *    · FitBrandTextSize：放得下用设计字号，放不下逐级缩小，缩到下限仍放不下则标记截断；
 *    · 登录窗 AuthBanner：默认文字、设置接口、两行文字整体在头像上方靠左（不显示标语时标题下移）、
 *      文字区不压到右侧火烈鸟图案、默认名称保持原字号；
 *    · 注册窗：banner 品牌文字与「<产品名称> 服务协议」链接随 ApplyRegisterBrand 改写，链接宽度
 *      按文字实际宽度设置，名称很长时链接所在的一行仍排得下。
 *  测量用屏幕 DC，结果随系统字体略有出入，所以断言都写成大小关系而不是精确像素值。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <memory>
#include <string>
#include "../App/BrandText.h"
#include "../UI/BalloonAuth/BrandTextFit.h"
#include "../UI/BalloonAuth/AuthBanner.h"
#include "../UI/BalloonRegister/RegisterLayout.h"
#include "../UI/BalloonRegister/RegisterModel.h"   // kRegStepFillInfo / kRegStepDone
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonwjui;
using namespace balloonauth;
using namespace balloonregister;

namespace {

// 登录窗品牌文字的字体（与 AuthBanner.cpp 绘制时一致）
const wchar_t* const kLoginBrandFont = L"Microsoft YaHei";

// 用例里的设计字号与下限字号，取值与登录窗标题相近即可
const int kDesiredPx = 22;
const int kMinPx     = 13;

// 登录窗 AuthBanner 的设计尺寸：宽同登录窗 380，高为 XML 里 auth-banner 的 fixedHeight 240
const RECT kLoginBannerDesignRect = { 0, 0, 380, 240 };

// 按 AuthBanner.cpp 中火烈鸟的定位公式，设计尺寸下图案最左端约在 x=263（渐变区高 160，
// 图案边长 160×1.15=184，左上角 x=380-184+160×0.12=215.2，路径最左点 52/200×184≈47.8）
const int kLoginFlamingoLeftX = 263;

// 设计尺寸下横幅中间头像白环的左缘与上缘：白环边长 240×72/240=72，水平居中，左缘 x=190-36=154；
// 上缘 y=240×100/240=100。产品名称与标语整体放在头像上方、靠左对齐（用户 2026-10-03 指定），
// 两行的下缘都不得低于头像上缘，横向则不再受头像限制
const int kLoginAvatarLeftX = 154;
const int kLoginAvatarTopY  = 100;

// 注册窗服务协议所在行的右边界：窗口宽 380 减去内容区右内边距 28
const int kRegAgreementRowRight = kRegWindowWidth - 28;

// 「隐私政策」链接的宽度，即 RegisterLayout.cpp 填写页 XML 里它的 fixedWidth
const int kRegPrivacyLinkWidth = 54;

// 注册窗品牌文字整块（产品名称 + 标语）的下缘离横幅下沿的距离：设计稿 design/_decoded/auth.jsx
// 的 AuthShell 里，文字块整体定位为 bottom:18
const int kRegBrandTextBottom = 18;

// 注册窗返回按钮的上缘在横幅下沿以上的距离：设计稿里内容区上内边距 22、返回行 marginTop -32，
// 返回行（即按钮）的上缘比横幅下沿高 32 - 22 = 10
const int kRegBackBtnAboveBanner = 10;

// 品牌文字块下缘与返回按钮上缘之间的竖向间距：18 - 10 = 8
const int kRegBrandToBackBtnGap = kRegBrandTextBottom - kRegBackBtnAboveBanner;

// 按 RegisterLayout.cpp 中火烈鸟的定位公式，设计尺寸下注册窗火烈鸟图案最左端约在 x=266
// （绘制区左上角 x=380-180+20=220，路径最左点 52/200×180=46.8）
const int kRegFlamingoLeftX = 266;

// 一个足够长、在任何字号下都放不下的产品名称
const wchar_t* const kVeryLongTitle =
    L"火烈鸟企业即时通讯协同办公平台火烈鸟企业即时通讯协同办公平台火烈鸟企业即时通讯协同办公平台";

// 一个比默认名称长、但缩小字号后放得下的产品名称
const wchar_t* const kMediumTitle = L"火烈鸟企业即时通讯协同办公";

// 屏幕 DC 的简单 RAII 封装，用例结束时释放
class ScreenDc
{
public:
    ScreenDc() : m_hdc(::GetDC(NULL)) {}
    ~ScreenDc() { ::ReleaseDC(NULL, m_hdc); }
    HDC Get() const { return m_hdc; }

private:
    HDC m_hdc;   // 屏幕 DC，析构时释放
};

// 用控件自己的字体测量链接文字的宽度（像素）
int MeasureLabelTextWidth(HDC hdc, DuiLabel* label)
{
    HFONT hOld = (HFONT)::SelectObject(hdc, label->GetFont());
    const CString text = label->GetText();
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, text.GetLength(), &sz);
    ::SelectObject(hdc, hOld);
    return sz.cx;
}

} // namespace

TEST_CASE("LB1 短文字用设计字号、不截断", "[brandfit]")
{
    ScreenDc dc;
    BrandTextFitResult r = FitBrandTextSize(dc.Get(), L"FlamingoIM", kLoginBrandFont, kDesiredPx, kMinPx, 230, true);
    CHECK(r.m_pixelHeight == kDesiredPx);
    CHECK_FALSE(r.m_truncated);
}

TEST_CASE("LB2 放不下时缩小到放得下的字号，宽度不超过可用宽度", "[brandfit]")
{
    ScreenDc dc;
    const int fullWidth = MeasureBrandText(dc.Get(), kMediumTitle, kLoginBrandFont, kDesiredPx, true).cx;
    REQUIRE(fullWidth > 0);

    //可用宽度取设计字号下宽度的 80%：设计字号放不下，缩小两成左右就放得下
    const int maxWidth = fullWidth * 8 / 10;
    BrandTextFitResult r = FitBrandTextSize(dc.Get(), kMediumTitle, kLoginBrandFont, kDesiredPx, kMinPx, maxWidth, true);
    CHECK_FALSE(r.m_truncated);
    CHECK(r.m_pixelHeight < kDesiredPx);
    CHECK(r.m_pixelHeight >= kMinPx);
    CHECK(MeasureBrandText(dc.Get(), kMediumTitle, kLoginBrandFont, r.m_pixelHeight, true).cx <= maxWidth);
}

TEST_CASE("LB3 缩到下限字号仍放不下时标记截断", "[brandfit]")
{
    ScreenDc dc;
    BrandTextFitResult r = FitBrandTextSize(dc.Get(), kVeryLongTitle, kLoginBrandFont, kDesiredPx, kMinPx, 230, true);
    CHECK(r.m_truncated);
    CHECK(r.m_pixelHeight == kMinPx);
    CHECK(MeasureBrandText(dc.Get(), kVeryLongTitle, kLoginBrandFont, kMinPx, true).cx > 230);
}

TEST_CASE("LB4 边界：空文字、下限大于设计字号", "[brandfit]")
{
    ScreenDc dc;

    BrandTextFitResult empty = FitBrandTextSize(dc.Get(), L"", kLoginBrandFont, kDesiredPx, kMinPx, 10, true);
    CHECK(empty.m_pixelHeight == kDesiredPx);
    CHECK_FALSE(empty.m_truncated);

    BrandTextFitResult nullText = FitBrandTextSize(dc.Get(), NULL, kLoginBrandFont, kDesiredPx, kMinPx, 10, true);
    CHECK(nullText.m_pixelHeight == kDesiredPx);
    CHECK_FALSE(nullText.m_truncated);

    //下限大于设计字号：按不允许缩小处理，放不下就直接截断
    BrandTextFitResult noShrink = FitBrandTextSize(dc.Get(), kVeryLongTitle, kLoginBrandFont, kDesiredPx, kDesiredPx + 5, 230, true);
    CHECK(noShrink.m_pixelHeight == kDesiredPx);
    CHECK(noShrink.m_truncated);
}

TEST_CASE("LB5 AuthBanner 默认显示 FlamingoIM 与内置标语，设置接口按约定处理空值", "[brandfit]")
{
    AuthBanner banner;
    CHECK(banner.GetTitleText() == CString(L"FlamingoIM"));
    CHECK(banner.GetSloganText() == CString(brandtext::kDefaultSlogan));

    banner.SetTitleText(L"火烈鸟办公");
    banner.SetSloganText(L"高效协作");
    CHECK(banner.GetTitleText() == CString(L"火烈鸟办公"));
    CHECK(banner.GetSloganText() == CString(L"高效协作"));

    //产品名称为空时恢复默认；标语为空表示不显示
    banner.SetTitleText(NULL);
    CHECK(banner.GetTitleText() == CString(L"FlamingoIM"));
    banner.SetTitleText(L"");
    CHECK(banner.GetTitleText() == CString(L"FlamingoIM"));
    banner.SetSloganText(L"");
    CHECK(banner.GetSloganText().IsEmpty());
    banner.SetSloganText(NULL);
    CHECK(banner.GetSloganText().IsEmpty());
}

TEST_CASE("LB6 登录窗文字区在头像上方、不压到火烈鸟图案，默认名称保持设计字号，长名称不越界", "[brandfit]")
{
    const RECT area = AuthBanner::ComputeBrandTextArea(kLoginBannerDesignRect);
    CHECK(area.left > kLoginBannerDesignRect.left);
    CHECK(area.right <= kLoginFlamingoLeftX);
    //文字区整体在头像上方，横向不必再避让头像：右缘越过头像左缘、一直延伸到火烈鸟之前
    CHECK(area.bottom < kLoginAvatarTopY);
    CHECK(area.top >= kLoginBannerDesignRect.top);
    CHECK(area.right > kLoginAvatarLeftX);
    CHECK(area.right > area.left);

    ScreenDc dc;
    //单个字母一定放得下，它的字号就是设计字号；默认名称应与之相同、且不截断，即默认外观不变
    BrandTextFitResult letter = AuthBanner::FitTitle(dc.Get(), kLoginBannerDesignRect, L"F");
    BrandTextFitResult def = AuthBanner::FitTitle(dc.Get(), kLoginBannerDesignRect, L"FlamingoIM");
    CHECK(def.m_pixelHeight == letter.m_pixelHeight);
    CHECK_FALSE(def.m_truncated);

    //长名称：要么缩小后放得下，要么标记截断（绘制时以省略号收尾），总之不会画出文字区
    BrandTextFitResult medium = AuthBanner::FitTitle(dc.Get(), kLoginBannerDesignRect, kMediumTitle);
    CHECK(medium.m_pixelHeight <= letter.m_pixelHeight);
    if (!medium.m_truncated)
    {
        CHECK(MeasureBrandText(dc.Get(), kMediumTitle, kLoginBrandFont, medium.m_pixelHeight, true).cx
              <= area.right - area.left);
    }
    BrandTextFitResult longOne = AuthBanner::FitTitle(dc.Get(), kLoginBannerDesignRect, kVeryLongTitle);
    CHECK(longOne.m_truncated);
}

TEST_CASE("LB7 注册窗默认名称保持设计字号，很长的名称标记截断", "[brandfit]")
{
    ScreenDc dc;
    BrandTextFitResult letter = FitRegisterBrandTitle(dc.Get(), kRegWindowWidth, L"F");
    BrandTextFitResult def = FitRegisterBrandTitle(dc.Get(), kRegWindowWidth, L"FlamingoIM");
    CHECK(def.m_pixelHeight == letter.m_pixelHeight);
    CHECK_FALSE(def.m_truncated);

    BrandTextFitResult longOne = FitRegisterBrandTitle(dc.Get(), kRegWindowWidth, kVeryLongTitle);
    CHECK(longOne.m_truncated);
    CHECK(longOne.m_pixelHeight < letter.m_pixelHeight);
}

TEST_CASE("LB8 登录窗产品名称与标语两行都在头像上方、靠左，标题在标语之上", "[brandfit]")
{
    ScreenDc dc;
    RECT titleRc;
    RECT sloganRc;
    AuthBanner::ComputeBrandTextRows(dc.Get(), kLoginBannerDesignRect, L"FlamingoIM",
                                     brandtext::kDefaultSlogan, titleRc, sloganRc);
    const RECT area = AuthBanner::ComputeBrandTextArea(kLoginBannerDesignRect);

    INFO("title = [" << titleRc.top << ", " << titleRc.bottom << "], slogan = ["
         << sloganRc.top << ", " << sloganRc.bottom << "], area bottom = " << area.bottom);
    //两行都靠左，从文字区左缘起排
    CHECK(titleRc.left == area.left);
    CHECK(sloganRc.left == area.left);
    //标语贴文字区下缘，下缘高于头像上缘
    CHECK(sloganRc.bottom == area.bottom);
    CHECK(sloganRc.bottom < kLoginAvatarTopY);
    //标题在标语之上，两行不重叠，也不越出横幅上缘
    CHECK(titleRc.bottom <= sloganRc.top);
    CHECK(titleRc.top >= kLoginBannerDesignRect.top);
    CHECK(titleRc.bottom > titleRc.top);
    CHECK(sloganRc.bottom > sloganRc.top);
}

TEST_CASE("LB9 登录窗不显示标语时，产品名称下移到原标语的位置", "[brandfit]")
{
    ScreenDc dc;
    RECT titleRc;
    RECT sloganRc;
    AuthBanner::ComputeBrandTextRows(dc.Get(), kLoginBannerDesignRect, L"FlamingoIM", L"", titleRc, sloganRc);
    const RECT area = AuthBanner::ComputeBrandTextArea(kLoginBannerDesignRect);

    //没有标语：标题贴文字区下缘，紧挨头像上方，不在上方悬空
    CHECK(titleRc.bottom == area.bottom);
    CHECK(titleRc.bottom < kLoginAvatarTopY);
    //标语行为空矩形，不占位置
    CHECK(sloganRc.bottom == sloganRc.top);
}

TEST_CASE("RB1 注册窗默认显示 FlamingoIM、内置标语与「FlamingoIM 服务协议」", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    CString title;
    CString slogan;
    REQUIRE(GetRegisterBrandText(root.get(), title, slogan));
    CHECK(title == CString(L"FlamingoIM"));
    CHECK(slogan == CString(brandtext::kDefaultSlogan));

    DuiLabel* agreement = dynamic_cast<DuiLabel*>(root->FindCtrlById(kRegIdLinkAgreement));
    REQUIRE(agreement != nullptr);
    CHECK(agreement->GetText() == CString(L"FlamingoIM 服务协议"));
}

TEST_CASE("RB2 ApplyRegisterBrand 改写品牌文字与协议链接，链接宽度容得下文字", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    REQUIRE(ApplyRegisterBrand(root.get(), L"火烈鸟办公", L"高效协作"));

    CString title;
    CString slogan;
    REQUIRE(GetRegisterBrandText(root.get(), title, slogan));
    CHECK(title == CString(L"火烈鸟办公"));
    CHECK(slogan == CString(L"高效协作"));

    DuiLabel* agreement = dynamic_cast<DuiLabel*>(root->FindCtrlById(kRegIdLinkAgreement));
    REQUIRE(agreement != nullptr);
    CHECK(agreement->GetText() == CString(L"火烈鸟办公 服务协议"));

    //按设计尺寸排版后，链接的宽度不小于文字宽度，文字不会被截断
    RECT rc = { 0, 0, kRegWindowWidth, kRegWindowHeight - kRegWindowTitleBarH };
    root->ForceLayout(rc);
    ScreenDc dc;
    const RECT rcLink = agreement->GetRect();
    CHECK(rcLink.right - rcLink.left >= MeasureLabelTextWidth(dc.Get(), agreement));
}

TEST_CASE("RB3 名称很长时协议链接一行仍排得下，「隐私政策」不越出内容区", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    REQUIRE(ApplyRegisterBrand(root.get(), kVeryLongTitle, L""));

    RECT rc = { 0, 0, kRegWindowWidth, kRegWindowHeight - kRegWindowTitleBarH };
    root->ForceLayout(rc);

    DuiControl* agreement = root->FindCtrlById(kRegIdLinkAgreement);
    DuiControl* privacy = root->FindCtrlById(kRegIdLinkPrivacy);
    REQUIRE(agreement != nullptr);
    REQUIRE(privacy != nullptr);

    const RECT rcAgreement = agreement->GetRect();
    const RECT rcPrivacy = privacy->GetRect();
    INFO("agreement = [" << rcAgreement.left << ", " << rcAgreement.right << "], privacy = ["
         << rcPrivacy.left << ", " << rcPrivacy.right << "]");
    CHECK(rcAgreement.right <= rcPrivacy.left);
    CHECK(rcPrivacy.right <= kRegAgreementRowRight);
    //「隐私政策」保持原来的宽度，没有被挤压
    CHECK(rcPrivacy.right - rcPrivacy.left == kRegPrivacyLinkWidth);

    //标语为空表示不显示
    CString title;
    CString slogan;
    REQUIRE(GetRegisterBrandText(root.get(), title, slogan));
    CHECK(slogan.IsEmpty());
}

// 按设计尺寸排版注册窗某一步的控件树，取返回按钮与品牌文字两行的矩形（RB5～RB7 共用）。
//   root：BuildRegisterBody 的结果；rcBack / rcTitle / rcSlogan / bannerBottom：出参。
static void LayoutRegisterBanner(DuiControl* root, RECT& rcBack, RECT& rcTitle, RECT& rcSlogan,
                                 int& bannerBottom)
{
    RECT rc = { 0, 0, kRegWindowWidth, kRegWindowHeight - kRegWindowTitleBarH };
    root->ForceLayout(rc);

    DuiControl* back = root->FindCtrlById(kRegIdBackBtn);
    REQUIRE(back != nullptr);
    rcBack = back->GetRect();
    REQUIRE(GetRegisterBrandRows(root, rcTitle, rcSlogan, bannerBottom));
}

TEST_CASE("RB4 完成页没有协议链接，ApplyRegisterBrand 同样改写 banner", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepDone);
    REQUIRE(root != nullptr);
    CHECK(ApplyRegisterBrand(root.get(), L"火烈鸟办公", NULL));

    CString title;
    CString slogan;
    REQUIRE(GetRegisterBrandText(root.get(), title, slogan));
    CHECK(title == CString(L"火烈鸟办公"));
    CHECK(slogan.IsEmpty());

    //传空的产品名称时按 FlamingoIM 处理
    CHECK(ApplyRegisterBrand(root.get(), L"", L"x"));
    REQUIRE(GetRegisterBrandText(root.get(), title, slogan));
    CHECK(title == CString(L"FlamingoIM"));

    CHECK_FALSE(ApplyRegisterBrand(NULL, L"x", L"y"));
}

TEST_CASE("RB5 注册窗默认文字时标语下缘离横幅下沿 18 像素、产品名称在其上，两行都在返回按钮之上并相隔 8 像素", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    RECT rcBack;
    RECT rcTitle;
    RECT rcSlogan;
    int bannerBottom = 0;
    LayoutRegisterBanner(root.get(), rcBack, rcTitle, rcSlogan, bannerBottom);
    INFO("banner bottom = " << bannerBottom << ", back = [" << rcBack.top << ", " << rcBack.bottom
         << "], title = [" << rcTitle.top << ", " << rcTitle.bottom << "], slogan = ["
         << rcSlogan.top << ", " << rcSlogan.bottom << "]");

    //返回按钮的位置与设计稿一致：上缘在横幅下沿以上 10 像素（本次修复不动它）
    CHECK(rcBack.top == bannerBottom - kRegBackBtnAboveBanner);

    //文字块整体下缘离横幅下沿 18 像素，产品名称在标语之上
    CHECK(rcSlogan.bottom > rcSlogan.top);
    CHECK(rcSlogan.bottom == bannerBottom - kRegBrandTextBottom);
    CHECK(rcTitle.bottom <= rcSlogan.top);

    //两行都在返回按钮上缘之上，标语与按钮之间留出设计稿的 8 像素
    CHECK(rcBack.top - rcSlogan.bottom == kRegBrandToBackBtnGap);
    CHECK(rcTitle.bottom < rcBack.top);
}

TEST_CASE("RB6 注册窗不显示标语时产品名称下移到标语的位置，仍在返回按钮之上", "[brandfit][register]")
{
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    REQUIRE(ApplyRegisterBrand(root.get(), L"FlamingoIM", L""));

    RECT rcBack;
    RECT rcTitle;
    RECT rcSlogan;
    int bannerBottom = 0;
    LayoutRegisterBanner(root.get(), rcBack, rcTitle, rcSlogan, bannerBottom);
    INFO("banner bottom = " << bannerBottom << ", back = [" << rcBack.top << ", " << rcBack.bottom
         << "], title = [" << rcTitle.top << ", " << rcTitle.bottom << "]");

    //标语行为空矩形，不占位置；产品名称的下缘就是文字块的下缘
    CHECK(rcSlogan.bottom == rcSlogan.top);
    CHECK(rcTitle.bottom == bannerBottom - kRegBrandTextBottom);
    CHECK(rcBack.top - rcTitle.bottom == kRegBrandToBackBtnGap);
}

TEST_CASE("RB7 产品名称与标语都很长时，两行仍在返回按钮之上、右缘不越过火烈鸟图案", "[brandfit][register]")
{
    //完成页的返回行与填写页相同，这里顺带覆盖完成页
    std::unique_ptr<DuiControl> root = BuildRegisterBody(kRegStepDone);
    REQUIRE(root != nullptr);
    REQUIRE(ApplyRegisterBrand(root.get(), kVeryLongTitle, kVeryLongTitle));

    RECT rcBack;
    RECT rcTitle;
    RECT rcSlogan;
    int bannerBottom = 0;
    LayoutRegisterBanner(root.get(), rcBack, rcTitle, rcSlogan, bannerBottom);
    INFO("banner bottom = " << bannerBottom << ", back = [" << rcBack.top << ", " << rcBack.bottom
         << "], title = [" << rcTitle.top << ", " << rcTitle.bottom << "], slogan = ["
         << rcSlogan.top << ", " << rcSlogan.bottom << "], right = " << rcTitle.right);

    //文字很长时字号缩小、末尾截断成省略号，但两行的矩形不变，仍在返回按钮之上
    CHECK(rcSlogan.bottom == bannerBottom - kRegBrandTextBottom);
    CHECK(rcSlogan.bottom < rcBack.top);
    CHECK(rcTitle.bottom <= rcSlogan.top);
    //两行左对齐、右缘相同，都止于火烈鸟图案之前
    CHECK(rcTitle.left == rcSlogan.left);
    CHECK(rcTitle.right == rcSlogan.right);
    CHECK(rcTitle.right <= kRegFlamingoLeftX);
}

TEST_CASE("RB8 返回按钮右侧的「注册新账号」落在横幅下方的白底上，用深色字", "[brandfit][register]")
{
    const int kSteps[] = { kRegStepFillInfo, kRegStepDone };
    for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); ++i)
    {
        std::unique_ptr<DuiControl> root = BuildRegisterBody(kSteps[i]);
        REQUIRE(root != nullptr);
        RECT rcBack;
        RECT rcTitle;
        RECT rcSlogan;
        int bannerBottom = 0;
        LayoutRegisterBanner(root.get(), rcBack, rcTitle, rcSlogan, bannerBottom);

        DuiControl* backTitle = root->FindCtrlById(kRegIdBackTitle);
        REQUIRE(backTitle != nullptr);
        const RECT rcText = backTitle->GetRect();
        INFO("step = " << kSteps[i] << ", banner bottom = " << bannerBottom << ", title = [" << rcText.top
             << ", " << rcText.bottom << "]");
        //文字竖直居中在返回行里，中心在横幅下沿之下，即大部分落在白底上
        CHECK((rcText.top + rcText.bottom) / 2 > bannerBottom);

        //所以文字须是深色：三个分量都不超过一半亮度（白字时三个分量都是 255）
        COLORREF color = RGB(255, 255, 255);
        REQUIRE(GetRegisterTextColor(root.get(), kRegIdBackTitle, color));
        const int kHalfBrightness = 128;
        CHECK(GetRValue(color) < kHalfBrightness);
        CHECK(GetGValue(color) < kHalfBrightness);
        CHECK(GetBValue(color) < kHalfBrightness);
    }
}
