/**
 *  AuthBanner.h
 *
 *  登录 / 注册窗顶部的橙色品牌 Banner + 头像区 —— balloonui 纯绘制控件。
 *
 *  视觉对应 design/login-screen.png：
 *   · 控件矩形上部为 135° 线性渐变 #FFB793 → #FF7A45 区；
 *   · 渐变区右侧一只白色描边的火烈鸟轮廓（约 0.85 不透明）；
 *   · 头像上方左侧两行文字：产品名称标题 + 标语（默认 "FlamingoIM" 与 "让沟通像火烈鸟一样
 *     优雅"，登录窗按 flamingo.ini 的配置改写；名称过长时先缩小字号，仍放不下再截断成省略号）。
 *     2026-10-03 起整体从渐变区左下角上移到头像上方，与头像纵向不再重叠；
 *   · 下部居中一枚"白色圆环 + 橙色圆角头像 + 白色首字母"，上移压住
 *     渐变区下沿；
 *   · 头像下方居中一行用户名 + 一颗"在线"状态胶囊（2026-10-04 起带细边框与下拉小三角，
 *     鼠标悬停时底色加深、光标变手形并提示「点击切换登录状态」）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "BrandTextFit.h"                 // BrandTextFitResult

#include <memory>   // std::unique_ptr：头像照片
#include <string>

namespace Gdiplus {
class Bitmap;
}

namespace balloonauth {

/**
 *  AuthBanner
 *
 *  品牌 + 头像区绘制控件。所有尺寸均按控件自身矩形（DuiControl::m_rcItem）
 *  等比推算，自适应 DPI 与窗口尺寸。头像下方的"在线状态胶囊"<u>可点击</u>：
 *  命中胶囊时发 DUIN_BANNER_STATUS_CLICK 通知，登录窗据此弹出在线状态菜单
 *  （在线 / 离开 / 忙碌），与主面板状态胶囊同套交互。
 */
class AuthBanner : public balloonwjui::DuiControl
{
public:
    // 头像下方"在线状态胶囊"被点击时发给父 host 的通知码 —— 登录窗据此弹出在线状态
    // 选择菜单。取 DUIN_CUSTOM 之后的值，避免与 balloonui 内置通知码冲突。
    enum { DUIN_BANNER_STATUS_CLICK = balloonwjui::DUIN_CUSTOM + 1 };

    AuthBanner();

    // 析构：释放头像照片（GDI+ 位图的完整类型只在 .cpp 里可见，析构函数因此定义在 .cpp）
    ~AuthBanner();

    /**
     *  设置头像方块里画什么（2026-10-04 起，登录窗按当前账号的头像调用，见 LoginAccountAvatar.h）。
     *  照片在本机且能加载时用照片铺满圆角方块；否则按底色画方块、白色字形居中。调用过本方法之后，
     *  头像里的字形取 glyph，不再取用户名首字。
     *    glyph：    字形（1~2 个字符，可含 emoji）；NULL 或空串表示不画字形（输入框里没有账号时）。
     *    color：    方块底色（同时是照片加载失败时的底色）。
     *    photoPath：照片在本机的路径；NULL 或空串、或加载失败时画字形。与上次相同时不重新加载。
     */
    void SetAvatar(LPCTSTR glyph, COLORREF color, LPCWSTR photoPath);

    // 头像方块当前是否画照片
    bool HasAvatarPhoto() const;

    // 头像方块的底色
    COLORREF GetAvatarColor() const { return m_avatarColor; }

    // 头像方块的矩形（host 客户区坐标，由 OnPaint 按实际绘制位置缓存）；尚未绘制时为空矩形
    const RECT& GetAvatarRect() const { return m_rcAvatar; }

    // 设置头像下方显示的用户名 —— 同时决定头像里的占位字母（取用户名
    // 第一个非空字符）。空字符串表示当前无账号，头像画通用人形占位、
    // 不显示用户名。
    //   name：用户名 / 账号文本；NULL 视为空。
    void SetUserName(LPCTSTR name);

    // 设置头像方块的底色 —— 登录窗用它表达"已登录账号按账号稳定取色、
    // 未登录账号用默认橙色"。仅改底色，首字母 / 白环 / 阴影等画法不变。
    //   color：头像方块填充色（RGB）。默认构造为主题橙 RGB(0xFF,0x7A,0x45)。
    void SetAvatarColor(COLORREF color);

    // 设置状态胶囊显示的在线状态（v2 presence_type，见 net/Protocol.h），决定胶囊
    // 圆点颜色与文字（在线绿 / 离开橙 / 忙碌红）。登录窗在用户从状态菜单选中后调用，
    // 随后须 Invalidate 重绘。默认在线。
    //   presence：v2 在线状态。
    void SetPresence(int presence);

    // 状态胶囊命中矩形（host 客户区坐标，由 OnPaint 按实际绘制位置缓存）。供登录窗把
    // 状态菜单锚到胶囊正下方。空矩形(全 0)表示尚未绘制。
    const RECT& GetPillHitRect() const { return m_rcPillHit; }

    // 设置渐变区左下角的产品名称。登录窗创建后用 g_strAppTitle 调用；不调用时为 FlamingoIM。
    //   title：产品名称；NULL 或空串时恢复为 FlamingoIM。
    void SetTitleText(LPCTSTR title);

    // 取当前的产品名称。
    const CString& GetTitleText() const { return m_title; }

    // 设置产品名称下方的标语。登录窗创建后用 g_strAppSlogan 调用；不调用时为内置标语。
    //   slogan：标语；NULL 或空串表示不显示标语。
    void SetSloganText(LPCTSTR slogan);

    // 取当前的标语；空串表示不显示。
    const CString& GetSloganText() const { return m_slogan; }

    /**
     *  计算产品名称与标语可用的文字区域。两行文字整体放在头像上方、靠左对齐：左缘为文字
     *  左边距；右缘止于火烈鸟图案最左侧，再留出一段间距；上缘为控件上缘；下缘在头像白环
     *  上缘之上留出一段间距。文字与头像纵向不重叠，所以横向不必避让头像。
     *  @param rcItem 本控件的矩形（host 客户区坐标）。
     *  @return 文字区域（同一坐标系）；rcItem 为空时返回空矩形。
     */
    static RECT ComputeBrandTextArea(const RECT& rcItem);

    /**
     *  计算产品名称与标语两行的绘制矩形。标语贴文字区下缘，标题在标语之上，两行间距与
     *  设计稿一致；标语为空时标题下移、贴文字区下缘。两行左右边界都取文字区的左右边界。
     *  @param hdc      用于测量行高的 DC。
     *  @param rcItem   本控件的矩形（host 客户区坐标）。
     *  @param title    产品名称，用于测量标题行高。
     *  @param slogan   标语；NULL 或空串表示不显示，此时 sloganRc 为高度 0 的矩形。
     *  @param titleRc  [出参] 标题行矩形，文字在其中贴底绘制。
     *  @param sloganRc [出参] 标语行矩形，文字在其中贴顶绘制。
     */
    static void ComputeBrandTextRows(HDC hdc, const RECT& rcItem, LPCWSTR title, LPCWSTR slogan,
                                     RECT& titleRc, RECT& sloganRc);

    /**
     *  按控件矩形计算产品名称应采用的字号：放得下时用设计字号，放不下时逐级缩小，
     *  缩到下限仍放不下则标记为需要截断。
     *  @param hdc    用于测量的 DC。
     *  @param rcItem 本控件的矩形。
     *  @param title  产品名称。
     *  @return 字号与是否截断。
     */
    static BrandTextFitResult FitTitle(HDC hdc, const RECT& rcItem, LPCWSTR title);

    // DuiControl 覆写：绘制渐变背景 + 火烈鸟 + 标题 + 头像 + 用户名 +
    // 状态胶囊。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：鼠标抬起时，命中状态胶囊则发 DUIN_BANNER_STATUS_CLICK
    // 触发"弹出在线状态菜单"；其余区域不触发。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写（2026-10-04 起）：鼠标移动时判断是否在状态胶囊上，进出胶囊时切换悬停态
    // （底色加深、登记 / 注销悬停提示「点击切换登录状态」）。
    //   pt：host 客户区坐标；mkFlags：按键状态。返回值同基类。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写（2026-10-04 起）：鼠标离开本控件时取消胶囊的悬停态。返回值同基类。
    bool OnMouseLeave() override;

    // DuiControl 覆写（2026-10-04 起）：鼠标在状态胶囊上时设为手形光标并返回 true；其余区域交给基类。
    //   pt：host 客户区坐标。
    bool OnSetCursor(POINT pt) override;

    // 鼠标是否悬停在状态胶囊上
    bool IsPillHovered() const { return m_pillHover; }

private:
    // 切换状态胶囊的悬停态：进入时登记悬停提示并通知提示管理器开始计时，离开时注销提示；随后重绘。
    //   hover：是否悬停；pt：鼠标位置（host 客户区坐标，进入时用来换算提示的屏幕位置）。
    void SetPillHover(bool hover, POINT pt);

    CString  m_userName;     // 头像下方用户名；空表示无账号
    bool     m_pillHover;    // 鼠标是否悬停在状态胶囊上（2026-10-04 起：悬停时底色加深、手形光标、登记悬停提示）
    COLORREF m_avatarColor;  // 头像方块底色；默认主题橙，可按账号改
    int      m_presence;     // 状态胶囊显示的在线状态(v2 presence_type)；默认在线
    RECT     m_rcPillHit;    // 状态胶囊命中矩形(host 客户区坐标)，供登录窗锚定状态菜单
    CString  m_title;        // 渐变区左下角的产品名称；默认 FlamingoIM
    CString  m_slogan;       // 产品名称下方的标语；默认内置标语，空串表示不显示
    CString  m_avatarGlyph;      // SetAvatar 设的字形；m_avatarGlyphSet 为 false 时不用它，改取用户名首字
    bool     m_avatarGlyphSet;   // 是否调用过 SetAvatar：调用过之后头像字形取 m_avatarGlyph
    std::unique_ptr<Gdiplus::Bitmap> m_avatarPhoto;   // SetAvatar 加载的照片（内存里的副本，不占用文件）；为空表示画字形
    std::wstring m_avatarPhotoPath;   // m_avatarPhoto 对应的文件路径，路径相同时不重复加载
    RECT     m_rcAvatar;         // 头像方块的矩形（host 客户区坐标），由 OnPaint 缓存
};

} // namespace balloonauth
