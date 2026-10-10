/**
 *  浏览窗口多选与拖出用到的纯逻辑：选择集（ShareSelection）与拖出条目表（ShareDragModel），ShareDragModel.h
 *
 *  第 2 期第 5 步（docs/文件共享方案.md 7.4）：
 *    - 选择集：单击只选这一项、Ctrl + 单击加选或减选、Shift + 单击从上次单击的那一项连选到这一项（Ctrl + Shift 在原有
 *      选择上追加这一段）；下标是列表里的条目下标；
 *    - 拖出条目表：把选中的条目（目录带上它的整棵目录树）展开成资源管理器要的虚拟文件描述（FILEGROUPDESCRIPTORW 的每一项）。
 *      每一项的名字相对于「拖出的位置」：选中的 docs/a.txt 落地为 a.txt，选中的目录 docs 之下的 docs/sub/b.txt 落地为
 *      docs\sub\b.txt。父目录排在子条目之前（资源管理器按顺序建）。总条目数超过 kShareTreeMaxItems 时拒绝，提示改用下载；
 *      顶层条目同名（不区分大小写）时同样拒绝。
 *
 *  本模块不碰窗口与网络，可在任意线程调用。单元测试见 FlamingoFileShare/Tests/ShareDragModelTests.cpp（标签 [sharedrag]）。
 *
 *  典型用法：
 *      ShareSelection sel;
 *      sel.Click(index, ctrlDown, shiftDown);
 *      std::vector<ShareDragItem> items;
 *      if (ShareDragModel::Build(sources, trees, items) == share_drag_build_ok) ...
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ShareControlFraming.h"   //ShareEntry

namespace fileshare {

/**
 *  ShareSelection —— 列表里的选择集。
 */
class ShareSelection
{
public:
    ShareSelection();

    /**
     *  单击一个条目。
     *  @param index 条目下标（>= 0）
     *  @param ctrl  是否按着 Ctrl：加选或减选这一项
     *  @param shift 是否按着 Shift：从上次单击的那一项连选到这一项（同时按 Ctrl 时追加，否则替换原有选择）
     */
    void Click(int index, bool ctrl, bool shift);

    //只选一个条目（右键点在未选中的条目上、程序选中时）；index 为 -1 时清空
    void SelectOnly(int index);

    //清空
    void Clear();

    //某个条目是否选中
    bool Contains(int index) const;

    //选中的条目个数
    size_t Count() const;

    //选中的条目，按下标从小到大
    std::vector<int> Sorted() const;

    //「当前项」：最近一次单击（或 SelectOnly）的条目，单项操作（重命名）作用于它；没有时为 -1
    int Primary() const;

private:
    std::set<int> m_items;    //选中的条目下标
    int           m_anchor;   //Shift 连选的起点：最近一次不带 Shift 的单击；没有时为 -1
    int           m_primary;  //当前项；没有时为 -1
};

//拖出条目表的构造结果，ShareDragModel::Build 的返回值
enum share_drag_build
{
    share_drag_build_ok           = 0,   //成功
    share_drag_build_too_many     = 1,   //总条目数超过 kShareTreeMaxItems，应提示改用下载
    share_drag_build_missing_tree = 2,   //有选中的目录还没有取到整棵目录树（或目录树被截断）
    share_drag_build_empty        = 3,   //没有选中任何条目
    share_drag_build_duplicate    = 4,   //顶层条目里有同名的（搜索结果里来自不同目录的同名条目），落地时会互相覆盖
};

//一个被拖出的顶层条目：它在共享里的路径与列表里的条目信息
struct ShareDragSource
{
    std::string m_sharePath;   //相对共享根的路径
    ShareEntry  m_entry;       //条目信息（是否目录、大小、修改时间）

    ShareDragSource()
    {
    }
};

//拖出条目表的一项（对应 FILEGROUPDESCRIPTORW 的一项）
struct ShareDragItem
{
    std::string  m_sharePath;   //相对共享根的路径（'/' 分隔）
    std::wstring m_dropName;    //落地的相对名字（'\' 分隔，相对于拖出的位置）
    bool         m_isDir;       //是否目录
    int64_t      m_size;        //字节数；目录为 0
    int64_t      m_mtime;       //修改时间（Unix 毫秒）

    ShareDragItem()
        : m_isDir(false)
        , m_size(0)
        , m_mtime(0)
    {
    }
};

/**
 *  ShareDragModel —— 拖出条目表的构造；全部为静态方法，不可实例化。
 */
class ShareDragModel
{
public:
    /**
     *  构造拖出条目表。
     *  @param sources 选中的顶层条目
     *  @param trees   选中的目录 → 它的整棵目录树（条目的 m_name 为相对共享根的路径，父目录在子条目之前）
     *  @param out     [出参] 条目表；失败时为空
     *  @return share_drag_build
     */
    static int Build(const std::vector<ShareDragSource>& sources, const std::map<std::string, std::vector<ShareEntry> >& trees,
                     std::vector<ShareDragItem>& out);

    /**
     *  一个条目落地的相对名字：去掉 baseDir 前缀（拖出位置所在的目录）、把 '/' 换成 '\'。
     *  @param sharePath 条目相对共享根的路径
     *  @param baseDir   拖出位置所在的目录（相对共享根，空串为共享根）
     *  @return 落地名字；sharePath 不在 baseDir 之下时返回它的最后一段
     */
    static std::wstring DropNameFor(const std::string& sharePath, const std::string& baseDir);

    /**
     *  拒绝拖出时状态栏显示的原因。
     *  @param result Build 的返回值（share_drag_build_ok 以外的值）
     *  @return 界面文字；share_drag_build_ok 返回空串
     */
    static std::wstring RefusalText(int result);

private:
    ShareDragModel() = delete;
};

} // namespace fileshare
