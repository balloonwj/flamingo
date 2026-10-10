/**
 *  FlamingoMarkdown.dll 文档的 C++ 包装：持有一个 DLL 文档，析构时经函数表释放；DLL 不可用时所有方法返回
 *  「未渲染」，调用方据此走纯文字路径（docs/Markdown消息方案.md 第 6.1 节）。
 *
 *  另外提供纯文本提取（摘要、引用预览用）：DLL 可用时去掉 Markdown 标记，不可用时原样返回源文本。
 *
 *  函数表取自 CMarkdownModuleHost；单元测试可用 SetMarkdownApiOverrideForTest 换成别的函数表或模拟 DLL 不可用。
 *
 *  典型用法（气泡排版时）：
 *
 *      markdown::CMarkdownDocument doc;
 *      if (doc.Parse(utf8Source, imagePrefix))
 *      {
 *          SIZE size;
 *          doc.Layout(hdc, style, maxWidth, size);
 *          doc.Paint(hdc, left, top, paintState);
 *      }
 *      else
 *      {
 *          ...按纯文字排版...
 *      }
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。所有方法只在界面线程调用，
 *  纯文本提取函数可在任意线程调用。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "../../../FlamingoMarkdown/FlamingoMarkdownApi.h"

namespace markdown {

/**
 *  取当前可用的函数表：测试替换生效时返回替换值，否则返回宿主已初始化的函数表。
 *  @return 函数表；DLL 不可用（含尚未加载完）时返回 NULL
 */
const flamingomd::MdApi* ActiveMarkdownApi();

//Markdown 渲染当前是否可用（ActiveMarkdownApi() 不为 NULL）
bool IsMarkdownAvailable();

/**
 *  测试用：替换 ActiveMarkdownApi() 的返回值。
 *  @param enabled 为 true 时启用替换（api 可以为 NULL，模拟 DLL 不可用）；为 false 时恢复为宿主的函数表
 *  @param api     替换用的函数表
 */
void SetMarkdownApiOverrideForTest(bool enabled, const flamingomd::MdApi* api);

/**
 *  把 Markdown 源文本转成去掉标记的纯文本（UTF-8），块之间为 \n、单元格之间为 \t。
 *  DLL 不可用或提取失败时原样返回源文本。
 *  @param utf8Source  源文本（UTF-8）
 *  @param imagePrefix 图片语法转成链接时加在说明前面的文字（如 L"[图片] "），可为空
 *  @return 纯文本（UTF-8）
 */
std::string MarkdownToPlainTextUtf8(const std::string& utf8Source, const std::wstring& imagePrefix);

/**
 *  一个 Markdown 文档。不可复制；析构时释放 DLL 文档。
 */
class CMarkdownDocument
{
public:
    CMarkdownDocument();

    //释放 DLL 文档
    ~CMarkdownDocument();

    /**
     *  解析源文本（会先释放已有的文档）。
     *  @param utf8Source  源文本（UTF-8）
     *  @param imagePrefix 图片语法转成链接时加在说明前面的文字，可为空
     *  @return 解析成功返回 true；DLL 不可用或解析失败返回 false（调用方按纯文字显示）
     */
    bool Parse(const std::string& utf8Source, const std::wstring& imagePrefix);

    //释放文档，回到未解析状态
    void Reset();

    //是否持有已解析的文档
    bool IsParsed() const;

    /**
     *  按最大宽度排版；样式与宽度都不变时 DLL 直接返回缓存结果。
     *  @param hdc      测量用的设备上下文
     *  @param style    排版样式（m_cbSize 由调用方填好）
     *  @param maxWidth 最大宽度（像素），大于 0
     *  @param outSize  [出参] 文档尺寸
     *  @return 成功返回 true
     */
    bool Layout(HDC hdc, const flamingomd::MdStyle& style, int maxWidth, SIZE& outSize);

    /**
     *  绘制（须已排版）。
     *  @param hdc   目标设备上下文
     *  @param x     文档左上角横坐标
     *  @param y     文档左上角纵坐标
     *  @param state 绘制状态（m_cbSize 由调用方填好），可为 NULL
     *  @return 成功返回 true
     */
    bool Paint(HDC hdc, int x, int y, const flamingomd::MdPaintState* state);

    /**
     *  命中测试（须已排版）。
     *  @param x   相对文档原点的横坐标
     *  @param y   相对文档原点的纵坐标
     *  @param out [出参] 结果（本函数会填好 m_cbSize）
     *  @return 成功返回 true
     */
    bool HitTest(int x, int y, flamingomd::MdHitResult& out) const;

    /**
     *  取渲染后文字的一段（换行为 \r\n）。
     *  @param begin 起点下标，与 end 顺序不限
     *  @param end   终点下标
     *  @return 文字；失败时为空串
     */
    std::wstring GetText(int begin, int end) const;

    //渲染后文字的长度；未解析时为 0
    int GetTextLength() const;

    /**
     *  取第 index 个链接的地址。
     *  @return 地址；序号越界或失败时为空串
     */
    std::wstring GetLinkUrl(int index) const;

    /**
     *  取第 index 个代码块的原文（换行为 \r\n）。
     *  @return 原文；序号越界或失败时为空串
     */
    std::wstring GetCodeBlockText(int index) const;

    // ---- 以下三个方法用到 DLL 第二版接口（docs/Markdown消息方案.md 第 11 节）：DLL 是旧版时返回 false ----

    /**
     *  取消息卡片的标题与摘要（标题优先取第一个标题块，摘要是其余文字压成的一段）。
     *  @param title   [出参] 标题
     *  @param summary [出参] 摘要
     *  @return 成功返回 true；未解析或 DLL 是旧版时返回 false
     */
    bool GetCardText(std::wstring& title, std::wstring& summary) const;

    /**
     *  导出为单个 HTML 文件的内容（UTF-8、不带 BOM、样式内嵌）。不需要先排版。
     *  @param title 写进 <title> 的文档标题，可为空
     *  @param html  [出参] HTML 文件内容
     *  @return 成功返回 true；未解析或 DLL 是旧版时返回 false
     */
    bool ExportHtml(const std::wstring& title, std::string& html) const;

    /**
     *  把渲染后文字里 [begin, end) 这一段导出为 HTML（DLL 第四版接口），复制到剪贴板用：完整的 HTML 文档，正文只含
     *  选中的部分，前后带 <!--StartFragment--> 与 <!--EndFragment--> 注释。不需要先排版。
     *  @param begin 选区起点（渲染后文字的下标，与 GetText 相同），与 end 顺序不限，越界截断
     *  @param end   选区终点（不含）
     *  @param html  [出参] HTML（UTF-8）；选区为空时为空串
     *  @return 成功返回 true（选区为空也算成功）；未解析或 DLL 没有第四版接口时返回 false
     */
    bool ExportHtmlRange(int begin, int end, std::string& html) const;

    /**
     *  按页切分已排版的文档（导出 PDF 用），只在行与行之间切开。
     *  @param pageHeight 一页可用的高度（与排版同一单位）
     *  @param starts     [出参] 各页的起始纵坐标，第一页为 0
     *  @return 成功返回 true；未排版或 DLL 是旧版时返回 false
     */
    bool Paginate(int pageHeight, std::vector<int>& starts) const;

    /**
     *  导出为 Word 文档（.docx）的全部字节（DLL 第三版接口）。不需要先排版。
     *  @param bytes [出参] .docx 文件内容
     *  @return 成功返回 true；未解析或 DLL 没有第三版接口时返回 false
     */
    bool ExportDocx(std::string& bytes) const;

    /**
     *  当前生效的函数表是否带 Word 导出（第三版接口）。导出菜单据此决定是否列出「Word 文档」。
     */
    static bool CanExportDocx();

private:
    CMarkdownDocument(const CMarkdownDocument&);
    CMarkdownDocument& operator=(const CMarkdownDocument&);

    const flamingomd::MdApi* m_api;       //解析时取得的函数表（DLL 不卸载，一直有效）；未解析时为 NULL
    flamingomd::MdDocument*  m_doc;       //DLL 文档，归本对象所有；未解析时为 NULL
    bool                     m_laidOut;   //是否已成功排版过（未排版时绘制与命中直接返回 false）
};

} // namespace markdown
