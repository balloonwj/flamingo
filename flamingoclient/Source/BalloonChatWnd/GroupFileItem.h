/**
 *  GroupFileItem.h
 *
 *  群聊窗右侧「群文件」面板里的一行 —— 彩色文件类型角标 + 文件名 + 「上传者·
 *  时间·大小」一行说明 + 右侧下载箭头。对照 design/群文件.png 的列表项。
 *  纯绘制控件：整行悬停浅灰底；右侧下载箭头单独 hover 高亮并显示手型光标，
 *  点击下载箭头冒泡下载动作（本增量仅记日志，真实下载在后续增量接 fileserver）。
 *
 *  视觉与现有边栏成员行 GroupMemberItem 同一套画法（GDI+ 画形状、GDI 画字），
 *  保持自绘聊天窗子系统风格统一。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

// 群文件行的固定行高（逻辑像素）。要容下两行文字（文件名 + 说明）；整体偏紧凑以便
// 在 240px 窄边栏里多显示文件名（放不下的完整名/说明走 tooltip）。
const int kGroupFileRowHeight = 42;

/**
 *  GroupFileItem —— 群文件列表的一行。
 *
 *  数据来源：增量2 用 mock 文件预填；增量3 起由 cmd_get_group_files 应答的
 *  GroupFileEntry 映射填充（文件名 / 扩展名 / 上传者 / 时间 / 大小 / entryid /
 *  fileid）。本控件只负责"画一行 + 下载箭头命中"，不持网络逻辑。
 */
class GroupFileItem : public balloonwjui::DuiControl
{
public:
    GroupFileItem();

    // 析构：从 DuiToolTipMgr 反注册本行的 tooltip（行是动态重建的，不反注册会留下
    // 悬空指针、hover 时崩）。
    ~GroupFileItem() override;

    /**
     *  设置本行文件的展示信息。
     *    name：     文件名（含扩展名），过长时尾部省略号。
     *    metaLine： 「上传者·时间·大小」整行说明（已由调用方拼好，如
     *               "谢知行 · 今天 14:31 · 128 KB"）。
     *    ext：      扩展名（不含点，大小写不敏感）；据此算彩色角标的颜色与标签文字。
     */
    void SetFile(const CString& name, const CString& metaLine, const CString& ext);

    /**
     *  设置本行对应的群文件 ID（供后续下载 / 删除按 id 定位；本增量仅暂存）。
     *    entryid： 群文件条目ID（字符串编码int64，删除用）。
     *    fileid：  文件本体ID（字符串编码int64，下载用）。
     */
    void SetIds(const CString& entryid, const CString& fileid);
    const CString& EntryId() const { return m_entryid; }
    const CString& FileId() const { return m_fileid; }

    /**
     *  进入 / 更新「下载中」态：右侧下载箭头换成百分比文字，行底叠一条进度条。
     *    percent：下载进度 0~100。
     */
    void SetDownloadProgress(int percent);

    /**
     *  退出「下载中」态：恢复为普通下载箭头（下载完成 / 失败时调用）。
     */
    void ClearDownloadProgress();

    /**
     *  进入 / 更新「上传中」态（群文件上传专用，非库内条目）：右侧显示 ✕ 取消按钮，
     *  说明行显示「上传中 NN%」，行底叠一条进度条。点 ✕ 向宿主投 WM_GROUP_FILE_UPLOAD_CANCEL。
     *    uploadId：本笔上传的客户端上传ID（点 ✕ 时随消息回投，宿主据此取消）。
     *    percent： 上传进度 0~100。
     */
    void SetUploading(int uploadId, int percent);

    // 本行对应的上传ID（仅「上传中」态有意义）。
    int UploadId() const { return m_uploadId; }

    /**
     *  设置本行是否可删除（由宿主按权限算好：群主 / 管理员 / 上传者本人）。可删时，
     *  鼠标悬停本行会在下载箭头左侧显示一个垃圾桶图标，点它向宿主投 WM_GROUP_FILE_DELETE。
     *  不可删时永不显示垃圾桶。仅普通库内文件行（非上传中 / 下载中）才显示。
     *    canDelete：是否允许删除本行文件。
     */
    void SetDeletable(bool canDelete);

    // DuiControl 覆写：绘制角标 / 文件名 / 说明 / 下载箭头（+ 行悬停底）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：鼠标移动时更新"下载箭头是否被悬停"，变化则重绘。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标离开本行时清掉下载箭头悬停态。
    bool OnMouseLeave() override;

    // DuiControl 覆写：光标在下载箭头上时显示手型，其余用普通箭头。
    bool OnSetCursor(POINT pt) override;

    // DuiControl 覆写：点击命中下载箭头时触发下载动作。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

private:
    // 计算下载箭头的命中矩形（host 客户区坐标）。绘制与命中共用，保证一致。
    RECT DownloadHitRect() const;

    // 计算垃圾桶图标的命中矩形（下载箭头左侧的一个等宽区）。绘制与命中共用。
    RECT TrashHitRect() const;

    // 垃圾桶当前是否“生效”（可删 + 普通态，即非上传中 / 下载中）。决定是否预留空间 / 命中 / 绘制。
    bool TrashActive() const { return m_canDelete && !m_downloading && !m_uploading; }

private:
    CString  m_name;        // 文件名（含扩展名）
    CString  m_metaLine;    // 「上传者·时间·大小」整行说明
    CString  m_typeLabel;   // 角标内文字（扩展名大写，最多 4 字符，如 "XLSX"）
    COLORREF m_typeColor;   // 角标底色（按扩展名分类）
    CString  m_entryid;     // 群文件条目ID（删除用，本增量暂存未用）
    CString  m_fileid;      // 文件本体ID（下载用，本增量暂存未用）
    bool     m_downloadHover; // 右侧动作区当前是否被悬停（下载箭头 / 上传 ✕ 共用，决定高亮与手型）
    bool     m_downloading;   // 是否正在下载（true 时右侧显百分比 + 行底进度条）
    int      m_downloadPercent; // 下载进度百分比 0~100（仅 m_downloading 时有意义）
    bool     m_uploading;     // 是否正在上传（true 时右侧显 ✕ 取消 + 说明「上传中 NN%」+ 行底进度条）
    int      m_uploadId;      // 本行对应的上传ID（仅 m_uploading 时有意义；点 ✕ 回投给宿主）
    int      m_uploadPercent; // 上传进度百分比 0~100（仅 m_uploading 时有意义）
    bool     m_canDelete;     // 本行是否可删除（宿主按权限设；决定悬停时是否显示垃圾桶图标）
    bool     m_deleteHover;   // 垃圾桶图标当前是否被悬停（决定其高亮与手型）
};

} // namespace balloonchatwnd
