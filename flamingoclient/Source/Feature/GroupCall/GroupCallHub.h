/**
 *  GroupCallHub.h
 *  exe 侧的群通话中心：群聊窗与 FlamingoMedia.dll 之间关于群通话的唯一衔接点（docs/群音视频通话方案.md 6.3）。
 *    - 转发操作给 DLL：群聊窗「语音通话」「视频通话」选好人之后发起、横幅与通话卡片的「加入」、打开群聊窗时查询、
 *      选人框确认后追加邀请；
 *    - 记录 DLL 回报的各群群通话状态（GroupCallStatusCache），群聊窗据此显示横幅、决定通话卡片的状态与「加入」按钮。
 *  群通话的状态机、窗口与媒体管线都在 DLL 里（FlamingoMedia/GroupCall/GroupCallController），本类不解析任何信令。
 *  进程内唯一（GetInstance），所有方法只在 UI 线程调用。模块指针由主面板在 SetMediaModule 时登记，DLL 未加载时
 *  各操作返回 false，群聊窗据 IsModuleReady 隐藏「语音通话」「视频通话」按钮。
 *
 *  典型用法（群聊窗选好人之后）：
 *      balloongroupcall::CGroupCallHub::GetInstance().StartCall(groupId, groupName, media, invitees);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "GroupCallStatusCache.h"

namespace flamingomedia { class IMediaModule; }

namespace balloongroupcall {

//群通话的媒体类型，取值与 DLL 的 groupcall::gc_media 一致
enum GroupCallMedia
{
    kGroupCallVoice = 1,   //群语音：加入时摄像头默认关闭
    kGroupCallVideo = 2,   //群视频：加入时摄像头默认打开
};

//exe 侧的群通话中心
class CGroupCallHub
{
public:
    //取进程内唯一的对象（函数内静态变量）
    static CGroupCallHub& GetInstance();

    /**
     *  登记 FlamingoMedia.dll 的模块接口。
     *  @param module 模块接口，不拥有；传 NULL 表示模块不可用（卸载前调用）
     */
    void AttachModule(flamingomedia::IMediaModule* module);

    //模块是否已加载；未加载时群聊窗隐藏「语音通话」「视频通话」按钮
    bool IsModuleReady() const;

    /**
     *  发起群通话。
     *  @param groupId   群 id
     *  @param groupName 群名，用于通话窗标题
     *  @param media     取值见 enum GroupCallMedia
     *  @param invitees  要振铃的人（不含自己）
     *  @return 已交给 DLL 返回 true；模块未加载或 DLL 拒绝（已在通话中等，DLL 已提示原因）返回 false
     */
    bool StartCall(int64_t groupId, const CString& groupName, int media, const std::vector<int64_t>& invitees);

    /**
     *  加入某群进行中的群通话（横幅与通话卡片的「加入」）。
     *  @param roomId 房间号
     *  @return 已交给 DLL 返回 true
     */
    bool JoinCall(int64_t groupId, const CString& groupName, const std::string& roomId);

    //查询某群有无进行中的群通话（群聊窗打开时调用）；结果经 OnStatus 回来。模块未加载时不做任何事
    void Query(int64_t groupId);

    /**
     *  追加邀请（选人框确认后）。
     *  @return 已交给 DLL 返回 true
     */
    bool InviteMembers(int64_t groupId, const std::vector<int64_t>& userIds);

    /**
     *  DLL 回报某群的群通话状态（经 CMediaHostAdapter::OnGroupCallStatus 转来），记进状态缓存。
     *  参数含义同 IMediaHost::OnGroupCallStatus；roomId 可为 NULL，按空串处理。
     */
    void OnStatus(int64_t groupId, const char* roomId, bool active, int media, int count, int64_t initiatorId,
                  int64_t startTime);

    //某张通话卡片所指那一场的状态，取值见 enum GroupCallRoomState
    int RoomState(int64_t groupId, const std::string& roomId) const;

    /**
     *  取某群进行中的群通话（横幅用）。
     *  @return 已知有进行中的群通话返回 true
     */
    bool GetActive(int64_t groupId, GroupCallActiveInfo& out) const;

    //登出时清空状态缓存；模块登记保持不变
    void ClearStatus();

private:
    CGroupCallHub();

    CGroupCallHub(const CGroupCallHub&);
    CGroupCallHub& operator=(const CGroupCallHub&);

    flamingomedia::IMediaModule* m_module;   //模块接口（不拥有）；未加载时为 NULL
    GroupCallStatusCache         m_cache;    //各群的群通话状态，本次登录期间有效，登出时清空
};

} // namespace balloongroupcall
