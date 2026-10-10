/**
 *  帖子群窗口的工厂 CThreadUiFactory：IThreadWindowFactory 的实现（docs/帖子群方案.md 4.6、7.1）。
 *
 *  职责：
 *    - InitUi：本 DLL 那份 balloonui 的界面初始化——界面文字经宿主翻译（i18n::InitFromHost）、默认字体随界面语言、
 *      balloonui 内置文字的译文。主操作按钮沿用 balloonui 的品牌蓝（DuiButton 的配色写在库里，不读 DuiTheme），
 *      与客户端其余用 balloonui 搭的窗口一致；
 *    - CreateThreadWindow：新建一个帖子群窗口对象（CThreadWindow）交给模块。
 *
 *  进程内经 Instance() 取唯一对象，由 DllMain 的导出函数登记给模块（CThreadGroupModule::SetWindowFactory）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../Host/ThreadWindowApi.h"

namespace threadgroup {

/**
 *  CThreadUiFactory —— 帖子群窗口的工厂。只在界面线程上使用。
 */
class CThreadUiFactory : public IThreadWindowFactory
{
public:
    /**
     *  取进程内唯一的工厂对象。
     */
    static CThreadUiFactory& Instance();

    /**
     *  模块初始化时调用一次：登记翻译用的宿主，按宿主报告的界面语言与字体初始化多语言和 balloonui 的默认字体。
     *  @param host 宿主接口（借用，Shutdown 之前一直有效）
     *  @return 总是返回 true：宿主取不到界面语言时按代码里的简体中文原文显示，只记一条警告
     */
    virtual bool InitUi(IThreadGroupHost* host) override;

    //模块关闭时调用一次：解除翻译用的宿主（之后的 TR 返回原文）
    virtual void ShutdownUi() override;

    /**
     *  新建一个帖子群窗口对象（Open 时才创建窗口）。
     *  @param groupId 帖子群
     *  @param session 会话控制器（借用）
     *  @param owner   模块（借用）
     *  @return 窗口对象，所有权交给模块；参数非法时返回 NULL
     */
    virtual IThreadWindow* CreateThreadWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner) override;

private:
    CThreadUiFactory();
    CThreadUiFactory(const CThreadUiFactory&);
    CThreadUiFactory& operator=(const CThreadUiFactory&);
};

} // namespace threadgroup
