/**
 *  ChatBubble.h
 *
 *  单聊窗里的一条消息行 —— 对照 design 07 / chat.jsx 的 MessageBubble。
 *  一条消息一个 ChatBubble：可能是居中时间分隔，也可能是带头像的气泡
 *  （对方靠左 / 自己靠右），气泡内容支持文字 / emoji / 图片 / 文件，
 *  气泡上方可带引用块、自己消息下方带"✓ 已读"回执。
 *
 *  文字气泡支持自动折行：超过气泡最大宽度的长文字在气泡内换行、气泡
 *  随之变高。折行布局由 Measure() 按消息区可用宽度测量，须在排版
 *  （GetRowHeight / OnPaint）之前调用。
 *
 *  本行还支持右键上下文菜单（复制 / 引用回复 / 转发 / 撤回 / 删除 /
 *  多选）；进入多选模式后行首显示勾选圈、左键点击即切换选中。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"
#include "ChatTextSelection.h"   // SelLayout（成员需完整类型）
#include "ChatLinkDetect.h"      // LinkSpan（成员需完整类型）
#include <string>

//图片气泡解码用的 GDI+ 位图前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonchatwnd {

/**
 *  ChatBubble —— 一条消息行（时间分隔 / 气泡消息）。自绘控件。
 */
class ChatBubble : public balloonwjui::DuiControl
{
public:
    ChatBubble();
    ~ChatBubble();

    // 设置本行承载的消息（拷贝一份）。
    void SetMessage(const ChatMessage& msg);

    // 取本行消息的客户端消息ID（图片发送态更新时按它定位本行）。空=无。
    const std::string& ClientMsgId() const { return m_msg.clientMsgId; }

    // 更新图片上传进度（0-100）并置为"上传中"态；触发重绘。
    void SetImageUploadProgress(int percent);

    // 把图片标为"上传/发送失败"态；触发重绘。
    void SetImageUploadFailed();

    // 把图片置为"正常"态（上传完成 / 接收完成）；触发重绘。
    void SetImageUploadDone();

    // 取本行消息的服务器 msgid（收到的图片下载完成后按它定位本行）。0=无。
    long long Msgid() const { return m_msg.msgid; }

    // 收图下载完成：填本地缓存路径、置正常态、丢弃旧位图缓存以重新解码；触发重绘。
    void SetImageDownloaded(const CString& localPath);

    // 收图下载失败：置"失败"态；触发重绘。
    void SetImageDownloadFailed();

    // ---- 文件气泡（kMsgFile）的收发态更新 ----
    // 注：文件气泡高度随状态变化（有无进度条 / 按钮行），故 ChatMessageList 在调用这些
    // 之后须 ForceLayout 重排（不能像图片那样只 Invalidate）。

    // 取本行文件消息的客户端消息ID（发送态更新时按它定位本行）。空=无。
    const std::string& FileClientMsgId() const { return m_msg.fileClientMsgId; }

    // 取本行文件消息的服务器 msgid（下载态更新时按它定位本行）。0=无。
    long long FileMsgid() const { return m_msg.fileMsgid; }

    // 取本行文件当前传输进度（0-100）。供列表按整数百分比节流强制重绘用。
    int FileProgress() const { return m_msg.fileProgress; }

    // 更新文件上传进度（0-100）并置"上传中"态；触发重绘。
    void SetFileUploadProgress(int percent);

    // 文件上传完成：置"完成"态、进度满格；触发重绘。
    void SetFileUploadDone();

    // 文件上传失败：置"失败"态（保留当前进度作红色停位）；触发重绘。
    void SetFileUploadFailed();

    // 更新文件下载进度（0-100）并置"下载中"态；触发重绘。
    void SetFileDownloadProgress(int percent);

    // 文件下载完成：填本地落地路径、置"完成"态；触发重绘。
    void SetFileDownloaded(const CString& localPath);

    // 文件下载失败：置"失败"态；触发重绘。
    void SetFileDownloadFailed();

    // 文件传输已取消：置"已取消"态（保留当前进度作灰色停位）；触发重绘。
    void SetFileCancelled();

    // 本条文件是否可"重新发送"（而非"重新下载"）：需同时具备与发送中气泡对回的
    // clientMsgId 和仍在原处的本地源文件，即只有本端刚发出的那笔才行；历史里自己
    // 发过的文件两者皆无，只能从服务器重新下载。决定重试按钮的文案与宿主的分派。
    bool CanResendFile() const;

    // 用户拒绝接收该文件：置"已拒绝"态（不下载、不通知发送方）；触发重绘。
    // 该态仍提供「重新下载」，用户反悔时可再收。
    void SetFileRejected();

    // 用户点「接收」/「重新下载」后真正发起了下载：置"下载中"态并把进度清零；触发重绘。
    void SetFileDownloadStarted();

    // 用户点「重新发送」后重新入队上传：置"上传中"态并把进度清零；触发重绘。
    void SetFileUploadStarted();

    // 更新"对端把这个文件收到什么程度了"（仅本端发出的文件气泡有意义）；触发重绘。
    // 不改 fileState —— 本端这一侧传得怎么样与对端收得怎么样是两回事。
    //   peerState：取值见 ChatFilePeerState。
    void SetFilePeerState(int peerState);

    // 文件已删除 / 失效（服务端已不存在）：置"已删除"态、文件名加删除线；触发重绘。
    void SetFileDeleted();

    // 只读取回本行承载的消息（右键菜单复制 / 引用等需要）。
    const ChatMessage& GetMessage() const { return m_msg; }

    /**
     *  让折行测量与渲染缓存作废，使下一次排版重新测量本行。
     *
     *  用于"消息内容没变、但渲染口径变了"的场合 —— 目前只有"自动识别链接"开关被
     *  改动时用得上：链接片段是在 Measure 里算的，不重新测量就还是旧的那一份。
     *  与 SetMessage 不同，本方法<u>不</u>动消息内容与选区。
     */
    void InvalidateMeasureCache();

    /**
     *  回填服务器分配的消息 ID 与消息时间（本端发出、服务器回执到达后调用）。
     *
     *  <u>只</u>改这两个字段。它们既不参与折行也不参与绘制，仅供撤回时限判断
     *  （见 .cpp 里 canRecallMessage 的调用），故不置脏、不重排、不重绘。
     *
     *  不要退回用 SetMessage 做这件事：那是"整条换掉"的重路径，会连带清空选区
     *  模型、把折行测量缓存置为失效，而回填方本就没有理由触发重排 —— 于是这一行
     *  在下次重排之前都选不中文字（表现为回执一到就选不了，改一下窗口大小又好了）。
     *    msgid：       服务器分配的消息 ID。
     *    timestampMs： 消息时间（Unix 毫秒）。
     */
    void BackfillServerIds(long long msgid, long long timestampMs);

    /**
     *  回填本端发出的图片的 fileid / md5（上传成功后）。
     *  这两项不参与折行也不参与绘制，只在本条被引用时写进 content.quote，
     *  故与 BackfillServerIds 一样：不置脏、不重排、不重绘。
     */
    void SetImageIds(const CString& fileid, const CString& md5)
    {
        m_msg.imageFileId = fileid;
        m_msg.imageMd5    = md5;
    }

    // 本行发送者 userid（真图头像下载完成后宿主按它定位本行刷新；0=未知）。
    int64_t SenderUserId() const { return m_msg.senderUserId; }

    // 设置 / 更新发送者真实图片头像本地路径（真图下载完成后宿主调用）。路径变化时
    // 丢弃旧位图缓存以重新解码并触发重绘；传空串则回退画字形头像。
    void SetSenderAvatarImage(const CString& path);

    // 按消息区可用宽度测量文字气泡的折行布局（折行后气泡的宽 / 高）。
    // 须由 ChatMessageList 在排版时对每行调用一次；availWidth 变化
    // （窗口缩放）后须重新调用。非文字消息调用本函数无副作用。
    //   availWidth：消息区可用宽度（逻辑像素）。
    void Measure(int availWidth);

    // 本行所需高度（逻辑像素）—— 供 ChatMessageList 排版用。文字气泡的
    // 高度取自最近一次 Measure() 的折行结果（未测量则回退到单行高）。
    int GetRowHeight() const;

    // ---- 性能探针（测试 / 排查用）-------------------------------------
    // 统计"重量级折行测量"（GetDC + 建字体 + DrawText(DT_CALCRECT|
    // DT_WORDBREAK)）实际执行的次数。Measure 命中缓存时会在该路径之前
    // 返回，故此计数恰等于真正发生的昂贵测量次数 —— 用来量化 resize 时
    // 的重复测量浪费、并作为缓存修复的回归断言依据。进程级全局计数。
    static unsigned long HeavyMeasureCount();
    // 把上述计数清零（一次度量 / 一个用例开始前调用）。
    static void ResetHeavyMeasureCount();

    // 把本行就地变成一条居中的系统提示行，显示"某某撤回了一条消息"。收到服务端
    // cmd_message_recalled_notify 推送后调用。调用后本行 kind 变为时间分隔样式、
    // 行高随之改变，调用方须触发一次重排。
    //   tipText：提示文案，由 balloonchat::BuildRecallTipText 按撤回者是否本人生成
    //            （"你撤回了一条消息" / "张三撤回了一条消息"）。传空串时本行会变成
    //            一条空提示，调用方应保证非空。
    void RecallToTip(const CString& tipText);

    // 本行当前是否被 Pin 住（右键菜单据它决定那一项的文案是「Pin」还是「取消 Pin」）。
    bool IsPinned() const { return m_msg.pinned; }

    // 设置本行的 Pin 标记（收到服务端 cmd_message_pinned_notify 推送后由消息列表调用）。
    // 标记画在气泡<u>外侧</u>、不占内容宽度也不改行高，故只触发重绘、不必重排。
    //   pinned：true 钉住、false 取消。
    void SetPinned(bool pinned);

    // 多选模式下本行是否被选中 / 设置选中态。SetSelected 会触发重绘。
    bool IsSelected() const { return m_selected; }
    void SetSelected(bool selected);

    // ---- 气泡内文字选中（拖拽选择 + 复制）------------------------------
    // 注意与上面的"多选模式"是两回事：那个是整行勾选用于转发 / 删除，这个是
    // 在一条消息内部选中部分文字。

    /**
     *  本气泡当前是否有非空的文字选区。
     */
    bool HasTextSelection() const;

    /**
     *  取当前选中的文字。内联表情会被替换成 "[表情]"（与会话列表预览同口径）。
     *  @return 选中文本；无选区时返回空串。
     */
    CString GetSelectedText() const;

    /**
     *  清除文字选区并重绘。点到别处、切换消息、进入多选模式时调用。
     */
    void ClearTextSelection();

    /**
     *  把本气泡的全部文字设为选中（三击 / 菜单"全选"用）。非文字消息无副作用。
     */
    void SelectAllText();

    // DuiControl 覆写：绘制本行。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 同名隐藏 DuiControl::Invalidate（基类非虚）：本行任何内容变更都经
    // Invalidate() 请求重绘，这里顺带把渲染缓存置脏，使下次 OnPaint 重画缓存。
    // 内部各 setter 调的无限定 Invalidate() 都解析到本版本，故无需逐处置脏；
    // 框架经 DuiControl* 发起的非内容重绘走基类、不置脏（缓存仍有效，语义正确）。
    void Invalidate();

    // DuiControl 覆写：滚轮转发给祖先消息列表（气泡铺满列表，滚轮多落在
    // 气泡上，须主动交还）。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // DuiControl 覆写：左键点击 —— 文字气泡开始拖拽选择；多选模式下切换本行选中态。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：左键抬起 —— 结束拖拽选择、释放鼠标捕获。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：右键弹出上下文菜单（复制 / 引用回复 / 转发 /
    // 撤回 / 删除 / 多选）。时间分隔 / 撤回提示行不弹菜单。
    bool OnRButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：双击 —— 仅当本行是已就绪的图片消息、且双击点落在图片
    // 矩形内时，向宿主聊天窗发 WM_VIEW_CHAT_IMAGE 请求打开看图窗放大查看。
    // 其余情况不消费。需宿主对 DuiHost 开启 EnableDoubleClick(true) 才会触发。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标移动 —— 文件行据落点更新底部操作链接的悬停高亮（hover
    // 变化时重绘，使悬停链接变深色 + 加下划线）。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标离开本行 —— 清除文件链接悬停态并重绘。
    bool OnMouseLeave() override;

    // DuiControl 覆写：光标落在文件操作链接上时显示手型（超链接感），否则普通箭头。
    bool OnSetCursor(POINT pt) override;

private:
    /**
     *  落点命中了第几条超链接。
     *
     *  命中测试在"相对文本区原点"的坐标系里做（与 @提醒 的 TryPopupMentionMenu
     *  同一套），故非文字气泡、尚未测量、或本条没有链接时一律返回 -1。
     *    pt：落点（宿主客户区坐标）。
     *  @return m_linksFlat 的下标；没命中返回 -1。
     */
    int LinkAtPoint(POINT pt) const;

    /**
     *  请宿主聊天窗打开第 index 条链接（投 WM_CHAT_OPEN_LINK，URL 用堆上的
     *  std::wstring 传递，宿主处理后 delete）。气泡自己不碰 ShellExecute ——
     *  协议白名单校验只该有一处，放在宿主窗里。
     *    index：m_linksFlat 的下标；越界时什么都不做。
     */
    void OpenLinkByIndex(int index);


    // 本行是否是"可被选择 / 可弹菜单的真实消息"（时间分隔与撤回提示
    // 行返回 false）。
    bool IsRealMessage() const;

    // 取祖先消息列表是否处于多选模式（找不到列表则按 false）。
    bool ParentInMultiSelect() const;

    // 本行是否可走"渲染缓存"：文字 / emoji / 时间分隔行可缓存（绘制时不记录任何
    // 宿主坐标系命中矩形）。图片 / 文件行不可缓存 —— 它们绘制时要记下双击图片、
    // 点按钮的命中矩形（m_imageRect / m_fileBtnRect），缓存会让这些矩形失效。
    bool IsRenderCacheable() const;

    // 把本行按 m_rcItem 的相对坐标实际绘制到 hdc（原 OnPaint 主体）。OnPaint 据
    // IsRenderCacheable 决定直接调它（图片 / 文件行）还是先画进缓存位图再 BitBlt。
    void RenderContent(HDC hdc);

    // 本行是不是"可以选中文字"的文字类消息（纯文字 / emoji 大表情之外的文字气泡）。
    // 时间分隔、撤回提示、图片、文件行都不参与文字选中。
    bool IsTextSelectable() const;

    // 把宿主客户区坐标换算成文本区内的坐标，再映射成字符下标。
    // 落点在文本区之外时，靠 CharIndexFromPoint 自身的夹取规则归到首 / 尾。
    int CharIndexFromHostPoint(POINT pt) const;

    // 取本行文本区左上角（宿主客户区坐标）。选区矩形与命中测试都以它为原点。
    // 未测量 / 非文字消息时返回 (0,0) 并令 okOut 为 false。
    POINT TextOriginInHost(bool& okOut) const;

    // 把当前选区画成高亮矩形（在文字之下）。hdc 为本行绘制目标，origin 为文本区原点
    // （已按缓存平移过），故这里只做相对到绝对的偏移。
    void DrawTextSelection(HDC hdc, const POINT& origin) const;

    // 若落点命中某个 @某人 片段，则弹出该片段的右键菜单并把选中项投给宿主聊天窗。
    //   pt：   落点（宿主客户区坐标）。
    //   hwnd： 宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 命中并已处理（无论用户是否选了菜单项）返回 true；没命中返回 false，
    //         调用方据此继续弹气泡自己的菜单。
    bool TryPopupMentionMenu(const POINT& pt, HWND hwnd);

    // 本行发送者头像的矩形（宿主客户区坐标）。几何与 RenderContent 里画头像那段
    // 逐字一致：自己发的靠右、别人发的靠左，边长 kAvatarSize。
    // 时间分隔 / 撤回提示行没有头像，此时返回空矩形。
    RECT AvatarRectInHost() const;

    // 若落点命中本行的发送者头像，则弹出该发送者的右键菜单（与 @某人 片段、群成员
    // 侧栏共用同一套模型与分发消息）并把选中项投给宿主聊天窗。
    //
    // <u>只在群聊里生效</u>：单聊窗上下就两个人，对着头像弹"与 TA 聊天 / @TA"没有意义。
    // 判据是本行带不带发送者名（showSender 那套口径，只有群聊才填 senderName）。
    // 自己发的那条也不弹 —— 与 @ 到自己时只留「查看资料」是同一个道理，但这里连
    // 菜单都不必出：右键点自己的头像本就不是个有意义的动作。
    //   pt：   落点（宿主客户区坐标）。
    //   hwnd： 宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 命中并已处理返回 true；没命中返回 false，调用方据此继续往下走。
    bool TryPopupAvatarMenu(const POINT& pt, HWND hwnd);

    // 建出成员操作菜单、弹出、把选中项投给宿主聊天窗。@某人 片段与发送者头像两个
    // 入口共用它 —— 两处菜单本来就该逐字一样，各建一份迟早会走样。
    //   uid：        目标成员 userid。
    //   displayName：目标成员显示名（不含 '@'），复制昵称 / 再 @ 一次要用。
    //   isSelf：     目标是不是本人（决定菜单只留「查看资料」）。
    //   pt：         落点（宿主客户区坐标）。
    //   hwnd：       宿主窗口句柄，菜单的属主、也是投递目标。
    // @return 菜单弹出过（无论用户是否选了项）返回 true；模型为空、建菜单失败返回 false。
    bool PopupMemberMenu(long long uid, const std::wstring& displayName, bool isSelf,
                         const POINT& pt, HWND hwnd);

    // 取落点 pt（宿主客户区坐标）命中的文件操作链接下标（0~2）；未命中返回 -1。
    // 供悬停高亮（OnMouseMove）、手型光标（OnSetCursor）、点击（OnLButtonDown）共用。
    int FileLinkAtPoint(POINT pt) const;

    // 确保渲染缓存位图存在且为 w×h；尺寸变化则重建。ref 提供兼容 DC。
    void EnsureRenderCache(HDC ref, int w, int h);
    // 释放渲染缓存位图（尺寸变 / SetMessage 等内容变 / 析构时）。
    void ReleaseRenderCache();

    // 释放缓存的头像真图位图（换头像路径 / SetMessage / 析构时调用）。
    void ReleaseAvatarBitmap();

    // 文件气泡底部按钮被点击：组 ChatFileActionData 经 WM_CHAT_FILE_ACTION 发给宿主
    // 聊天窗集中处理（打开 / 另存为 / 取消 / 重试）。action 取值见 ChatFileActionKind。
    void HandleFileButton(int action);

    // 图片气泡右键选了「图片另存为」/「再次发送」：组 ChatImageActionData 经
    // WM_CHAT_IMAGE_ACTION 发给宿主聊天窗执行。默认另存名在这里就按本行的
    // imagePath / msgid 算好，宿主拿到即用。
    //   hwnd：   宿主聊天窗窗口句柄，调用方须保证非 NULL；
    //   menuId： 选中的菜单项，取值见 ChatMessageMenuId（仅上述两项）。
    void PostImageMenuAction(HWND hwnd, int menuId);

private:
    ChatMessage m_msg;            // 本行承载的消息

    // 注：图片（kMsgImage）的"解码原图 + 缩放到展示尺寸"已移到后台线程的
    // ChatImageCache（按路径 + 展示尺寸缓存）。气泡绘制时只查缓存、命中即画，未命中
    // 画占位并请求后台解码，故本类不再持有任何图片位图。

    // 发送者真图头像解码缓存：首次绘制时按 m_msg.senderAvatarImagePath 解码、之后复用，
    // 避免每帧重新解码。SetMessage 换消息 / SetSenderAvatarImage 换路径 / 析构时释放。
    Gdiplus::Bitmap* m_avatarBmp;        // 解码后的头像真图；NULL=尚未解码 / 无真图 / 解码失败
    bool             m_avatarLoadTried;  // 是否已尝试过解码头像真图（失败也置位，避免每帧重试）

    // kMsgImage 行最近一次绘制时图片在宿主客户区坐标系里的矩形，供双击命中测试
    //（双击图片打开看图窗）。仅图片行有效；非图片行不读取此值。
    RECT             m_imageRect;

    // kMsgFile 行最近一次绘制时底部各按钮在宿主客户区坐标系里的矩形与动作，供左键
    // 命中测试（点击打开 / 打开所在文件夹 / 另存为 / 取消 / 重试）。m_fileBtnCount 为
    // 本次绘制的按钮数（0~3），随文件态变化；仅文件行有效。动作取值见 ChatFileAction.h。
    RECT             m_fileBtnRect[3];
    int              m_fileBtnAction[3];
    int              m_fileBtnCount;

    // kMsgFile 行底部操作链接当前悬停的下标（0~2）；-1=无。OnMouseMove / OnMouseLeave
    // 维护，OnPaint 据它把悬停链接画成深色 + 下划线。
    int              m_fileHoverIdx;

    // ---- Measure() 产出的文字气泡折行布局缓存 ----
    int  m_measuredWidth;         // 上次 Measure 的 availWidth；-1=未测量
    bool m_measuredMultiSel;      // 上次 Measure 时所属列表是否处于多选模式
                                  // （多选会让出勾选列、收窄气泡可用宽度，
                                  //  故它与 availWidth 一起构成折行缓存键）
    int  m_textBubbleW;           // kMsgText 折行后气泡宽（含内边距）
    int  m_textBubbleH;           // kMsgText 折行后气泡高（含内边距，可多行）

    bool m_selected;              // 多选模式下本行是否被选中

    // ---- 气泡内文字选中的状态 ----
    // m_selLayout / m_selFlatText 由 Measure() 在折行测量的同时产出（两者必须同源，
    // 否则选区矩形会与实际画出来的文字错位）；m_selAnchor / m_selCaret 是拖拽的两个
    // 端点（下标，非负时有效），m_selDragging 表示正在拖。
    // 引用块在<u>本行内</u>的矩形（相对 m_rcItem 左上角），绘制时记下、左键命中时用。
    // 刻意存相对坐标而不是宿主坐标：本行的绘制结果有渲染缓存，缓存期间 m_rcItem 会被
    // 临时移到 (0,0)，存宿主坐标会失效（图片 / 文件行正因为要存宿主坐标才不能缓存）。
    RECT m_quoteRectRel;

    // 引用缩略图入队解码的日志只打一次（绘制每帧都会走到那一支）。
    bool m_quoteThumbLogged;

    SelLayout    m_selLayout;      // 字符级排版结果（供命中测试与选区矩形）
    std::wstring m_selFlatText;    // 扁平文本（表情用 U+FFFC 占位）

    // m_msg.ats 换算到<u>扁平文本</u>下标后的副本，与 m_selLayout 同一套口径，故可
    // 直接喂给 BuildSelectionRects 拿到 @ 片段的矩形（画底色块、命中测试都用它）。
    // m_msg.ats 本身记的是<u>发送正文</u>下标，正文里有表情或转义斜杠时两者不等，
    // 换算见 balloonchat::BuildFlatIndexMap。随 Measure 一起重建。
    std::vector<RenderSpan> m_atsFlat;

    // 本条正文里识别出的超链接，口径同样是<u>扁平文本</u>下标，与 m_selLayout 配套。
    // Measure() 里与选区模型一起产出；识别开关关掉时恒为空，于是渲染 / 命中 / 菜单
    // 三处都自然退化成"没有链接"，不需要各自再判一次开关。
    std::vector<LinkSpan> m_linksFlat;
    // 左键按下时命中的链接下标（-1 = 没按在链接上）。抬起时要求落在同一条链接上
    // 才算点击 —— 否则"从链接上按下、拖出去选中一段文字"会被误当成点击。
    int m_linkPressIdx;
    int          m_selAnchor;      // 选区锚点字符下标；-1 = 无选区
    int          m_selCaret;       // 选区当前端字符下标；-1 = 无选区
    bool         m_selDragging;    // 是否正在拖拽选择（鼠标按下未抬起）

    // ---- 渲染缓存（仅文字 / emoji / 时间分隔行）----
    // 把本行画好的位图缓存下来：内容 / 尺寸 / 多选态都不变时，OnPaint 直接 BitBlt
    // 这张缓存，省掉每帧用 GDI+ 重画气泡（拖动分隔条 / 滚动时的主要每帧开销）。
    // 失效条件：内容变（SetMessage / SetSelected / RecallToTip / 折行重测）置
    // m_renderDirty；行宽高变或多选态变由 OnPaint 比对 m_renderW/H/MultiSel 自动发现。
    HDC      m_renderDC;          // 缓存位图内存 DC；NULL=未建（或缓存创建失败）
    HBITMAP  m_renderBmp;         // 缓存位图
    HBITMAP  m_renderOldBmp;      // SelectObject 旧位图，释放时换回
    int      m_renderW;           // 缓存位图对应的行宽
    int      m_renderH;           // 缓存位图对应的行高
    bool     m_renderMultiSel;    // 缓存渲染时所属列表的多选态
    bool     m_renderDirty;       // 内容已变、需重画缓存

    // ---- 性能探针计数器 ----
    static unsigned long s_heavyMeasureCount;  // 重量级折行测量实际执行次数
};

} // namespace balloonchatwnd
