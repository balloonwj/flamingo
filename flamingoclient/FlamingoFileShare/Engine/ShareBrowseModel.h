/**
 *  浏览窗口用到的纯逻辑：共享内路径的拼接与回退、条目排序、大小与时间的显示文字、状态与错误的提示文字，
 *  ShareBrowseModel.h
 *
 *  访问者在浏览窗口里看到的一切文字都由这里生成，窗口本身只负责摆放与绘制（docs/文件共享方案.md 7.4）：
 *    - 路径：相对共享根、用 '/' 分隔、共享根为空串（与控制通道的约定一致，方案 5.3）；
 *    - 排序：目录始终排在文件前面，同类再按选中的列（名称、大小、修改时间）升序或降序；名称不区分大小写，
 *      相同时按区分大小写的结果定先后，保证结果稳定；
 *    - 显示：字节数换算成 B / KB / MB / GB，时间按本机时区显示到分钟；
 *    - 提示：共享暂不可用、已失效、打开失败时横幅上的文字；
 *    - 写操作（第 2 期第 2 步）：右键菜单按权限与共享类型显示哪些项、新名字是否合法、操作结果在状态栏上的文字；
 *    - 多选（第 2 期第 5 步）：多选时右键菜单去掉只对单个条目有意义的项、删除确认的文字、把条目按所在目录分组
 *      （下载与上传的一次传输只能包含同一目录下的条目，搜索结果与拖入的文件可能来自不同目录）。
 *
 *  本模块不碰窗口、不打日志，可在任意线程调用。单元测试见 FlamingoFileShare/Tests/ShareBrowseModelTests.cpp
 *  （标签 [sharebrowse]）。
 *
 *  典型用法：
 *      std::string child = ShareBrowseModel::JoinPath(current, entry.m_name);
 *      ShareBrowseModel::SortEntries(entries, share_sort_size, false);
 *      std::wstring sizeText = ShareBrowseModel::FormatSize(entry.m_size, entry.m_isDir);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "ShareControlFraming.h"   //ShareEntry

namespace fileshare {

//浏览窗口可以按哪一列排序
enum share_sort_column
{
    share_sort_name  = 1,   //名称（默认）：不区分大小写的序数比较
    share_sort_size  = 2,   //大小：目录之间按名称比较
    share_sort_mtime = 3,   //修改时间：相同时按名称比较
};

//浏览窗口右键菜单的各项，ShareBrowseModel::ContextMenuItems 返回它们的组合
enum share_browse_menu_item
{
    share_menu_open       = 1,    //打开：右键点在目录上时进入该目录
    share_menu_download   = 2,    //下载：右键点在某一行上时
    share_menu_rename     = 4,    //重命名：点在某一行上且有重命名权限
    share_menu_delete     = 8,    //删除：点在某一行上且有删除权限
    share_menu_move       = 16,   //移动到…：点在某一行上且有移动权限
    share_menu_new_folder = 32,   //新建文件夹：有新建权限（点在行上或空白处都显示）
    share_menu_refresh    = 64,   //刷新：总是显示
    share_menu_preview    = 128,  //预览：右键点在图片上时（第 2 期第 4 步）
};

//新名字的最大字符数（UTF-16 字符），与 NTFS 单个名字的上限一致
const size_t kShareNameMaxChars = 255;

//缩略图视图的网格尺寸（像素）：每格宽高、格与格之间的间距、网格左右的留白
struct ShareGridMetrics
{
    int m_tileWidth;    //一格的宽
    int m_tileHeight;   //一格的高（图片区加名字）
    int m_gap;          //格与格之间（横向与纵向）的间距
    int m_padding;      //网格左右两侧的留白

    ShareGridMetrics()
        : m_tileWidth(0)
        , m_tileHeight(0)
        , m_gap(0)
        , m_padding(0)
    {
    }
};

/**
 *  ShareBrowseModel —— 浏览窗口的纯逻辑；全部为静态方法，不可实例化。
 */
class ShareBrowseModel
{
public:
    /**
     *  在目录下拼出一个条目的相对路径。
     *  @param dir  目录的相对路径；共享根为空串
     *  @param name 条目名
     *  @return dir 为空时返回 name，否则返回 dir + "/" + name
     */
    static std::string JoinPath(const std::string& dir, const std::string& name);

    /**
     *  取上一级目录。
     *  @param path 相对路径
     *  @return 去掉最后一段后的路径；只有一段时返回空串（共享根）；本身为空串时仍返回空串
     */
    static std::string ParentPath(const std::string& path);

    /**
     *  把相对路径拆成各段。
     *  @param path 相对路径
     *  @return 各段，共享根返回空数组
     */
    static std::vector<std::string> SplitPath(const std::string& path);

    /**
     *  按选中的列排序：目录始终排在文件前面。
     *  @param entries   [入出参] 条目
     *  @param column    排序列，取值见 share_sort_column；不认识的值按名称处理
     *  @param ascending 是否升序
     */
    static void SortEntries(std::vector<ShareEntry>& entries, int column, bool ascending);

    /**
     *  大小的显示文字：小于 1 KB 显示字节数，其余保留一位小数，单位依次为 KB、MB、GB、TB。
     *  @param bytes 字节数
     *  @param isDir 是否为目录；目录返回空串
     *  @return 显示文字，如 "512 B"、"1.5 KB"、"12.0 MB"
     */
    static std::wstring FormatSize(int64_t bytes, bool isDir);

    /**
     *  按年月日时分拼出时间的显示文字（纯格式化，不做时区换算）。
     *  @return 形如 "2026-10-02 23:06"
     */
    static std::wstring FormatTimeParts(int year, int month, int day, int hour, int minute);

    /**
     *  修改时间的显示文字：按本机时区换算后调用 FormatTimeParts。
     *  @param unixMs Unix 毫秒；0 或换算失败时返回空串
     *  @return 显示文字
     */
    static std::wstring FormatTime(int64_t unixMs);

    /**
     *  打开或浏览失败时的提示文字。
     *  @param code 错误码（800 段为文件共享专用，其余按通用错误处理）
     *  @return 提示文字
     */
    static std::wstring ErrorText(int code);

    /**
     *  共享对本人的状态（2203、2205 应答、卡片里的 sharestate / sharereason）对应的横幅文字。
     *  @param state  share_state
     *  @param reason 暂不可用时为 share_unavailable_reason，已失效时为 share_invalid_reason
     *  @return 可用时返回空串（不显示横幅），其余返回提示文字
     */
    static std::wstring StateText(int state, int reason);

    /**
     *  右键菜单显示哪些项。单文件共享不支持写操作，只有下载与刷新。
     *  @param perms    本人的写权限位，share_perm 的组合
     *  @param kind     共享类型，share_kind
     *  @param onRow    是否点在某一行上（否则点在列表空白处）
     *  @param rowIsDir 点中的那一行是不是目录（onRow 为假时忽略）
     *  @param rowIsImage 点中的那一行是不是图片文件（onRow 为假时忽略）
     *  @return share_browse_menu_item 的组合
     */
    static int ContextMenuItems(int perms, int kind, bool onRow, bool rowIsDir, bool rowIsImage);

    /**
     *  检查访问者输入的新名字（新建文件夹、重命名）。与分享者一端的检查规则相同，先在本地拦下明显不合法的名字。
     *  @param name  输入的名字
     *  @param error [出参] 不合法时的提示文字；合法时清空
     *  @return 合法返回 true
     */
    static bool CheckNewName(const std::wstring& name, std::wstring& error);

    /**
     *  写操作的结果在状态栏上的文字。
     *  @param opType 帧类型：sf_mkdir、sf_rename、sf_delete、sf_move 之一
     *  @param code   总结果：0 成功，否则为错误码
     *  @param items  逐条结果；为空时按一条计
     *  @return 例如「已删除 3 项」「移动了 1 项，2 项失败：目标位置已有同名的文件或文件夹」「重命名失败：……」
     */
    static std::wstring OpResultText(int opType, int code, const std::vector<ShareOpItem>& items);

    /**
     *  缩略图网格每行放几格：按可用宽度能放下的整格数，至少 1。
     *  @param width 可用宽度（像素，不含滚动条）
     *  @param m     网格尺寸
     *  @return 列数
     */
    static int GridColumns(int width, const ShareGridMetrics& m);

    /**
     *  缩略图网格的行数。
     *  @param count   条目数
     *  @param columns 列数（> 0）
     *  @return 行数
     */
    static int GridRows(size_t count, int columns);

    /**
     *  一行里第 column 格的矩形（行矩形由列表给出；格子在行的上方，行底留出纵向间距）。
     *  @param rowRect 行矩形
     *  @param column  列号（从 0 起）
     *  @param m       网格尺寸
     *  @return 格子矩形
     */
    static RECT GridTileRect(const RECT& rowRect, int column, const ShareGridMetrics& m);

    /**
     *  横坐标 x 落在某一行的第几个条目上。
     *  @param rowIndex 行号
     *  @param x        横坐标（与 rowLeft 同一坐标系）
     *  @param rowLeft  行矩形的左边
     *  @param columns  列数
     *  @param count    条目数
     *  @param m        网格尺寸
     *  @return 条目下标；落在格与格之间、或这一格没有条目时返回 -1
     */
    static int GridIndexAt(int rowIndex, int x, int rowLeft, int columns, size_t count, const ShareGridMetrics& m);

    /**
     *  按选中的条目数修正右键菜单：选中多于一项时去掉打开、预览、重命名（这几项只对单个条目有意义）。
     *  @param items         ContextMenuItems 的结果
     *  @param selectedCount 菜单作用的条目数
     *  @return 修正后的 share_browse_menu_item 组合
     */
    static int MenuItemsForSelection(int items, size_t selectedCount);

    /**
     *  删除前确认框的文字：一项时写出名字，多项时写第一项的名字与总数。
     *  @param names 要删除的条目名（界面显示的名字），不可为空
     *  @return 确认框正文
     */
    static std::wstring DeleteConfirmText(const std::vector<std::wstring>& names);

    /**
     *  把共享内的相对路径按所在目录分组，组的顺序与组内顺序都保持第一次出现时的先后。
     *  @param paths 相对共享根的路径（'/' 分隔）
     *  @return 每组是 paths 里的下标
     */
    static std::vector<std::vector<size_t> > GroupByParent(const std::vector<std::string>& paths);

    /**
     *  把本机路径按所在目录分组（不区分大小写），顺序规则同 GroupByParent。
     *  @param paths 本机绝对路径（'\' 分隔）
     *  @return 每组是同一目录下的路径
     */
    static std::vector<std::vector<std::wstring> > GroupLocalByParent(const std::vector<std::wstring>& paths);

private:
    ShareBrowseModel() = delete;
};

} // namespace fileshare
