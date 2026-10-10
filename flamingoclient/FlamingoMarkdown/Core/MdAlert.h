/**
 *  警告框（GitHub 写法的 > [!NOTE] 等，docs/Markdown消息方案.md 第 13 节）的类型、颜色与标题。解析由 md4c 的
 *  MD_FLAG_ADMONITIONS 完成（引用块第一行正好是「[!类型]」时整块成为警告框，这一行不进正文），模型里警告框是
 *  m_alertType 不为 0 的引用块；排版、绘制、HTML 与 Word 导出都从这里取颜色与标题。
 *
 *  标题属于界面文字，DLL 不自带：宿主经函数表的 m_setAlertTitles（第五版）在初始化后传入一次各类型的译文；
 *  没有传入时显示类型名本身（NOTE、TIP、IMPORTANT、WARNING、CAUTION，即源文本里写的关键字）。
 *
 *  典型用法：
 *      const int type = MdAlertTypeFromKeyword(L"warning");   // md_alert_warning
 *      const std::wstring title = MdAlertTitle(type);
 *      const uint32_t bar = MdAlertColor(type);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

namespace flamingomd {

//警告框的类型（取值也是 m_setAlertTitles 传入的标题数组下标加一）
enum MdAlertType
{
    md_alert_none      = 0,   //不是警告框（普通引用块）
    md_alert_note      = 1,   //[!NOTE]：提示，蓝色
    md_alert_tip       = 2,   //[!TIP]：技巧，绿色
    md_alert_important = 3,   //[!IMPORTANT]：重要，紫色
    md_alert_warning   = 4,   //[!WARNING]：警告，琥珀色
    md_alert_caution   = 5,   //[!CAUTION]：注意（危险操作），红色
};

//警告框类型的个数
const int kMdAlertTypeCount = 5;

/**
 *  由 md4c 给出的类型名（"note"、"tip"……，大小写不限）取类型。
 *  @param keyword 类型名
 *  @return 取值见 enum MdAlertType；认不出时为 md_alert_none
 */
int MdAlertTypeFromKeyword(const std::wstring& keyword);

/**
 *  类型名的大写形式（NOTE、TIP……），宿主没有传入标题时用它作标题，HTML 导出用它的小写形式作 class。
 *  @param type 类型，取值见 enum MdAlertType；越界时返回空串
 */
std::wstring MdAlertKeyword(int type);

/**
 *  设置各类型的标题（宿主的译文）。传 NULL 或 count 为 0 时清除，恢复为类型名。
 *  @param titles 标题数组，按 md_alert_note ~ md_alert_caution 的顺序；只读取前 count 个，其中的 NULL 视为未设置
 *  @param count  数组元素个数，超过类型个数的部分忽略
 *  副作用：修改 DLL 范围的标题表（宿主应在初始化后、排版之前调用一次）
 */
void SetMdAlertTitles(const wchar_t* const* titles, uint32_t count);

/**
 *  类型的标题：宿主设置过时为宿主给的译文，否则为类型名。
 *  @param type 类型，取值见 enum MdAlertType；越界时返回空串
 */
std::wstring MdAlertTitle(int type);

/**
 *  类型的主色（竖条、图标与标题文字），参照 GitHub 浅色主题。
 *  @return 0x00BBGGRR（与 COLORREF 相同）；越界时返回灰色
 */
uint32_t MdAlertColor(int type);

/**
 *  类型的底色（主色在白底上的浅色）。
 *  @return 0x00BBGGRR；越界时返回浅灰
 */
uint32_t MdAlertBackColor(int type);

/**
 *  颜色写成 6 位大写十六进制「RRGGBB」（不带 #），HTML 与 Word 导出用。
 *  @param color 0x00BBGGRR
 */
std::string MdAlertColorHex(uint32_t color);

} // namespace flamingomd
