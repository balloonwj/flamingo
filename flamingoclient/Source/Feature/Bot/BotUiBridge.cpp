/**
 *  BotUiBridge.cpp
 *
 *  机器人管理界面的入口桥接，说明见 BotUiBridge.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotUiBridge.h"

namespace botui {

namespace {

IsAvailableFn      g_isAvailable = NULL;  //插件是否可用；NULL 表示没有登记
OpenGroupManagerFn g_openGroup = NULL;    //打开群机器人管理窗
OpenMyBotsFn       g_openMine = NULL;     //打开「我的机器人」窗口
FeaturesFn         g_onFeatures = NULL;   //开关变化
GroupBotsChangedFn g_onGroupBots = NULL;  //群机器人变化

} // anonymous namespace

void SetHooks(IsAvailableFn isAvailable, OpenGroupManagerFn openGroup, OpenMyBotsFn openMine)
{
    g_isAvailable = isAvailable;
    g_openGroup = openGroup;
    g_openMine = openMine;
}

void SetNotifyHooks(FeaturesFn onFeatures, GroupBotsChangedFn onGroupBots)
{
    g_onFeatures = onFeatures;
    g_onGroupBots = onGroupBots;
}

void NotifyFeatures(bool bot, bool groupBot, bool personal, bool groupExternal)
{
    if (g_onFeatures != NULL)
        g_onFeatures(bot, groupBot, personal, groupExternal);
}

void NotifyGroupBotsChanged(int64_t groupid)
{
    if (g_onGroupBots != NULL)
        g_onGroupBots(groupid);
}

bool IsAvailable()
{
    return g_isAvailable != NULL && g_isAvailable();
}

bool OpenGroupManager(HWND owner, int64_t groupid, const wchar_t* groupName)
{
    if (!IsAvailable() || g_openGroup == NULL)
        return false;
    g_openGroup(owner, groupid, groupName);
    return true;
}

bool OpenMyBots(HWND owner)
{
    if (!IsAvailable() || g_openMine == NULL)
        return false;
    g_openMine(owner);
    return true;
}

} // namespace botui
