/**
 *  SettingsPageModel.h
 *
 *  "系统设置"对话框左侧导航的<u>纯数据模型</u>。把"导航有哪几页、什么顺序、
 *  每页标题、本期是否已落地内容"从对话框类里抽出成不依赖 HWND / 不依赖
 *  balloonui 的纯函数，便于被 FlamingoUnit 测试工程直接断言（建窗口 / 切页
 *  等带副作用的部分留在 CBalloonSystemSettingDlg）。
 *
 *  设计稿：design/系统设置1.png ~ 系统设置7.png（通用 / 消息通知 / 聊天 /
 *  音视频 / 快捷键 / 存储 / 账号安全，共 7 页）。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // 仅为 wchar_t 常量串类型一致；本头不依赖 GDI/HWND
#include <vector>

namespace balloonsettings {

/**
 *  SettingsPageId —— 7 个设置分页的稳定序号。同时也是该页在内容区里的
 *  下标（第 N 页对应内容容器的第 N 个子控件），导航选中索引与之一一对应。
 *
 *  取值必须从 0 连续递增、与 BuildSettingsNavModel() 返回顺序严格一致 ——
 *  对话框据此用"选中索引"直接定位要显示哪一页。
 */
enum SettingsPageId : int
{
    kPageGeneral     = 0,   // 通用 —— 开机自启动 / 关闭行为 / 自动离开 / 语言
    kPageNotify      = 1,   // 消息通知 —— 新消息提醒 / 桌面弹窗 / 提示音 / 勿扰等
    kPageChat        = 2,   // 聊天 —— 发送快捷键 / 自动接收 / 消息漫游 / 链接识别
    kPageAudioVideo  = 3,   // 音视频 —— 麦克风 / 扬声器 / 摄像头 / 回声消除
    kPageShortcut    = 4,   // 快捷键 —— 6 条只读键帽展示
    kPageStorage     = 5,   // 存储 —— 占用条 / 保存位置 / 清理缓存
    kPageAccount     = 6,   // 账号安全 —— 设备管理 / 改密 / 二次验证 / 注销

    kPageCount       = 7    // 分页总数（哨兵，便于循环 / 断言）
};

/**
 *  SettingsNavEntry —— 左侧导航里的一行（一页一行）。
 */
struct SettingsNavEntry
{
    int            id;            // 页序号（见 SettingsPageId），等于内容区子控件下标
    const wchar_t* title;         // 导航显示文本（宽字符常量串，进程级生命周期）
    bool           implemented;   // 本期是否已接入真实内容；false = 仅占位空页
};

/**
 *  构造导航的行模型 —— 按设计稿顺序返回 7 项。纯函数、无副作用、不创建窗口。
 *  本期（阶段 1/2）仅前三页（通用 / 消息通知 / 聊天）implemented=true，其余四页
 *  为占位（implemented=false），等后续阶段补内容。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。title 指向静态常量串，可直接持有。
 */
std::vector<SettingsNavEntry> BuildSettingsNavModel();

/**
 *  把导航选中索引夹到合法区间 [0, count)。
 *    index：原始选中索引（可能越界 / 为负）。
 *    count：分页总数；<= 0 时无合法页。
 *  返回：合法页索引；count <= 0 时返回 -1；index 越界时夹到最近端点。
 */
int ClampNavSelection(int index, int count);

/**
 *  给定当前选中页与分页总数，算出每一页的可见性（仅选中页可见，其余隐藏）。
 *  对话框据此对内容区的 N 个分页容器逐个 SetVisible，实现"同一时刻只显示一页"。
 *    selectedIndex：当前选中页索引（会先经 ClampNavSelection 夹取）。
 *    pageCount：分页总数；<= 0 返回空 vector。
 *  返回：长度为 pageCount 的布尔表，第 i 项表示第 i 页是否可见。
 */
std::vector<bool> ComputePageVisibility(int selectedIndex, int pageCount);

} // namespace balloonsettings
