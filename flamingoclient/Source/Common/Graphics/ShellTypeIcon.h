/**
 *  按类型取本机系统图标，ShellTypeIcon.h
 *
 *  文件共享的各个列表（本会话共享浮层、「文件共享」窗口、聊天里的共享卡片、浏览窗口的文件列表与缩略图视图）
 *  用本机的系统图标表示一个条目：整个磁盘用硬盘图标，文件夹用文件夹图标，文件按扩展名取本机该类型的图标。
 *  条目大多在别人的电脑上，取不到它真实的图标，所以一律按类型取，不访问磁盘（docs/文件共享方案.md 7.4）。
 *
 *  取到的是系统图像列表里的下标，按「类型 + 扩展名」缓存，同一种类型只向外壳查询一次；绘制时按目标边长从系统的
 *  小 / 大 / 特大 / 超大四档图像列表里挑不小于边长的最小一档，取出图标缩放绘制，取出的图标句柄也缓存起来。
 *  主程序与 FlamingoFileShare.dll 各编译一份（DLL 直接编译本文件，因此不包含 stdafx.h，也不得包含主程序专用的头文件），
 *  两份缓存互不相干；DLL 卸载前要调用 ReleaseCache 释放缓存的图标句柄。
 *
 *  线程：只在 UI 线程上调用（外壳的图标查询要求调用线程已初始化 COM，UI 线程在程序启动时已经初始化）。
 *
 *  典型用法：
 *      //浏览窗口的一行
 *      shellicon::DrawItemIcon(hdc, shellicon::kItemFile, L"报告.docx", iconRect);
 *      //balloonui 的图片控件要位图
 *      HBITMAP bmp = shellicon::CreateItemIconBitmap(shellicon::kItemDisk, L"", 32);
 *
 *  单元测试见 Tests/ShellTypeIconTests.cpp（标签 [shellicon]）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

#include <string>

namespace shellicon {

// 条目类型，取值与文件共享协议的 share_kind 一致（1 整个磁盘、2 文件夹、3 单个文件），共享记录的 kind 可以直接传入
enum ItemKind
{
    kItemDisk   = 1,   // 整个磁盘：本机固定硬盘的图标
    kItemFolder = 2,   // 文件夹：本机普通文件夹的图标
    kItemFile   = 3,   // 文件：按扩展名取本机该类型的图标；没有扩展名时为通用文件图标
};

// 取扩展名作为缓存键：取文件名（最后一个 \ 或 / 之后）里最后一个点及其后的部分并转成小写，如「.docx」；
// 没有点、点在末尾时返回空串。以点开头的名字（如 .gitignore）整体按扩展名处理，与资源管理器一致。
//   name：文件名或路径。
std::wstring ExtensionKey(const std::wstring& name);

// 取某类条目在系统图像列表里的图标下标，结果按「类型 + 扩展名」缓存。
//   kind：条目类型，取值见 ItemKind；不认识的取值按文件处理。
//   name：文件名或路径，只在 kind 为文件时用到其扩展名。
//   返回：图标下标；外壳查询失败时返回 -1。
int TypeIconIndex(int kind, const std::wstring& name);

// 在 rc 里画系统图像列表的第 index 个图标：按 rc 的较短边从四档图像列表里挑不小于它的最小一档（都小于时取最大一档），
// 缩放到 rc 的较短边、在 rc 里居中绘制，透明部分保留底色。
//   hdc：目标设备上下文。index：TypeIconIndex 的返回值。rc：目标矩形（hdc 的坐标）。
//   返回：画了返回 true；index 小于 0、rc 为空或取图标失败时不画、返回 false。
bool DrawTypeIcon(HDC hdc, int index, const RECT& rc);

// DrawTypeIcon(hdc, TypeIconIndex(kind, name), rc) 的简写。
bool DrawItemIcon(HDC hdc, int kind, const std::wstring& name, const RECT& rc);

// 生成一张 px × px 的 32 位预乘透明度位图，内容为该类条目的图标（供只接受位图的 balloonui 控件显示）。
//   kind / name：同 TypeIconIndex。px：边长，像素，须大于 0。
//   返回：位图句柄，所有权归调用方，用完 DeleteObject；失败返回 NULL。
HBITMAP CreateItemIconBitmap(int kind, const std::wstring& name, int px);

// 释放缓存的全部图标句柄并清空下标缓存。DLL 卸载前调用；之后再取图标会重新查询，照常可用。
void ReleaseCache();

// 下标缓存里的条目数（单元测试核对「同一类型只查询一次」用）。
size_t CachedTypeCount();

} // namespace shellicon
