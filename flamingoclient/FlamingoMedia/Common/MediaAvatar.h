/**
 *  媒体 DLL 里用户与群头像的来源与位图，MediaAvatar.h
 *
 *  来电窗左侧的头像、通话画面在对方没开摄像头时显示的头像（docs/音视频通话优化方案.md 第 1、5 项）都经本文件取得。
 *  头像该怎么画由宿主决定（IMediaHost::GetUserAvatar / GetGroupAvatar：字形与底色，照片已在本机时另给照片路径），与
 *  主面板联系人列表、群列表同一口径；位图的画法也与主面板是同一份代码：字形用 balloonmain::MakeMonogramAvatar，照片用
 *  balloonmain::MakeRoundedAvatarFromFile，经 balloonmain::CAvatarBitmapCache 按内容缓存（这几个源文件本 DLL 直接编译，
 *  做法与文件共享 DLL 的 CShareAvatarSource 相同）。界面上用 balloonui 的 DuiAvatar 显示，头像是圆形的。
 *
 *  每个窗口持有一个 CMediaAvatarSource。位图归它所有，窗口里的 DuiAvatar 与自绘代码只借用，所以它必须比这些控件存活得久：
 *  窗口销毁控件树之后再 Clear 或析构。宿主通知头像变了（IMediaModule::OnAvatarsUpdated）时，窗口重新取一遍即可。
 *
 *  典型用法：
 *      m_avatars.SetHost(host);
 *      row->AddChild(std::unique_ptr<DuiControl>(m_avatars.MakeUserAvatar(userId, kAvatarPx).release()), ...);
 *      HBITMAP bmp = m_avatars.GetUserBitmap(peerId, 96);   //自绘时直接取位图
 *      ...
 *      m_avatars.Clear();   //窗口销毁之后
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <memory>

#include "../Api/FlamingoMediaApi.h"
#include "../../Source/UI/BalloonMain/AvatarBitmapCache.h"

namespace balloonwjui {
class DuiAvatar;
}

namespace flamingomedia {

//宿主不给头像时（没有登记宿主、宿主返回失败）画的字形与底色：问号、中性灰
const wchar_t* const kUnknownAvatarGlyph = L"?";
const COLORREF kUnknownAvatarColor = RGB(0x9A, 0x98, 0x93);

/**
 *  取头像的来源。生产代码由 CHostAvatarQuery 把调用转给 IMediaHost；单元测试换成自己的实现，不必实现整个宿主接口。
 */
class IMediaAvatarQuery
{
public:
    virtual ~IMediaAvatarQuery()
    {
    }

    /**
     *  取一个用户的头像。
     *  @param userId 用户 userid
     *  @param out    [出参] m_cbSize 已由调用方填好
     *  @return 已填写返回 true
     */
    virtual bool QueryUserAvatar(int64_t userId, MediaAvatarInfo* out) = 0;

    /**
     *  取一个群的头像。参数与返回值同 QueryUserAvatar。
     */
    virtual bool QueryGroupAvatar(int64_t groupId, MediaAvatarInfo* out) = 0;
};

/**
 *  CHostAvatarQuery —— 把 IMediaAvatarQuery 的调用转给 IMediaHost::GetUserAvatar / GetGroupAvatar。
 */
class CHostAvatarQuery : public IMediaAvatarQuery
{
public:
    CHostAvatarQuery();

    /**
     *  登记宿主。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长）；为 NULL 时一律返回 false
     */
    void SetHost(IMediaHost* host);

    //转给 IMediaHost::GetUserAvatar；没有宿主时返回 false
    virtual bool QueryUserAvatar(int64_t userId, MediaAvatarInfo* out) override;

    //转给 IMediaHost::GetGroupAvatar；没有宿主时返回 false
    virtual bool QueryGroupAvatar(int64_t groupId, MediaAvatarInfo* out) override;

private:
    IMediaHost* m_host;   //宿主（借用，不拥有）；为 NULL 时不取头像
};

/**
 *  CMediaAvatarSource —— 一个窗口里全部头像的来源：向宿主取头像，生成并持有头像位图。只在 UI 线程使用。
 */
class CMediaAvatarSource
{
public:
    CMediaAvatarSource();

    //析构时释放全部位图（等同于 Clear），借用这些位图的控件须已销毁
    ~CMediaAvatarSource();

    /**
     *  登记取头像的宿主（内部经 CHostAvatarQuery 转接）。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长），为 NULL 时一律画问号
     */
    void SetHost(IMediaHost* host);

    /**
     *  登记取头像的来源（单元测试用；与 SetHost 二选一，后调用的生效）。
     *  @param query 来源（借用，不拥有；须比本对象的使用期长），为 NULL 时一律画问号
     */
    void SetQuery(IMediaAvatarQuery* query);

    /**
     *  取一个用户的头像描述。来源不给（未登记、返回失败）时为问号与中性灰底色。
     *  @param userId 用户 userid
     *  @return 头像描述（m_cbSize 已填好）
     */
    MediaAvatarInfo QueryUser(int64_t userId) const;

    /**
     *  取一个群的头像描述。其余同 QueryUser。
     *  @param groupId 群 id
     */
    MediaAvatarInfo QueryGroup(int64_t groupId) const;

    /**
     *  取一个用户头像的位图：照片在本机且能解码时为照片，否则为字形。
     *  @param userId 用户 userid
     *  @param sizePx 位图边长，单位：像素，须大于 0
     *  @return 位图，<u>归本对象所有，调用方不得释放</u>；生成失败时返回 NULL
     */
    HBITMAP GetUserBitmap(int64_t userId, int sizePx);

    /**
     *  取一个群头像的位图。其余同 GetUserBitmap。
     *  @param groupId 群 id
     *  @param sizePx  位图边长，单位：像素，须大于 0
     */
    HBITMAP GetGroupBitmap(int64_t groupId, int sizePx);

    /**
     *  建一个显示这个用户头像的圆形 DuiAvatar：挂上 GetUserBitmap 的位图；位图生成失败时由 DuiAvatar 自己画字形与底色。
     *  @param userId 用户 userid
     *  @param sizePx 头像边长，单位：像素，须大于 0（位图按这个尺寸生成，与控件矩形一致时显示最清晰）
     *  @return 新建的控件，所有权交给调用方
     */
    std::unique_ptr<balloonwjui::DuiAvatar> MakeUserAvatar(int64_t userId, int sizePx);

    /**
     *  建一个显示这个群头像的圆形 DuiAvatar。其余同 MakeUserAvatar。
     */
    std::unique_ptr<balloonwjui::DuiAvatar> MakeGroupAvatar(int64_t groupId, int sizePx);

    /**
     *  把已有的 DuiAvatar 改为显示这个用户的头像（头像更新后刷新用，不必重建控件）。
     *  @param avatar 要改的控件（借用）；为 NULL 时什么也不做
     *  @param userId 用户 userid
     *  @param sizePx 头像边长，单位：像素
     */
    void ApplyUser(balloonwjui::DuiAvatar* avatar, int64_t userId, int sizePx);

    /**
     *  把已有的 DuiAvatar 改为显示这个群的头像。其余同 ApplyUser。
     */
    void ApplyGroup(balloonwjui::DuiAvatar* avatar, int64_t groupId, int sizePx);

    //释放全部位图。之前借出的位图随之失效，借用它们的控件须已销毁或已改用新位图
    void Clear();

    //当前持有的位图张数（单元测试核对缓存用）
    size_t BitmapCount() const;

private:
    CMediaAvatarSource(const CMediaAvatarSource&);
    CMediaAvatarSource& operator=(const CMediaAvatarSource&);

    //把一个头像描述套用到 DuiAvatar 上：圆形、字形与底色作后备、挂上位图
    void Apply(balloonwjui::DuiAvatar* avatar, const MediaAvatarInfo& info, int sizePx);

    CHostAvatarQuery               m_hostQuery;   //SetHost 时用的转接对象
    IMediaAvatarQuery*             m_query;       //当前取头像的来源（借用）；为 NULL 时一律画问号
    balloonmain::CAvatarBitmapCache m_cache;      //按内容缓存的头像位图，位图归它所有
};

} // namespace flamingomedia
