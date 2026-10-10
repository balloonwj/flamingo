/**
 *  文件共享窗口里成员头像的来源与位图，ShareAvatar.h
 *
 *  发起共享对话框的成员行、「文件共享」窗口详情里的成员与访问者都画头像（2026-10-03 起）。头像该怎么画由宿主决定
 *  （IFileShareHost::GetUserAvatar：字形与底色，照片已在本机时另给照片路径），与主面板联系人列表同一口径；位图的画法
 *  也与主面板是同一份代码：字形用 balloonmain::MakeMonogramAvatar，照片用 balloonmain::MakeRoundedAvatarFromFile，
 *  经 balloonmain::CAvatarBitmapCache 按内容缓存（这几个源文件 DLL 直接编译）。界面上用 balloonui 的 DuiAvatar 显示，
 *  头像是圆形的。
 *
 *  每个窗口持有一个 CShareAvatarSource。位图归它所有，窗口里的 DuiAvatar 只借用，所以它必须比这些控件存活得久：
 *  窗口销毁控件树之后再 Clear 或析构。成员行重建时新的头像控件直接复用缓存里的位图，不重新生成。
 *  宿主通知头像变了（IFileShareModule::OnAvatarsUpdated）时，窗口重建成员行即可，重建时会重新向宿主取头像。
 *
 *  典型用法：
 *      m_avatars.SetHost(host);
 *      row->AddChild(std::unique_ptr<DuiControl>(m_avatars.MakeAvatar(userId, kAvatarPx).release()), ...);
 *      ...
 *      m_avatars.Clear();   //窗口销毁之后
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <memory>
#include <string>

#include "../FileShareApi.h"
#include "../../Source/UI/BalloonMain/AvatarBitmapCache.h"

namespace balloonwjui {
class DuiAvatar;
}

namespace fileshare {

//宿主不给头像时（没有登记宿主、宿主返回失败）画的字形与底色：问号、中性灰
const wchar_t* const kUnknownAvatarGlyph = L"?";
const COLORREF kUnknownAvatarColor = RGB(0x9A, 0x98, 0x93);

/**
 *  CShareAvatarSource —— 一个窗口里全部成员头像的来源：向宿主取头像，生成并持有头像位图。只在 UI 线程使用。
 */
class CShareAvatarSource
{
public:
    CShareAvatarSource();

    //析构时释放全部位图（等同于 Clear），借用这些位图的控件须已销毁
    ~CShareAvatarSource();

    /**
     *  登记宿主。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长），为 NULL 时一律画问号
     */
    void SetHost(IFileShareHost* host);

    /**
     *  向宿主取一个用户的头像。宿主不给（未登记宿主、宿主返回失败）时为问号与中性灰底色。
     *  @param userId 用户 userid
     *  @return 头像描述（m_cbSize 已填好）
     */
    FileShareAvatarInfo Query(int64_t userId) const;

    /**
     *  取一个用户头像的位图：照片在本机且能解码时为照片，否则为字形。
     *  @param userId 用户 userid
     *  @param sizePx 位图边长，单位：像素，须大于 0
     *  @return 位图，<u>归本对象所有，调用方不得释放</u>；生成失败时返回 NULL
     */
    HBITMAP GetBitmap(int64_t userId, int sizePx);

    /**
     *  建一个显示这个用户头像的圆形 DuiAvatar：挂上 GetBitmap 的位图；位图生成失败时由 DuiAvatar 自己画字形与底色。
     *  @param userId 用户 userid
     *  @param sizePx 头像边长，单位：像素，须大于 0（位图按这个尺寸生成，与控件矩形一致时显示最清晰）
     *  @return 新建的控件，所有权交给调用方（通常加入成员行）
     */
    std::unique_ptr<balloonwjui::DuiAvatar> MakeAvatar(int64_t userId, int sizePx);

    /**
     *  释放全部位图。调用前须确保借用这些位图的控件都已销毁（窗口已关闭）。
     */
    void Clear();

    /**
     *  当前缓存的位图张数（测试用：核对同一个头像只生成一次）。
     */
    size_t BitmapCount() const;

private:
    //禁止复制：位图所有权唯一
    CShareAvatarSource(const CShareAvatarSource&);
    CShareAvatarSource& operator=(const CShareAvatarSource&);

private:
    IFileShareHost*                  m_host;    //宿主（借用）；SetHost 之前为 NULL
    balloonmain::CAvatarBitmapCache  m_cache;   //字形与照片位图的缓存，位图归它所有；Clear 或析构时释放
};

} // namespace fileshare
