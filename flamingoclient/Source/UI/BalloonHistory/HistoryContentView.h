/**
 *  「消息记录」列表行里的多行内容块，HistoryContentView.h
 *
 *  一行消息的正文由本控件渲染：文字与系统表情图文混排、按可用宽度折行、
 *  搜索命中的字符区间铺一层淡黄底色、长文默认折叠成最多 5 行。
 *
 *  典型用法（列表行构建处）：
 *      std::unique_ptr<HistoryContentView> content(new HistoryContentView());
 *      content->SetContent(fullText);                        // 含 /f["<id>"] 的正文原文
 *      content->SetHighlightRanges(FindHighlightRanges(...));// 见 HistoryView.h
 *      content->SetExpanded(model.IsExpanded(msgid));
 *      row->AddChild(std::move(content));
 *  行高由外部先问一句：
 *      const int h = content->MeasureHeight(availWidth);
 *
 *  存在理由 —— 它取代的是 HistoryHighlightLabel（DuiLabel 子类，单行定高）。那条路上
 *  长文被省略号截成一句、表情只能显示成字面量「[表情]」，信息量太低（见
 *  docs/消息记录内容交互方案.md §2.1 / §2.2）。DuiLabel 画不出「文字与图混排」，
 *  balloonui 里也没有现成能干这事的控件，故按仓库 UI 约定走「末选：自绘 DuiControl
 *  子类」这一档 —— 但排版与绘制本身<u>不自己造</u>，整套复用聊天气泡那边抽出来的
 *  balloonchatwnd::LayoutInlineContent / DrawInlineContent，两处渲染口径完全一致。
 *
 *  测量与绘制共用同一套参数（字体、表情格边长、行数上限），由本类的三个私有函数
 *  统一给出 —— 一旦两处漂移，量出来的行高与画出来的内容就会对不上，列表要么被截
 *  一截、要么下面留一条空白。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../BalloonChat/BalloonChatMsgFormat.h"   // balloonchat::ChatSegment
#include "../BalloonChatWnd/ChatInlineText.h"      // 排版 / 绘制引擎与 InlinePlaced
#include "../BalloonChatWnd/ChatLinkDetect.h"      // LinkSpan（成员需完整类型）
#include "../BalloonChatWnd/ChatTextSelection.h"   // SelLayout（成员需完整类型）
#include "../BalloonChatWnd/ChatClipboardContent.h" // ChatClipSegment（复制选区时返回）
#include "HistoryView.h"                           // HighlightRange

namespace balloonhistory {

//折叠态最多画几行。取 5 是 docs/消息记录内容交互方案.md §10-1 与用户对齐后的决定：
//再多，一页八条里只要有两三条长消息就会把整页撑得很长；再少，两三行的日常消息也会
//被折起来，反倒每条都得点一下「展开」。
constexpr int kHistoryCollapsedMaxLines = 5;

//正文默认字号（像素）。与消息记录列表原先那个预览标签同档，换到多行之后不改观感。
constexpr int kHistoryContentDefaultFontPx = 14;

/**
 *  HistoryContentView —— 支持多行折行、内联表情、关键字高亮、折叠 / 展开的内容块。
 *
 *  字符口径：对外暴露的所有字符下标（高亮区间）都按<u>扁平文本</u>算 —— 全文按
 *  UTF-16 字符计数，<u>一个内联表情占一个字符位</u>（与 ChatTextSelection.h 一致）。
 *  这与调用方拿正文原文（`/f["5"]` 占 7 个字符）去数下标<u>不是</u>一回事，区间须由
 *  调用方按同一口径算好再传进来。
 */
class HistoryContentView : public balloonwjui::DuiControl
{
public:
    HistoryContentView();

    /**
     *  设置要显示的正文。
     *    fullText：正文原文，含 `/f["<id>"]` 形式的系统表情标记；可为空串。
     *  内部一次性解析成段序列缓存下来（绘制时不再重复解析），并作废已有的高度测量
     *  缓存。副作用：触发重绘。
     */
    void SetContent(const CString& fullText);

    /**
     *  设置要显示的正文，但把它当作<u>纯文本</u>，不做发送正文格式的解析。
     *
     *  两个入口的区别在于对 '/' 的处理：SetContent 吃的是聊天消息的<u>发送正文
     *  格式</u>，其中 `/f["5"]` 是表情标记、`//` 才表示一个字面 '/'；而群公告一类
     *  的文本是服务端原样存的纯文本，里面的 `https://x` 就是字面三个字符，拿
     *  SetContent 去解会平白吃掉一个斜杠、`/f[` 开头的文本还会被当成表情。
     *
     *  纯文本入口下正文里不会有内联表情（整串就是一个文字段），链接识别照常。
     *    plainText：正文原文（不含任何标记约定）；可为空串。
     *  副作用：作废高度测量缓存并触发重绘。
     */
    void SetPlainContent(const CString& plainText);

    /**
     *  设置搜索命中要高亮的字符区间。
     *    ranges：区间列表（扁平文本字符口径，见类注释）；传空表示不高亮。
     *            越界或长度非正的区间由本类忽略，不必由调用方预先过滤。
     *  高亮不改变内容尺寸，故<u>不</u>作废高度测量缓存。副作用：触发重绘。
     */
    void SetHighlightRanges(const std::vector<HighlightRange>& ranges);

    /**
     *  设置折叠 / 展开态。
     *    expanded：true=画全；false=最多画 kHistoryCollapsedMaxLines 行，
     *              内容画不下时末行以省略号收尾。
     *  副作用：触发重绘。状态变了行高也会变，调用方须重新问一次 MeasureHeight
     *  并让父容器重新布局。
     */
    void SetExpanded(bool expanded);

    //当前是否处于展开态。
    bool IsExpanded() const { return m_expanded; }

    /**
     *  设置单行模式：只画一行，放不下时末行以省略号收尾（与折叠态的省略号同一套画法）。
     *  用于文件卡片上的文件名 —— 名字再长也只占一行，又保留搜索高亮与拖选。
     *    singleLine：true=单行；false（默认）=按折叠 / 展开态决定行数。
     *  单行模式优先于折叠 / 展开态。副作用：作废高度测量缓存并触发重绘。
     */
    void SetSingleLine(bool singleLine);

    /**
     *  设置「按控件高度截断」：开启后只画控件高度能完整容下的行，放不下时末行以省略号收尾（与折叠态的省略号
     *  同一套画法）；内容放得下时与展开态相同。用于高度固定的区域，如右下角「新公告」弹窗的正文 —— 展开态不限
     *  行数，正文一长就画到控件下面的分隔线与按钮上。
     *  单行模式优先于本设置，本设置优先于折叠 / 展开态。只影响绘制，不影响 MeasureHeight（仍按折叠 / 展开态量）。
     *    fit：true=按控件高度截断；false（默认）=按折叠 / 展开态决定行数。
     *  副作用：触发重绘。
     */
    void SetFitToHeight(bool fit);

    /**
     *  取当前正文的「扁平文本」：各文字段原样拼接，每个内联表情占一个 U+FFFC
     *  占位字符（口径见类注释）。纯文本入口喂进来的内容原样返回。
     *
     *  供需要回读正文的调用方与单元测试使用 —— 本控件取代 DuiLabel 之后，那些
     *  原先靠 DuiLabel::GetText 断言正文的用例需要一个等价的读法。
     *  @return 扁平文本；正文为空时返回空串。
     */
    CString GetFlatText() const;

    /**
     *  设置正文颜色。默认深灰 RGB(0x33, 0x33, 0x33)，与聊天气泡正文同色。
     *    color：文字颜色。
     *  副作用：触发重绘。
     */
    void SetTextColor(COLORREF color);

    /**
     *  设置正文字号。
     *    px：字号（像素，取值须为正；<= 0 一律忽略，保持原值）。
     *        默认 kHistoryContentDefaultFontPx。
     *  字号变了折行结果与行高都会变，故会作废高度测量缓存。副作用：触发重绘。
     */
    void SetFontPixelSize(int px);

    /**
     *  按<u>当前折叠 / 展开态</u>算这块内容需要多高。列表行高就是靠它定的。
     *
     *  与 OnPaint 共用同一套排版参数（同一支字体、同一个表情格边长、同一个行数
     *  上限），量出来的和画出来的必然一致。
     *
     *  <u>本函数自建一个内存 DC 来量</u>（CreateCompatibleDC(NULL) + 选入自建字体），
     *  不要求调用方给 HDC —— 行高是在布局阶段问的，那时控件既未必已经进树、也拿不到
     *  宿主窗的 DC；而字体度量与"画在哪个 DC 上"无关，内存 DC 量出来的与屏幕一致。
     *
     *    availWidth：可用宽度（像素，不含内外边距）；<= 0 时按 1 处理。
     *  @return 内容所需高度（像素，<u>不含</u>任何内边距，恒 > 0：空正文也占一行）。
     *          宽度与折叠态都没变时直接返回上次的结果，不重新排版。
     */
    int MeasureHeight(int availWidth) const;

    /**
     *  内容是否多到了折叠态放不下 —— 即旁边该不该画那枚「展开 / 收起」链接。
     *
     *  判据恒按<u>折叠态</u>算，<u>与当前是折叠还是展开无关</u>：展开之后它仍然返回
     *  true，否则「收起」链接会在展开的一瞬间消失，用户就再也收不回去了。
     *
     *    availWidth：可用宽度（像素）；<= 0 时按 1 处理。
     *  @return 内容超过 kHistoryCollapsedMaxLines 行返回 true。
     */
    bool NeedsExpandLink(int availWidth) const;

    /**
     *  DuiControl 覆写：先铺高亮底色，再画文字与表情；折叠态内容没画完时，
     *  在末行末尾补一个省略号；最后把识别出的超链接用链接色 + 下划线重画一遍。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    /**
     *  DuiControl 覆写：光标落在超链接上时显示手型（与聊天气泡一致）。
     */
    bool OnSetCursor(POINT pt) override;

    /**
     *  DuiControl 覆写：左键抬起。结束拖选；若这一趟没有选出文字、且按下与抬起落在
     *  同一条超链接上，就当成点击，请宿主窗打开它（投 WM_CHAT_OPEN_LINK，URL 用堆上的
     *  std::wstring* 传递，宿主处理后 delete）。本控件不直接 ShellExecute —— 协议
     *  白名单校验只该有一处，放在宿主窗里。
     */
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // ---- 文字选区：鼠标拖选、双击选中整条、复制 ----
    //
    // 选区下标按扁平文本口径（见类注释），与高亮区间同一套。同一时刻整棵控件树里
    // 只有一个本类实例持有选区：在某一条上开始拖选时，其它条的选区会被清掉。

    /**
     *  DuiControl 覆写：左键按下即开始拖选。记下按下处的字符下标作为选区起点，并
     *  抓取鼠标（拖出本控件乃至拖出窗口都要继续收到移动事件）；同时清掉树里其它
     *  实例的选区。还没绘制过（没有排版模型）时不处理。
     *  @return 开始了拖选返回 true（消费这次按下）
     */
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    /**
     *  DuiControl 覆写：拖选过程中把当前点映射成字符下标作为选区另一端并重绘。
     *  折叠态下终点不会越过可见部分的末尾。
     *  @return 正在拖选时返回 true；否则 false，交给框架继续分发
     */
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    /**
     *  DuiControl 覆写：双击选中整条消息（折叠态下也包括没显示出来的部分），与聊天
     *  气泡的双击行为一致。
     *  @return 恒为 true（消费这次双击）
     */
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

    /**
     *  当前是否有非空选区。
     *  @return 有选中的文字（或表情）时返回 true
     */
    bool HasTextSelection() const;

    //清除选区（没有选区时什么都不做）。副作用：有选区时触发重绘。
    void ClearTextSelection();

    //选中整条消息，等同于双击。副作用：触发重绘。
    void SelectAllText();

    /**
     *  给定点是否落在当前选区的高亮矩形里。右键菜单据此决定"复制"复制选中部分还是
     *  整条：点在选区上复制选中部分，点在别处复制整条。
     *    pt：宿主客户区坐标。
     *  @return 有选区、已绘制过、且点落在某块高亮矩形里时返回 true
     */
    bool PointInTextSelection(POINT pt) const;

    /**
     *  取当前选区的内容，拆成剪贴板片段（文字段 + 表情段），交给
     *  balloonchatwnd::WriteChatContentToClipboard 写剪贴板：表情在聊天输入框里能还原，
     *  纯文字形式里写成 "[表情]"。
     *  @return 片段列表；没有选区时返回空
     */
    std::vector<balloonchatwnd::ChatClipSegment> GetSelectedClipSegments() const;

    /**
     *  清除 root 这棵控件树里除 keep 以外所有本类实例的选区。控件自己开始新的拖选时
     *  调用；宿主窗也可以用它一次性清掉整页的选区。
     *    root：要遍历的树根（引用，不转移所有权）；为空时什么都不做。
     *    keep：保留选区的那个实例；传 NULL 表示全部清除。
     */
    static void ClearTextSelectionsInTree(balloonwjui::DuiControl* root,
                                          const HistoryContentView* keep);

    /**
     *  测试接口：最近一次绘制得到的字符级排版模型（相对本控件左上角），用来算出
     *  某个字符的屏幕位置、模拟鼠标拖选。
     *  @return 已绘制过时返回模型指针（归本控件所有，下次绘制或换内容后失效）；
     *          还没绘制过返回 NULL
     */
    const balloonchatwnd::SelLayout* Test_GetSelLayout() const;

private:
    //建一支绘制 / 测量共用的字体（微软雅黑，字号取 m_fontPx）。与
    //DrawInlineContent 内部建的那支参数一致，两处量出来的字宽才对得上。
    //@return 字体句柄，<u>调用方负责 DeleteObject</u>；建失败返回 NULL。
    HFONT CreateContentFont() const;

    //当前该给排版引擎传的行数上限：折叠态为 kHistoryCollapsedMaxLines，
    //展开态为 0（排版引擎里 0 表示不限行）。
    int EffectiveMaxLines() const;

    //按控件高度截断时的行数上限：placed 是不限行数排出的放置项（坐标相对控件左上角），availHeight 是控件高度（像素）。
    //返回下沿不超过 availHeight 的行数；全部放得下返回 0（不限行）；一行都放不下返回 1。
    static int CountLinesFittingHeight(const std::vector<balloonchatwnd::InlinePlaced>& placed, int availHeight);

    //当前该给排版引擎传的表情格边长：正文里确实有表情时为 kFaceDisplayPx（28），
    //没有表情时为 0。
    //
    //为什么不恒传 28：排版引擎的行高取 max(字体行高, faceSize)，恒传 28 会让纯文字
    //的行也占 28px（14px 字体的行高才 19px 上下），一屏结果凭空高出一截。聊天气泡
    //那边是靠 MessageTextHasFace 分走原生 DrawText 快路径来回避这一点的，本类只有
    //一条渲染路径，于是改用"没表情就把格子边长置 0"达成同样的行高。
    int EffectiveFaceSize() const;

    //按 availWidth 与当前折叠态把高度和"要不要展开链接"一起算出来，填进缓存。
    //缓存命中（宽度与折叠态都没变）时直接返回，不重新排版。
    void EnsureMeasured(int availWidth) const;

    //作废高度测量缓存（内容 / 字号变了必须调，否则行高还是按旧内容算的）。
    void InvalidateMeasureCache();

    //把各高亮区间画成底色矩形。按本次绘制建好的 m_charLayout 取矩形，rc 为本控件矩形。
    void PaintHighlights(HDC hdc, const RECT& rc);

    //把当前选区画成底色矩形（与聊天气泡选区同色）。须在文字之前画，rc 为本控件矩形。
    void PaintSelection(HDC hdc, const RECT& rc);

    //把字符区间 [start, end) 的矩形用给定画刷铺底，超出本控件的部分裁掉。高亮与选区
    //两处共用。
    void FillCharRanges(HDC hdc, const RECT& rc, int start, int end, HBRUSH brush);

    //把宿主客户区坐标映射成拖选端点的字符下标，不越过可见部分的末尾。
    //须在已绘制过（m_charLayoutValid 为 true）时调用。
    int CharIndexAtPoint(POINT pt) const;

    //沿父链找到控件树的根，给 ClearTextSelectionsInTree 用。
    balloonwjui::DuiControl* TreeRoot();

    //请宿主窗打开第 index 条超链接（投 WM_CHAT_OPEN_LINK）。
    void OpenLinkByIndex(int index);

    //丢弃选区与拖选状态，不触发重绘。换内容时调用：旧下标对新内容没有意义。
    void ResetSelectionState();

    //折叠态内容没画完时，在末行末尾画一个省略号（与正文同字体同颜色）。
    void PaintCollapsedEllipsis(HDC hdc, const RECT& rc,
                                const std::vector<balloonchatwnd::InlinePlaced>& placed);

    //把识别出的超链接用链接色 + 下划线重画一遍（按 m_charLayout 定位）。rc 为本控件矩形。
    void PaintLinks(HDC hdc, const RECT& rc);

    //落点命中了第几条超链接；没命中（或还没画过、没有链接）返回 -1。
    //  pt：落点（宿主客户区坐标）。
    int LinkAtPoint(POINT pt) const;

    //按当前 m_segs 重算链接片段并作废排版模型。两个内容入口（正文格式 / 纯文本）
    //共用，免得两处各写一遍、日后漏改一处。
    void RebuildLinks();

private:
    //正文解析出的段序列（文字段 + 表情段），SetContent 时一次性解析好。
    //绘制与测量都直接吃它，不再重复解析正文。
    std::vector<balloonchat::ChatSegment> m_segs;

    //m_segs 里是否至少有一个表情段。决定 EffectiveFaceSize 给 0 还是 28。
    bool m_hasFace;

    //待高亮的字符区间（扁平文本口径）；空表示不高亮。
    std::vector<HighlightRange> m_ranges;

    //正文里识别出的超链接（扁平文本口径），SetContent 时一次性算好；识别开关
    //关掉时恒为空，于是渲染与命中两处自然退化成"没有链接"。
    std::vector<balloonchatwnd::LinkSpan> m_links;

    //最近一次绘制时的字符级排版模型与控件原点。链接命中、拖选时把落点映射成字符
    //下标、画选区与关键字高亮底色都用它 —— 排版只在绘制时算得出来（要 HDC 与字体），
    //故每次绘制都重建一份存下来给鼠标事件用。m_charLayoutValid 为 false 表示还没画过
    //（或内容已换），此时鼠标事件一律不处理。
    mutable balloonchatwnd::SelLayout m_charLayout;
    mutable POINT                     m_charOrigin;
    mutable bool                      m_charLayoutValid;

    //最近一次绘制时可见部分的末尾下标（扁平文本口径）。折叠态下它小于全文长度，
    //拖选的终点不越过它，免得把没显示出来的内容也选进去。
    mutable int m_visibleEnd;

    //正文颜色。
    COLORREF m_textColor;

    //正文字号（像素）。
    int m_fontPx;

    //折叠 / 展开态；true=画全。
    bool m_expanded;

    //是否单行模式（只画一行、放不下补省略号）；为 true 时优先于折叠 / 展开态。
    bool m_singleLine;

    //是否按控件高度截断（见 SetFitToHeight）；为 true 时绘制时按控件高度算行数上限，优先于折叠 / 展开态。
    bool m_fitToHeight;

    // ---- 文字选区（扁平文本口径；换内容时清空）----

    //选区锚点：按下处的字符下标。-1 表示没有选区。
    int m_selAnchor;

    //选区活动端：拖动中鼠标当前所在的字符下标。-1 表示没有选区；与锚点相等时是空选区。
    int m_selCaret;

    //是否正在拖选：左键按下后置 true，抬起时置 false。拖选期间本控件抓着鼠标。
    bool m_selDragging;

    //按下时命中的超链接下标，-1 表示没按在链接上。链接在抬起时才判定为点击：只有按下
    //与抬起落在同一条链接上、且期间没有选出文字，才打开它。
    int m_linkPressIdx;

    // ---- 高度测量缓存（生命周期同本控件；内容 / 字号变化时由 InvalidateMeasureCache 作废）----

    //上次测量用的可用宽度（像素）；-1 表示尚未测量过 / 缓存已作废。
    mutable int m_measuredWidth;

    //上次测量时的折叠态。它与 m_measuredWidth 一起构成缓存键 —— 展开与折叠是两个
    //不同的高度，只按宽度做键的话切换折叠态会拿到上一态的行高。
    mutable bool m_measuredExpanded;

    //上次测出来的内容高度（像素）。
    mutable int m_measuredHeight;

    //上次测出来的"折叠态放不下"标志，即 NeedsExpandLink 的返回值。
    mutable bool m_measuredNeedExpand;
};

} // namespace balloonhistory
