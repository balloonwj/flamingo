/**
 *  模块对象与帖子群窗口之间的接口（DLL 内部）。
 *
 *  模块对象（Host/ThreadGroupModule）只管路由与生命周期，不直接依赖界面代码：窗口经 IThreadWindowFactory 创建，
 *  以 IThreadWindow 的形式交给模块；窗口反过来经 IThreadWindowOwner 向模块要宿主接口、表情库、图片上传与下载。
 *  这样单元测试可以只编译模块与 Core（不带 balloonui），用假的工厂验证路由。真正的工厂在 UI/ThreadUiFactory.cpp，
 *  由 DllMain.cpp 的导出函数登记给模块。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../ThreadGroupApi.h"
#include "../Core/ThreadMembers.h"
#include "../Core/ThreadTypes.h"
#include "../Core/ThreadText.h"

namespace threadgroup {

class CThreadSession;
class IThreadSessionView;

//表情库里的一个表情（从宿主取来后缓存在模块里）
struct ThreadFaceEntry
{
    int          m_faceId;   //表情编号
    std::wstring m_name;     //表情名（不含方括号）
    std::wstring m_path;     //表情图片在本机的完整路径

    ThreadFaceEntry()
        : m_faceId(0)
    {
    }
};

/**
 *  窗口向模块要资源、报告状态的接口（模块实现）。只在界面线程上调用。
 */
class IThreadWindowOwner
{
public:
    //宿主接口；模块关闭后为 NULL
    virtual IThreadGroupHost* Host() = 0;

    //表情名表（WireToDisplay / DisplayToWire 用）
    virtual const FaceTable& Faces() = 0;

    //表情库的全部表情（按宿主给的顺序，表情选择浮层用）
    virtual const std::vector<ThreadFaceEntry>& FaceEntries() = 0;

    /**
     *  为一次图片上传或下载分配请求号并登记到本群名下。先分配、再 StartUpload / StartDownload，
     *  因为宿主对已缓存的图片会在 StartDownload 里同步回调，窗口必须先记下请求号。
     */
    virtual uint32_t BeginImageRequest(int64_t groupId) = 0;

    //开始上传；失败时请求号作废并返回 false
    virtual bool StartUpload(uint32_t requestId, const std::wstring& localPath) = 0;

    //开始下载；失败时请求号作废并返回 false
    virtual bool StartDownload(uint32_t requestId, const std::string& fileId, const std::string& md5) = 0;

    //窗口「正在看」状态变了（激活、失去激活、最小化、还原）
    virtual void OnWindowWatchChanged(int64_t groupId, bool watching) = 0;

    //窗口已经销毁（用户关闭）：模块撤销「正在查看」，稍后释放窗口对象与会话
    virtual void OnWindowClosed(int64_t groupId) = 0;

protected:
    ~IThreadWindowOwner()
    {
    }
};

/**
 *  一个帖子群窗口（界面实现）。只在界面线程上调用。
 */
class IThreadWindow
{
public:
    virtual ~IThreadWindow()
    {
    }

    /**
     *  显示（首次调用时创建）并激活窗口。
     *  @param postId 大于 0 时选中这条帖子
     *  @return 窗口已显示返回 true
     */
    virtual bool Open(int64_t postId) = 0;

    //销毁窗口（不再回调 OnWindowClosed）
    virtual void Close() = 0;

    //窗口存在、在前台、没有最小化
    virtual bool IsWatching() const = 0;

    //会话通知界面用的接口
    virtual IThreadSessionView* View() = 0;

    //群资料变了：刷新标题与可用的操作
    virtual void OnGroupChanged() = 0;

    //群成员名单到了（@ 成员用）：每个人带群角色与显示名（群名片或昵称）
    virtual void OnGroupMembers(const std::vector<ThreadMember>& members) = 0;

    //图片上传的结果；成功时 image 已填好 fileid、md5、尺寸与字节数
    virtual void OnImageUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath) = 0;

    //图片下载的结果
    virtual void OnImageReady(uint32_t requestId, bool ok, const std::wstring& localPath) = 0;

    //头像照片下载完成：重画头像
    virtual void OnAvatarsUpdated() = 0;

    //群成员面板被用户关掉了：记下面板已收起
    virtual void OnMemberPanelClosed() = 0;
};

/**
 *  创建帖子群窗口的工厂（界面实现，DllMain 登记给模块）。
 */
class IThreadWindowFactory
{
public:
    /**
     *  模块初始化时调用一次：界面库的多语言与字体（i18n::InitFromHost、balloonui 的默认字体与内置文字）。
     *  @return 成功返回 true
     */
    virtual bool InitUi(IThreadGroupHost* host) = 0;

    //模块关闭时调用一次：解除界面库与宿主的关联
    virtual void ShutdownUi() = 0;

    /**
     *  新建一个帖子群窗口对象（还没有创建窗口，Open 时才创建）。
     *  @param groupId 帖子群
     *  @param session 会话控制器（借用，模块保证比窗口存活久）
     *  @param owner   模块（借用）
     *  @return 窗口对象，所有权交给模块（用 delete 释放）
     */
    virtual IThreadWindow* CreateThreadWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner) = 0;

protected:
    ~IThreadWindowFactory()
    {
    }
};

} // namespace threadgroup
