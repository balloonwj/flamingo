/**
 *  Markdown 文档的命中测试与按范围取文字。
 *
 *  命中测试的做法：先找点所在的行（点同时落在行的纵向与横向范围内的优先；否则取纵向最近、其次横向最近的行），
 *  再在行内按片段与每个字符的右边缘找出最近的字符边界。点落在片段上时报告「文字」或「链接」，落在代码块的
 *  「复制」按钮上时报告「复制按钮」，其余报告「空白」，但都给出最近的字符边界，供宿主拖选。
 *
 *  典型用法：
 *
 *      MdHitInfo hit;
 *      HitTestMdLayout(layout, x, y, hit);
 *      std::wstring text = GetMdTextRange(model, selBegin, selEnd);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "MdLayout.h"
#include "MdModel.h"

namespace flamingomd {

/**
 *  命中测试的结果（DLL 内部使用，由导出函数写进 MdHitResult）。
 */
struct MdHitInfo
{
    int m_kind;             //命中种类，取值见 enum MdHitKind
    int m_charPos;          //最近的字符边界（渲染后文字的下标）
    int m_linkIndex;        //链接序号；不在链接上为 -1
    int m_codeBlockIndex;   //所在代码块的序号；不在代码块内为 -1

    MdHitInfo()
        : m_kind(md_hit_none), m_charPos(0), m_linkIndex(-1), m_codeBlockIndex(-1)
    {
    }
};

/**
 *  命中测试。
 *  @param layout 排版结果
 *  @param x      相对文档原点的横坐标
 *  @param y      相对文档原点的纵坐标
 *  @param out    [出参] 结果
 */
void HitTestMdLayout(const MdLayoutResult& layout, int x, int y, MdHitInfo& out);

/**
 *  取渲染后文字的一段，其中的换行转成 \r\n。
 *  @param model 文档模型
 *  @param begin 起点下标，与 end 顺序不限，超出范围的部分被截掉
 *  @param end   终点下标
 *  @return 文字
 */
std::wstring GetMdTextRange(const MdModel& model, int begin, int end);

/**
 *  把文字里的 \n 转成 \r\n（已有的 \r\n 不重复转换）。
 */
std::wstring ToCrLf(const std::wstring& text);

} // namespace flamingomd
