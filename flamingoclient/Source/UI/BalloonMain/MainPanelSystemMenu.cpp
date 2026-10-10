/**
 *  MainPanelSystemMenu.cpp
 *
 *  主面板系统菜单行模型的实现。仅装配数据（项 id / 文本 / 图标 / 分隔条 / 分组标题），
 *  不碰任何窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程
 *  逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，
 *  源码按 UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "MainPanelSystemMenu.h"
#include "MainPanelIcons.h"        // IconKind（各项左侧图标类型）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

// 「关于」项的缺省文字：调用方没有传入按产品名称拼好的文字时使用。
static const wchar_t* const kDefaultAboutLabel = TR_NOOP(L"关于 FlamingoIM");

namespace {

//追加一行分组标题：id 为 0、没有图标、不可点击
void AppendSysHeader(std::vector<SystemMenuEntry>& items, const wchar_t* label)
{
    items.push_back(SystemMenuEntry{ 0, false, label, -1, false, true });
}

//追加一条分隔线
void AppendSysSeparator(std::vector<SystemMenuEntry>& items)
{
    items.push_back(SystemMenuEntry{ 0, true, nullptr, -1, false });
}

} // namespace

std::vector<SystemMenuEntry> BuildSystemMenuModel(bool withFileShare, int recordState, const wchar_t* aboutLabel,
                                                  bool withCalls, bool withScreenShare, bool withRecordManager,
                                                  bool withOcr, bool withMarkdown, bool withLock, bool withBots)
{
    // 分组与顺序（2026-10-05 与用户确认，docs/屏幕共享与录制方案.md 第 11.6 节 F8；此前只用分隔线分组，
    // 顺序对照 design/主面板系统菜单.png）：
    //   账号        个人信息 / 修改密码 / 锁屏（锁屏模块不可用时没有「锁屏」）
    //   ──────────（分隔）
    //   发起        发起语音通话 / 发起视频通话 / 发起屏幕共享 / 发起录制（一项都没有时整组不出现）
    //   ──────────（分隔）
    //   消息与工具  聊天记录管理 / 公告与通知 / 消息盒子 / 备忘与提醒 / 文件共享 / 录制管理
    //   ──────────（分隔）
    //   设置        系统设置 / 网络设置
    //   ──────────（分隔）
    //   登出与关于  登出 / 关于 <产品名称>（2026-10-07 起「关于」放在最后一行）
    // 每组开头一行分组标题，组与组之间一条分隔线；模块不可用的项不出现。
    std::vector<SystemMenuEntry> items;

    AppendSysHeader(items, TR(L"账号"));
    items.push_back(SystemMenuEntry{ kSysMenuProfile,     false, TR(L"个人信息"),        kIconUser,  true });
    // 修改密码用钥匙图标；挂锁图标留给下面的「锁屏」，与托盘菜单「锁屏」一致（2026-10-07 起）
    items.push_back(SystemMenuEntry{ kSysMenuChangePwd,   false, TR(L"修改密码"),        kIconKey,   true });
    // 锁屏：与托盘菜单「锁屏」同一去处；锁屏模块没有加载时不出现
    if (withLock)
    {
        items.push_back(SystemMenuEntry{ kSysMenuLockScreen, false, TR(L"锁屏"),         kIconLock,  true });
    }

    // 发起：音视频通话、屏幕共享、录制，各自的模块不可用时不出现；三者都没有时整组连同前面的分隔线都不出现
    const bool withRecord = (recordState != kRecordMenuHidden);
    if (withCalls || withScreenShare || withRecord)
    {
        AppendSysSeparator(items);
        //「发起」在「发起接龙」窗口里是提交按钮的文字，日文译作「作成」；作分组标题要另一种译法，所以带上下文
        AppendSysHeader(items, TRC(L"系统菜单分组", L"发起"));
        // 发起语音 / 视频通话（2026-10-05 与用户确认）；音视频模块没有加载时不出现
        if (withCalls)
        {
            items.push_back(SystemMenuEntry{ kSysMenuStartVoiceCall, false, TR(L"发起语音通话"), kIconPhone,     true });
            items.push_back(SystemMenuEntry{ kSysMenuStartVideoCall, false, TR(L"发起视频通话"), kIconVideoCall, true });
        }
        //发起屏幕共享：紧挨在「发起录制」之前（屏幕共享第 17 期）；屏幕共享模块不可用时不出现
        if (withScreenShare)
        {
            items.push_back(SystemMenuEntry{ kSysMenuScreenShare, false, TR(L"发起屏幕共享"), kIconScreenShare, true });
        }
        //发起录制：录屏模块不可用时不出现；录制中文字为「停止录制」
        if (withRecord)
        {
            items.push_back(SystemMenuEntry{ kSysMenuRecord, false, RecordMenuLabel(recordState), kIconRecord, true });
        }
    }

    // 消息与工具：都是打开一个汇总窗口的入口
    AppendSysSeparator(items);
    AppendSysHeader(items, TR(L"消息与工具"));
    items.push_back(SystemMenuEntry{ kSysMenuHistoryManager, false, TR(L"聊天记录管理"), kIconHistory, true });
    items.push_back(SystemMenuEntry{ kSysMenuAnnounce,    false, TR(L"公告与通知"),      kIconBell,  true });
    // 消息盒子：紧跟在「公告与通知」之后，图标与会话列表里「消息盒子」那一行相同（2026-10-04 起）
    items.push_back(SystemMenuEntry{ kSysMenuMsgBox,      false, TR(L"消息盒子"),        kIconDrawer, true });
    // 备忘与提醒：紧跟在「消息盒子」之后（2026-10-03 与用户确认；此前入口在系统设置「通用」页）
    items.push_back(SystemMenuEntry{ kSysMenuMemo,        false, TR(L"备忘与提醒"),      kIconCalendar, true });
    // 我的机器人：紧跟在「备忘与提醒」之后（2026-10-08 起，docs/机器人方案.md §4.3）；插件不可用时不出现
    if (withBots)
    {
        items.push_back(SystemMenuEntry{ kSysMenuMyBots,  false, TR(L"我的机器人"),      kIconBot,   true });
    }
    // 图片文字识别：紧跟在「备忘与提醒」之后（2026-10-05 起；有「我的机器人」时排在它后面）；识别模块没有加载时不出现
    if (withOcr)
    {
        items.push_back(SystemMenuEntry{ kSysMenuOcr,     false, TR(L"图片文字识别"),    kIconOcr,   true });
    }
    // Markdown 编辑器：紧跟在「图片文字识别」之后（2026-10-06 起）；FlamingoMarkdown.dll 不可用时不出现
    if (withMarkdown)
    {
        items.push_back(SystemMenuEntry{ kSysMenuMarkdownEditor, false, TR(L"Markdown 编辑器"), kIconMarkdown, true });
    }
    // 文件共享：模块没有加载时不出现
    if (withFileShare)
    {
        items.push_back(SystemMenuEntry{ kSysMenuFileShare, false, TR(L"文件共享"),      kIconShare, true });
    }
    // 录制管理：排在这一组最后（2026-10-05 起）；录制管理窗口不可用时不出现
    if (withRecordManager)
    {
        items.push_back(SystemMenuEntry{ kSysMenuRecordManager, false, TR(L"录制管理"), kIconRecordList, true });
    }

    AppendSysSeparator(items);
    AppendSysHeader(items, TR(L"设置"));
    items.push_back(SystemMenuEntry{ kSysMenuSettings,    false, TR(L"系统设置"),        kIconGear,  true });
    items.push_back(SystemMenuEntry{ kSysMenuNetSettings, false, TR(L"网络设置"),        kIconGlobe, true });

    // 登出与关于：「关于」放在整张菜单的最后一行，「登出」在它之前（2026-10-07 用户要求两者互换）。
    // 登出是"结束当前会话"类操作（断开连接、关掉所有聊天窗、回登录界面），与上面的设置类操作之间
    // 隔着分隔线与分组标题，降低误点概率
    AppendSysSeparator(items);
    AppendSysHeader(items, TR(L"登出与关于"));
    items.push_back(SystemMenuEntry{ kSysMenuLogout,      false, TR(L"登出"),            kIconLogout, true });
    const wchar_t* const about = (aboutLabel != NULL && aboutLabel[0] != L'\0') ? aboutLabel : TR(kDefaultAboutLabel);
    items.push_back(SystemMenuEntry{ kSysMenuAbout,       false, about,              kIconInfo,   true });

    return items;
}

} // namespace balloonmain
