/**
 *  发帖窗口 CThreadComposeWindow（docs/帖子群方案.md 4.2、4.3、4.6，布局见 ThreadComposeLayout.h）：
 *  发新帖、编辑自己的帖子、追加补充共用一个窗口，帖子群窗口持有一个本对象，同一时间最多打开一个。
 *
 *  流程：
 *    - 帖子群窗口调 Open 打开（编辑时带入原帖的标题、正文、图片与 @ 的人）；
 *    - 点「发布 / 保存 / 补充」或按 Ctrl+Enter：本地校验（图片是否都已上传、字数、空内容），不通过时在底部说明原因；
 *      通过后经会话控制器发请求，按钮置灰，等应答；
 *    - 应答经帖子群窗口的 OnActionDone 转来（OnActionDone 返回 true 表示是本窗口的请求）：成功时关闭窗口，失败时说明原因；
 *    - 关闭前内容有改动且没有发出时先确认。
 *  窗口有所有者（帖子群窗口），按 UI 约定第 11 条用 WS_OVERLAPPEDWINDOW 建窗；标题栏有程序图标（UI 约定第 10 条）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏的程序图标
#include "../Core/ThreadComposeModel.h"
#include "../Core/ThreadTypes.h"
#include "../Host/ThreadWindowApi.h"
#include "ThreadBitmapCache.h"
#include "ThreadComposeLayout.h"
#include "ThreadFacePicker.h"
#include "ThreadMemberPicker.h"

namespace threadgroup {

class CThreadSession;

//发帖窗口投给自己、留到当前控件通知或按键处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x185）：
//wParam 为 ThreadComposeDeferred，lParam 随动作而定
const UINT kMsgThreadComposeDeferred = WM_APP + 0x185;

//kMsgThreadComposeDeferred 的 wParam
enum ThreadComposeDeferred
{
    compose_defer_submit       = 1,  //点了「发布 / 保存 / 补充」或在正文里按了 Ctrl+Enter：校验并发出
    compose_defer_cancel       = 2,  //点了「取消」：有改动时先确认再关闭
    compose_defer_open_face    = 3,  //点了「表情」：在按钮下方打开表情浮层
    compose_defer_face_picked  = 4,  //表情浮层里选中了一个表情：lParam 为表情编号，插到正文的光标处
    compose_defer_add_image    = 5,  //点了「图片」或「＋ 图片」：选择本机图片并上传
    compose_defer_thumb        = 6,  //点了缩略图：lParam 低 16 位为序号，高 16 位为 ComposeThumbClick（删除或重试）
    compose_defer_open_members = 7,  //点了「@ 成员」：打开选人浮层
    compose_defer_mention_chip = 8,  //点了已 @ 名单里的一个：lParam 为序号，从名单里去掉
    compose_defer_member_picked= 9,  //选人浮层里选中了一个人：lParam 为堆上的用户 ID（int64_t*），由本窗口 delete
};

//打开发帖窗口时的参数
struct ComposeRequest
{
    int               m_mode;          //模式，取值见 ComposeMode
    int64_t           m_postId;        //编辑帖子与追加补充时的帖子；发新帖时为 0
    std::wstring      m_groupName;     //群名（窗口标题用）；取不到时为空串
    std::wstring      m_postCaption;   //追加补充时窗口标题里的帖子标题或正文开头
    int               m_addendumIndex; //追加补充时这是第几段（1 起）
    const ThreadPost* m_post;          //编辑帖子时的原帖（借用，只在 Open 里读取）；其余模式为 NULL

    ComposeRequest()
        : m_mode(compose_mode_new_post)
        , m_postId(0)
        , m_addendumIndex(1)
        , m_post(NULL)
    {
    }
};

/**
 *  CThreadComposeWindow —— 发帖窗口（非模态，所有者为帖子群窗口）。只在界面线程上使用。
 */
class CThreadComposeWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  @param groupId 帖子群
     *  @param session 会话控制器（借用，帖子群窗口保证比本对象存活久）
     *  @param owner   模块（借用）：表情库、图片上传与下载、宿主
     */
    CThreadComposeWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner);
    virtual ~CThreadComposeWindow();

    /**
     *  打开窗口。已经打开时：模式与帖子相同则只把窗口提到前面；不同且内容没有改动则换成新的内容；
     *  不同且有改动则只把窗口提到前面、返回 false（调用方提示用户先处理正在编辑的内容）。
     *  @param ownerWnd 所有者窗口（帖子群窗口）
     *  @param request  打开参数
     *  @return 窗口按 request 打开或已经是这份内容返回 true
     */
    bool Open(HWND ownerWnd, const ComposeRequest& request);

    //关闭窗口，不确认、不保留内容（帖子群窗口关闭、帖子被删除时用）
    void Close();

    //窗口是否开着
    bool IsOpen() const;

    //当前模式（ComposeMode）；没有打开时为 0
    int Mode() const;

    //编辑帖子与追加补充时的帖子；其余为 0
    int64_t PostId() const;

    /**
     *  帖子群窗口收到的操作结果先交给本窗口。
     *  @param cmd  命令号（ThreadCmd）
     *  @param code 结果（ThreadError）
     *  @param id   成功时的帖子 ID 或补充 ID
     *  @return 是本窗口发出的发帖、编辑或补充请求返回 true（帖子群窗口就不再弹提示）
     */
    bool OnActionDone(int cmd, int code, int64_t id);

    /**
     *  图片上传的结果先交给本窗口。
     *  @return 请求号属于本窗口时返回 true
     */
    bool OnImageUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath);

    /**
     *  图片下载的结果（编辑帖子时原有图片的缩略图）先交给本窗口。
     *  @return 请求号属于本窗口时返回 true
     */
    bool OnImageReady(uint32_t requestId, bool ok, const std::wstring& localPath);

    /**
     *  群成员名单到了（「@ 成员」时经宿主请求的，帖子群窗口转来）。
     *  @param members 成员，带群角色与显示名（群名片或昵称）；为空表示没有拉到
     */
    void OnGroupMembers(const std::vector<ThreadMember>& members);

    //群资料变了（可能有人入群、退群或改了群名片）：丢掉缓存的成员名单，下次「@ 成员」时重新向宿主请求
    void InvalidateMembers();

    BEGIN_MSG_MAP(CThreadComposeWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgThreadComposeDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4 时，内容有改动先确认
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：清掉控件指针与未完成的状态，不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮单击与输入框文字变化（会改动控件树的动作一律延后执行，UI 约定第 13 条）
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgThreadComposeDeferred：执行投给自己的动作
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按 m_request 建窗口、建控件树、带入编辑内容
    bool CreateForRequest(HWND ownerWnd);

    //窗口标题：「发帖 · 群名」「编辑帖子 · 群名」「追加补充 · 帖子标题」
    std::wstring WindowTitle() const;

    //编辑帖子时带入原帖：标题、正文（表情换回「[表情名]」）、原有图片（顺带下载缩略图）、@ 的人
    void LoadPost(const ThreadPost& post);

    //当前的标题（去掉首尾空白）；追加补充时为空串
    std::wstring CurrentTitle() const;

    //当前正文的线上形式（「[表情名]」换成 /f["编号"]）
    std::wstring CurrentWire() const;

    //刷新标题与正文的字数（超出上限时变红）
    void UpdateCounts();

    //按模型重建图片行
    void RebuildImages();

    //弹出选择文件对话框（可多选），把选中的图片加进图片行并逐张交给宿主上传；超出 9 张的部分不加并说明
    void PickImages();

    /**
     *  加一张本机图片并开始上传（先登记进模型，再交给宿主）；宿主没能开始上传时这张图标为上传失败。
     *  @param path 本机路径
     */
    void StartImageUpload(const std::wstring& path);

    /**
     *  单击了图片行里的一张缩略图（延后到点击通知处理完之后）。
     *  @param index 序号（0 起）
     *  @param where 点在了哪里，取值见 ComposeThumbClick：「✕」删除；图本身上传失败时重试，已上传时用看图窗口打开
     */
    void HandleThumbClick(size_t index, int where);

    //按模型重建已 @ 名单行，名单为空时隐藏该行
    void RebuildMentions();

    //校验并发出（发帖、保存或补充）；不通过或没有发出时在底部说明原因
    void Submit();

    //「取消」或关闭：内容有改动时先确认，确认后关闭
    void RequestClose();

    //在底部显示出错说明；传空串清掉
    void SetError(const std::wstring& text);

    //发出中：按钮置灰、文字改为「发布中…」等；结束后恢复
    void SetSubmitting(bool submitting);

    //在「表情」按钮下方打开表情浮层
    void OpenFacePicker();

    //表情浮层的选中回调（user 为本窗口）：投递消息，等浮层的点击处理完再插入
    static void FacePickedThunk(void* user, int faceId);

    //把一个表情以「[表情名]」插到正文的光标处
    void InsertFace(int faceId);

    //在「@ 成员」按钮上方打开选人浮层；还没有成员名单时先向宿主请求
    void OpenMemberPicker();

    //选人浮层的选中回调（user 为本窗口）：投递消息，等浮层的通知处理完再插入
    static void MemberPickedThunk(void* user, int64_t userId);

    //@ 一个人：加进名单（已在名单里时不重复加），在正文光标处插入「@名字 」；名单已满时说明原因
    void InsertMention(int64_t userId);

    /**
     *  一个用户的显示名：先取成员名单里的（群名片或昵称，与普通群 @ 的口径相同），名单里没有时再经宿主取，
     *  都取不到时用 ID。插入正文的「@名字」只来自选人浮层，因此总是名单里的名字。
     */
    std::wstring UserName(int64_t userId) const;

    //按成员名单准备选人浮层的条目：去掉本人，群主、管理员在前
    std::vector<MemberPickEntry> MemberEntries() const;

    //建窗口后取正文字体、画工具图标；窗口销毁后释放图标
    void CreateResources();
    void DestroyResources();

    //在所有者窗口的中央摆放窗口（所有者不可见时在鼠标所在显示器的工作区居中）
    void CenterOnOwner(HWND ownerWnd);

private:
    int64_t              m_groupId;       //帖子群
    CThreadSession*      m_session;       //会话控制器（借用）
    IThreadWindowOwner*  m_owner;         //模块（借用）
    ComposeRequest       m_request;       //当前打开的参数（m_post 只在 Open 期间有效，之后置 NULL）
    std::unique_ptr<CThreadComposeModel> m_model;   //内容模型（图片、@ 的人、改动判断），每次打开新建
    ComposeControls      m_controls;      //控件树里要用到的控件（归控件树所有，窗口销毁后失效）
    HFONT                m_font;          //正文字体（借用 balloonui 按窗口 DPI 给的默认字体）；窗口创建后有效
    HBITMAP              m_iconFace;      //「表情」按钮的图标（本对象持有），窗口销毁后释放
    HBITMAP              m_iconImage;     //「图片」按钮的图标（本对象持有），窗口销毁后释放
    CThreadBitmapCache   m_bitmaps;       //缩略图位图（图片行的格子借用），窗口销毁后释放
    CThreadFacePicker    m_facePicker;    //表情选择浮层，本对象持有
    CThreadMemberPicker  m_memberPicker;  //「@ 成员」的选人浮层，本对象持有
    CThreadMemberBook    m_members;       //群成员名单（宿主给的，含本人，取条目时去掉）；跨多次打开复用
    std::vector<ThreadAtCandidate> m_seedAt;   //编辑帖子时原帖里 @ 的人与当时插入的名字（提交前重扫用）；每次打开时清空
    bool                 m_membersPending;//已向宿主请求成员名单、还没有结果
    bool                 m_submitting;    //已发出请求、还没有应答（期间不再发，按钮置灰）
    int                  m_submitCmd;     //发出的请求的命令号（ThreadCmd），用来认领应答
    bool                 m_closing;       //正在关闭（确认过或提交成功）：WM_CLOSE 不再确认
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏的程序图标，本对象持有
};

} // namespace threadgroup
