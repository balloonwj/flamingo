/**
 *  SubtitleSession.h
 *
 *  一个会话（单聊或群聊）的字幕模式：CSubtitleSession 管一个会话，CSubtitleRegistry 按会话管全部字幕、
 *  由主面板持有（2026-10-05 起字幕不随聊天窗关闭而关闭，原先由聊天窗持有）。头部「字幕模式」按钮与主面板
 *  右键菜单经集合切换进入 / 退出，收到的消息与撤回经集合交给对应会话的字幕。一个会话的字幕负责：
 *    · 进入时按保存的位置（或默认位置、避开其它字幕）建出字幕窗口，显示「等待新消息…」；
 *    · 收到消息时更新内容模型（CSubtitleModel），内容变了才让窗口重画；
 *    · 用户拖动、调宽、锁定后立刻把这个会话的位置存进账号配置；
 *    · 用户在任何一条字幕上改了外观（字号、行数、共享时隐藏）时保存，并让所有正在显示的字幕一起换新外观；
 *    · 「回到聊天窗」「退出字幕模式」两个按钮转告持有者（集合再转告主面板）。
 *  字幕模式不改变聊天窗本身（用户选定聊天窗保持不动）。方案见 docs/聊天字幕模式方案.md。
 *
 *  窗口经 ISubtitleView 接口操作，默认用 CSubtitleWindow；单元测试用 SetViewFactory 换成假视图
 *  （见 Tests/SubtitleTests.cpp 的 SUBC、SUBG 组）。只在界面线程上使用。
 *
 *  单个会话的典型用法（集合内部就是这样用的）：
 *      s.Init(kConvTypeSingle, peerId, &cfg.Subtitles(), &SaveThunk, this, &HostThunk, this);
 *      s.Toggle(title);
 *      s.OnIncoming(subtitle::MakeSubtitleIncoming(notify, selfId, speaker));
 *      s.Exit();
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../Session/SubtitlePrefs.h"
#include "SubtitleModel.h"
#include "SubtitleView.h"

namespace subtitle {

//字幕会话通知聊天窗的事件
enum SubtitleHostEvent
{
    //用户点了「回到聊天窗」：聊天窗应把自己调到前台（最小化时先还原）
    kSubtitleEventBack = 1,
    //字幕模式已经退出（用户点了退出按钮）：聊天窗据此刷新头部按钮的状态
    kSubtitleEventExited = 2,
};

/**
 *  通知聊天窗的回调。
 *    ctx：  Init 时传入的上下文（通常是聊天窗对象）。
 *    event：取值见 SubtitleHostEvent。
 */
typedef void (*SubtitleHostCallback)(void* ctx, int event);

/**
 *  把账号配置存盘的回调（偏好改动后调用）。
 *    ctx：Init 时传入的上下文。
 */
typedef void (*SaveSubtitlePrefsFn)(void* ctx);

/**
 *  CSubtitleSession —— 一个会话的字幕模式。
 */
class CSubtitleSession : public ISubtitleViewHost
{
public:
    CSubtitleSession();

    //析构时若仍在字幕模式，销毁字幕窗口（不通知聊天窗、不存盘）
    virtual ~CSubtitleSession();

    /**
     *  设置会话身份与依赖。须在 Enter 之前调用一次。
     *    ctype：      会话类型，1 单聊、2 群聊。
     *    cid：        单聊为对端 userid，群聊为群ID。
     *    prefs：      账号的字幕偏好（CUserConfig::Subtitles()）；不拥有，须比本对象活得久；可以为 NULL（不读写偏好）。
     *    save / saveCtx：偏好改动后存盘的回调与上下文；save 可以为 NULL。
     *    hostCb / hostCtx：通知聊天窗的回调与上下文；hostCb 可以为 NULL。
     */
    void Init(int ctype, int64_t cid, SubtitlePrefs* prefs, SaveSubtitlePrefsFn save, void* saveCtx,
              SubtitleHostCallback hostCb, void* hostCtx);

    /**
     *  更换建立视图的工厂（单元测试用）。不调用时用 CreateSubtitleWindowView 建出真正的字幕窗口。
     *    fn / ctx：工厂函数与上下文；fn 为 NULL 时恢复默认。
     */
    void SetViewFactory(CreateSubtitleViewFn fn, void* ctx);

    /**
     *  进入字幕模式：内容回到「等待新消息…」，建出字幕窗口并显示。已在字幕模式时什么都不做。
     *    title：会话名，显示在字幕工具条上。
     *  返回：处于字幕模式返回 true；窗口建不出来时返回 false。
     */
    bool Enter(const std::wstring& title);

    /**
     *  退出字幕模式：销毁字幕窗口。不在字幕模式时什么都不做。不通知聊天窗（调用方自己知道）。
     */
    void Exit();

    /**
     *  切换字幕模式：不在字幕模式时进入，否则退出。
     *    title：会话名（进入时使用）。
     *  返回：切换后是否处于字幕模式。
     */
    bool Toggle(const std::wstring& title);

    //是否处于字幕模式（字幕窗口正在显示）
    bool IsActive() const;

    /**
     *  收到一条消息。不在字幕模式时忽略；本人消息、系统提示与重复的消息不改变字幕。
     *    in：换算好的消息（MakeSubtitleIncoming）。
     */
    void OnIncoming(const SubtitleIncoming& in);

    /**
     *  有消息被撤回。正在显示的正是这条时换成撤回提示。
     *    msgid：     被撤回消息的服务器消息 id。
     *    noticeText：撤回提示。
     */
    void OnRecalled(long long msgid, const std::wstring& noticeText);

    /**
     *  更换会话名（群改名、联系人改备注之后）。不在字幕模式时只记下，下次进入时使用。
     *    title：新的会话名。
     */
    void SetTitle(const std::wstring& title);

    /**
     *  临时隐藏或恢复显示字幕窗口（客户端锁屏期间隐藏）。状态会记住：隐藏期间进入字幕模式，建出的窗口同样隐藏。
     *    hidden：true 隐藏，false 恢复显示。
     */
    void SetHidden(bool hidden);

    //当前是否处于临时隐藏状态（单元测试用）
    bool IsHidden() const;

    //内容模型（单元测试用）
    const CSubtitleModel& Model() const;

    //当前的视图对象；不在字幕模式时为 NULL（单元测试用）
    ISubtitleView* View() const;

    //此刻处于字幕模式的会话个数（单元测试用）
    static int ActiveCount();

    //---- 以下为 ISubtitleViewHost 的实现，由字幕窗口在用户操作时调用 ----

    //用户点了「回到聊天窗」：通知聊天窗 kSubtitleEventBack
    virtual void OnSubtitleBack() override;

    //用户点了「退出字幕模式」：退出字幕模式并通知聊天窗 kSubtitleEventExited
    virtual void OnSubtitleClose() override;

    /**
     *  用户锁定或解锁了字幕：把这个会话的位置与锁定状态存盘。
     *    locked：新的锁定状态。
     */
    virtual void OnSubtitleLockChanged(bool locked) override;

    /**
     *  用户拖动或调宽结束：把这个会话的位置存盘。
     *    rc：窗口矩形，屏幕坐标、物理像素。
     */
    virtual void OnSubtitleMoved(const RECT& rc) override;

    /**
     *  用户在设置菜单里改了外观：保存并存盘，所有正在显示的字幕都换成新外观，各自的新位置一并保存。
     *    ap：新的外观。
     */
    virtual void OnSubtitleAppearanceChanged(const SubtitleAppearance& ap) override;

private:
    CSubtitleSession(const CSubtitleSession&);
    CSubtitleSession& operator=(const CSubtitleSession&);

    /**
     *  把这个会话的字幕位置写进偏好（不存盘）。
     *    rc：    窗口矩形，屏幕坐标；为空矩形时不写。
     *    locked：锁定状态。
     */
    void rememberPlacement(const RECT& rc, bool locked);

    //调用存盘回调（有的话）
    void save();

    //通知聊天窗（有回调的话）
    void notifyHost(int event);

    //此刻处于字幕模式的全部会话（界面线程上使用）；进入时加入、退出时移除
    static std::vector<CSubtitleSession*>& activeSessions();

    int                  m_ctype;        //会话类型，1 单聊、2 群聊
    int64_t              m_cid;          //会话ID
    SubtitlePrefs*       m_prefs;        //账号的字幕偏好；不拥有，可以为 NULL
    SaveSubtitlePrefsFn  m_save;         //存盘回调；可以为 NULL
    void*                m_saveCtx;      //存盘回调的上下文
    SubtitleHostCallback m_hostCb;       //通知聊天窗的回调；可以为 NULL
    void*                m_hostCtx;      //通知聊天窗的回调的上下文
    CreateSubtitleViewFn m_viewFactory;  //建立视图的工厂；NULL 表示用 CreateSubtitleWindowView
    void*                m_viewCtx;      //工厂的上下文
    ISubtitleView*       m_view;         //字幕窗口；处于字幕模式时非 NULL，本对象拥有，退出时 delete
    CSubtitleModel       m_model;        //字幕内容
    std::wstring         m_title;        //会话名
    bool                 m_hidden;       //是否临时隐藏（客户端锁屏期间为 true）；与是否处于字幕模式无关
};

//字幕集合通知主面板的事件
enum SubtitleRegistryEvent
{
    //某个会话的字幕开了或关了（头部按钮、右键菜单、字幕上的退出按钮）：主面板据此刷新列表徽章与聊天窗按钮的高亮
    kRegistryEventStateChanged = 1,
    //用户点了某条字幕上的「回到聊天窗」：主面板打开这个会话的聊天窗（已打开时提到前台），字幕保留
    kRegistryEventBack = 2,
};

/**
 *  字幕集合通知主面板的回调。
 *    ctx：   Init 时传入的上下文（主面板）。
 *    event： 取值见 SubtitleRegistryEvent。
 *    ctype： 会话类型，1 单聊、2 群聊。
 *    cid：   会话ID。
 *    active：事件发生后这个会话是否处于字幕模式。
 */
typedef void (*SubtitleRegistryCallback)(void* ctx, int event, int ctype, int64_t cid, bool active);

/**
 *  CSubtitleRegistry —— 当前账号所有会话的字幕，由主面板持有（用户 2026-10-05 确认字幕不随聊天窗关闭而关闭）。
 *
 *  按（会话类型, 会话ID）各管一个 CSubtitleSession：主面板收到的消息、撤回、改名经它转给对应会话的字幕；
 *  头部按钮与右键菜单经它开关字幕；字幕上的「回到聊天窗」「退出字幕模式」经回调告诉主面板。字幕之间的避让与
 *  外观同步仍由 CSubtitleSession 自己完成。
 *
 *  退出过的会话对象不删除、留到下次开启时复用：用户点退出按钮时调用栈里还有这个会话对象自己的成员函数，
 *  此时删除它不安全。ExitAll 与析构时统一关闭全部字幕（不发通知）。只在界面线程上使用。
 *
 *  典型用法（主面板里）：
 *      m_subtitles.Init(&cfg.Subtitles(), &SaveThunk, this, &EventThunk, this);
 *      m_subtitles.Toggle(conversation_group, groupId, groupName);      //头部按钮或右键菜单
 *      m_subtitles.OnIncoming(conversation_group, groupId, subtitle::MakeSubtitleIncoming(notify, selfId, speaker));
 *      m_subtitles.ExitAll();                                            //注销或退出
 */
class CSubtitleRegistry
{
public:
    CSubtitleRegistry();

    //析构时关闭全部字幕（不发通知），释放全部会话对象
    ~CSubtitleRegistry();

    /**
     *  设置依赖。须在其它调用之前调用一次；可以重复调用（重新登录后换成新账号的偏好）。
     *    prefs：         账号的字幕偏好（CUserConfig::Subtitles()）；不拥有，须比本对象活得久；可以为 NULL。
     *    save / saveCtx：偏好改动后存盘的回调与上下文；save 可以为 NULL。
     *    cb / cbCtx：    通知主面板的回调与上下文；cb 可以为 NULL。
     */
    void Init(SubtitlePrefs* prefs, SaveSubtitlePrefsFn save, void* saveCtx, SubtitleRegistryCallback cb, void* cbCtx);

    /**
     *  更换建立视图的工厂（单元测试用），对之后新建的会话对象与已有的会话对象都生效。
     *    fn / ctx：工厂函数与上下文；fn 为 NULL 时恢复默认。
     */
    void SetViewFactory(CreateSubtitleViewFn fn, void* ctx);

    /**
     *  某个会话此刻是否处于字幕模式。
     *    ctype / cid：会话类型与会话ID。
     */
    bool IsActive(int ctype, int64_t cid) const;

    /**
     *  打开某个会话的字幕。已经开着时什么都不做、不发通知；打开成功时发 kRegistryEventStateChanged。
     *    ctype / cid：会话类型与会话ID。
     *    title：      会话名（单聊为对方显示名，群聊为群名），显示在工具条上；群聊同时用作行首的群名标注。
     *  返回：打开后是否处于字幕模式（字幕窗口建不出来时为 false）。
     */
    bool Enter(int ctype, int64_t cid, const std::wstring& title);

    /**
     *  关闭某个会话的字幕。没开着时什么都不做；关闭时发 kRegistryEventStateChanged。
     *    ctype / cid：会话类型与会话ID。
     */
    void Exit(int ctype, int64_t cid);

    /**
     *  切换某个会话的字幕：没开着时打开，否则关闭。
     *    ctype / cid / title：同 Enter。
     *  返回：切换后是否处于字幕模式。
     */
    bool Toggle(int ctype, int64_t cid, const std::wstring& title);

    /**
     *  把一条消息交给对应会话的字幕。那个会话没开字幕时忽略。
     *    ctype / cid：消息所属的会话。
     *    in：         换算好的消息（MakeSubtitleIncoming）。
     */
    void OnIncoming(int ctype, int64_t cid, const SubtitleIncoming& in);

    /**
     *  把一次撤回交给对应会话的字幕。那个会话没开字幕时忽略。
     *    ctype / cid：消息所属的会话。
     *    msgid：      被撤回消息的服务器消息 id。
     *    noticeText： 撤回提示。
     */
    void OnRecalled(int ctype, int64_t cid, long long msgid, const std::wstring& noticeText);

    /**
     *  更换某个会话的会话名（联系人改备注、群改名之后）。那个会话从没开过字幕时什么都不做。
     *    ctype / cid：会话类型与会话ID。
     *    title：      新的会话名。
     */
    void SetTitle(int ctype, int64_t cid, const std::wstring& title);

    //关闭全部字幕（注销、退出程序时），不发通知；之后仍可再打开
    void ExitAll();

    /**
     *  临时隐藏或恢复显示全部字幕（客户端锁屏开始时隐藏、解锁时恢复，docs/锁屏方案.md 第 4.9 节）。
     *  对之后新建的会话对象同样生效；字幕模式的开关状态不变，不发通知。
     *    hidden：true 隐藏，false 恢复显示。
     */
    void SetAllHidden(bool hidden);

    //此刻处于字幕模式的会话个数
    int ActiveCount() const;

    /**
     *  取出此刻处于字幕模式的全部会话。
     *    out：先清空，再放入（会话类型, 会话ID）。
     */
    void ActiveConversations(std::vector<std::pair<int, int64_t> >& out) const;

private:
    CSubtitleRegistry(const CSubtitleRegistry&);
    CSubtitleRegistry& operator=(const CSubtitleRegistry&);

    //一个会话的字幕：会话对象连同它所属的集合与会话身份（会话对象通知时据此找回是哪个会话）
    struct Entry
    {
        CSubtitleRegistry* m_owner;    //所属的集合，不拥有
        int                m_ctype;    //会话类型
        int64_t            m_cid;      //会话ID
        CSubtitleSession   m_session;  //这个会话的字幕
    };

    //会话的键：（会话类型, 会话ID）
    typedef std::pair<int, int64_t> Key;

    //按会话找条目；没有时返回 NULL
    Entry* find(int ctype, int64_t cid) const;

    //按会话找条目，没有时新建一个（已按当前依赖初始化）
    Entry* findOrCreate(int ctype, int64_t cid);

    /**
     *  会话对象的通知回调：把「回到聊天窗」「已退出」换算成集合的事件转告主面板。
     *    ctx：  Entry 指针。
     *    event：SubtitleHostEvent。
     */
    static void sessionEventThunk(void* ctx, int event);

    //调用通知主面板的回调（有的话）
    void notify(int event, int ctype, int64_t cid, bool active);

    std::map<Key, Entry*>    m_entries;      //全部会话的字幕（含已退出的），本对象拥有，析构时 delete
    SubtitlePrefs*           m_prefs;        //账号的字幕偏好；不拥有，可以为 NULL
    SaveSubtitlePrefsFn      m_save;         //存盘回调；可以为 NULL
    void*                    m_saveCtx;      //存盘回调的上下文
    SubtitleRegistryCallback m_cb;           //通知主面板的回调；可以为 NULL
    void*                    m_cbCtx;        //通知主面板的回调的上下文
    CreateSubtitleViewFn     m_viewFactory;  //建立视图的工厂；NULL 表示默认
    void*                    m_viewCtx;      //工厂的上下文
    bool                     m_hidden;       //全部字幕是否临时隐藏（客户端锁屏期间为 true），新建的会话对象照此设置
};

} // namespace subtitle
