/**
 *  机器人管理界面的窗口工厂 CBotUiFactory：IBotWindowFactory 的实现。
 *
 *  职责：
 *    - InitUi：本 DLL 那份 balloonui 的界面初始化——界面文字经宿主翻译（i18n::InitFromHost）、默认字体随界面语言
 *      （i18n::UiFontName）、balloonui 内置文字的译文；做法与 FlamingoThreadGroup 的 CThreadUiFactory 相同；
 *    - CreateGroupManager / CreateMyBots：新建两种顶层窗口对象交给模块。
 *
 *  进程内经 Instance() 取唯一对象，由 DllMain 的导出函数登记给模块（CBotModule::SetWindowFactory）。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "../Host/BotWindowApi.h"

namespace flamingobot {

/**
 *  CBotUiFactory —— 窗口工厂。只在界面线程上使用。
 */
class CBotUiFactory : public IBotWindowFactory
{
public:
    /**
     *  取进程内唯一的工厂对象。
     */
    static CBotUiFactory& Instance();

    /**
     *  模块初始化时调用一次：登记翻译用的宿主，按宿主报告的界面语言与字体初始化多语言和 balloonui 的默认字体。
     *  @param host 宿主接口（借用，ShutdownUi 之前一直有效）
     *  @return 总是返回 true：宿主取不到界面语言时按代码里的简体中文原文显示，只记一条警告
     */
    virtual bool InitUi(IBotHost* host) override;

    //模块关闭时调用一次：解除翻译用的宿主（之后的 TR 返回原文）
    virtual void ShutdownUi() override;

    /**
     *  新建群机器人管理窗对象（Open 时才创建窗口）。
     *  @return 窗口对象，所有权交给模块；参数非法时返回 NULL
     */
    virtual IBotTopWindow* CreateGroupManager(int64_t groupId, const std::wstring& groupName, IBotWindowOwner* owner) override;

    /**
     *  新建「我的机器人」窗口对象（Open 时才创建窗口）。
     *  @return 窗口对象，所有权交给模块；参数非法时返回 NULL
     */
    virtual IBotTopWindow* CreateMyBots(IBotWindowOwner* owner) override;

private:
    CBotUiFactory();
    CBotUiFactory(const CBotUiFactory&);
    CBotUiFactory& operator=(const CBotUiFactory&);
};

} // namespace flamingobot
