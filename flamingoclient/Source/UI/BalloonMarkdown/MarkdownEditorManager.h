/**
 *  MarkdownEditorManager.h
 *
 *  Markdown 编辑器窗口的进程级管理器（docs/Markdown消息方案.md 第 11.3 节）：打开新的编辑器窗口、持有全部打开着的
 *  编辑器、在退出程序前询问未保存的文档。编辑器不依赖登录状态，登出时不关闭，只是发送不可用；因此管理器归进程
 *  所有（单例，从不析构），而不是归主面板。
 *
 *  入口有三个：系统主菜单与托盘菜单打开空白编辑器（不预选发送对象），聊天窗工具栏的 Markdown 编辑器按钮与 Markdown
 *  消息的「重新编辑」打开时预选该会话。FlamingoMarkdown.dll 不可用时这些入口都不出现，调用方不必再判断。
 *
 *  典型用法：
 *      balloonmd::MarkdownSendTarget target;
 *      target.m_isGroup = false;
 *      target.m_id = peerUserId;
 *      target.m_name = peerName;
 *      balloonmd::CMarkdownEditorManager::GetInstance().OpenEditor(std::string(), &target);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

namespace balloonmd {

class CMarkdownEditorWindow;
class CMarkdownPreviewWindow;
class IMarkdownEditorHost;

/**
 *  预览窗口要显示的一篇文档（点聊天窗里的 Markdown 卡片时由聊天窗填好）。
 */
struct MarkdownPreviewDoc
{
    std::string  m_sourceUtf8;   //源文本（UTF-8）
    std::wstring m_title;        //窗口标题（卡片标题）；为空时为「Markdown 预览」
    std::wstring m_meta;         //正文上方的一行说明「发送者 · 时间」；可为空
    std::string  m_key;          //消息标识（服务器 msgid 或 clientMsgId）；同一标识只开一个窗口，为空时每次都新开
};

/**
 *  编辑器的一个发送对象：一个联系人或一个群。
 */
struct MarkdownSendTarget
{
    bool         m_isGroup;   //是否为群：true 群聊，false 单聊
    int64_t      m_id;        //单聊为对方用户 id，群聊为群 id；大于 0 才有效
    std::wstring m_name;      //显示名（昵称或群名）；为空时界面按 id 显示

    MarkdownSendTarget()
        : m_isGroup(false), m_id(0)
    {
    }
};

/**
 *  Markdown 编辑器窗口的进程级管理器。只在界面线程上使用；用 GetInstance() 取唯一实例。
 */
class CMarkdownEditorManager
{
public:
    /**
     *  取进程内唯一的实例（堆上分配，进程退出时不析构）。
     */
    static CMarkdownEditorManager& GetInstance();

    /**
     *  设置编辑器的宿主接口（登录状态、选择发送对象、发送）。登录前也可以打开编辑器，此时宿主报告未登录。
     *  @param host 宿主，借用；须活到进程退出，或在宿主销毁前再调用本方法传入 NULL
     */
    void SetHost(IMarkdownEditorHost* host);

    //当前的宿主；未设置时为 NULL
    IMarkdownEditorHost* GetHost() const;

    /**
     *  打开一个新的编辑器窗口并激活它。FlamingoMarkdown.dll 不可用时什么也不做（记一条日志）。
     *  @param sourceUtf8 预填的 Markdown 源文本（UTF-8）；为空时打开空白文档
     *  @param target     预选的发送对象；为 NULL 时不预选（从主菜单、托盘打开）
     *  @return 打开了窗口返回 true
     */
    bool OpenEditor(const std::string& sourceUtf8, const MarkdownSendTarget* target);

    //当前打开着的编辑器窗口个数
    size_t GetOpenCount() const;

    /**
     *  登录状态变化后调用：通知每个打开着的编辑器刷新底部的发送区。
     */
    void OnLoginStateChanged();

    /**
     *  退出程序前调用：有未保存修改的编辑器时询问一次，用户同意退出后关闭全部编辑器。
     *  @param owner 询问框的所有者窗口，可为 NULL
     *  @return 可以退出返回 true；用户选择取消返回 false
     */
    bool ConfirmCloseAllForExit(HWND owner);

    /**
     *  程序结束前调用（CAppShell::Shutdown）：不询问，直接关闭全部编辑器。用户主动退出时已经由
     *  ConfirmCloseAllForExit 询问过；其余退出路径（如自动更新）不能被询问框挡住。
     */
    void CloseAll();

    /**
     *  编辑器窗口销毁时由窗口自己调用，从列表中移除并释放它。
     *  @param window 正在销毁的窗口，不可为 NULL
     */
    void OnEditorDestroyed(CMarkdownEditorWindow* window);

    /**
     *  打开 Markdown 预览窗口；同一条消息（m_key 相同）已打开时激活已有的窗口。DLL 不可用时什么也不做。
     *  @param doc 要显示的文档
     *  @return 显示了窗口（新开或激活）返回 true
     */
    bool OpenPreview(const MarkdownPreviewDoc& doc);

    //当前打开着的预览窗口个数
    size_t GetPreviewCount() const;

    //关闭全部预览窗口（登出时调用：窗口里是上一个账号的消息内容）
    void ClosePreviews();

    /**
     *  预览窗口销毁时由窗口自己调用，从列表中移除并释放它。
     *  @param window 正在销毁的窗口，不可为 NULL
     */
    void OnPreviewDestroyed(CMarkdownPreviewWindow* window);

private:
    CMarkdownEditorManager();
    CMarkdownEditorManager(const CMarkdownEditorManager&);
    CMarkdownEditorManager& operator=(const CMarkdownEditorManager&);

    IMarkdownEditorHost*                 m_host;      //宿主接口，借用；未设置时为 NULL
    std::vector<CMarkdownEditorWindow*>  m_editors;   //打开着的编辑器窗口，按打开顺序；本对象持有（new 出来），窗口销毁时
                                                      //从列表移除并在消息处理结束后 delete（窗口类在头文件里只有前向声明，
                                                      //故不用 unique_ptr，释放放在 .cpp 里完整类型可见处）
    int                                  m_cascade;   //已打开过的窗口个数，用于计算错开位置，只增不减
    std::vector<CMarkdownPreviewWindow*> m_previews;  //打开着的预览窗口，按打开顺序；本对象持有，持有与释放方式同 m_editors
};

} // namespace balloonmd
