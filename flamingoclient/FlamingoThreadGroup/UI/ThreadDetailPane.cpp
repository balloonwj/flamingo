/**
 *  帖子群窗口右栏的帖子详情，见 ThreadDetailPane.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadDetailPane.h"

#include <memory>

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../Core/ThreadTypes.h"   // kMaxAddenda / kMaxImages
#include "../Core/ThreadRules.h"   // ScrollToReveal：滚动到本人新发的留言
#include "ThreadRichText.h"
#include "ThreadWidgets.h"

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiRichEdit;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素（样稿第 0 期） ----
const int kPadX = 22;               //详情左右内边距
const int kPadTop = 18;             //详情上内边距
const int kPadBottom = 18;          //详情下内边距
const int kBlockGap = 12;           //相邻两部分之间的距离
const int kHeaderHeight = 40;       //作者行高
const int kAvatarPx = 34;           //作者头像边长
const int kAvatarGap = 10;          //头像与名字之间的距离
const int kNameRowH = 20;           //名字一行高
const int kTimeRowH = 18;           //时间一行高
const int kOwnerTagH = 16;          //「楼主」标记高
const int kOwnerTagPad = 4;         //「楼主」标记左右内边距
const int kOwnerTagRadius = 3;      //「楼主」标记圆角
const int kOwnerTagGap = 6;         //名字与「楼主」标记之间的距离
const int kOpButtonH = 26;          //「编辑」「置顶」「删除」按钮高
const int kOpButtonPad = 10;        //这几个按钮文字两侧的内边距
const int kOpGap = 4;               //这几个按钮之间的距离
const int kFacePx = 20;             //正文里表情图片的边长
const int kImagePx = 132;           //图片网格里每张图的边长
const int kImageGap = 6;            //图片之间的距离
const int kImageRadius = 6;         //图片圆角
const int kImagesPerRow = 3;        //图片网格每行几张
const int kReactRowH = 26;          //互动标签一行高
const int kChipGap = 6;             //互动标签之间的距离
const int kAddBarWidth = 3;         //补充左侧竖条宽
const int kAddPadX = 12;            //补充内容左右内边距
const int kAddPadY = 8;             //补充内容上下内边距
const int kAddHeadH = 18;           //补充标题行高
const int kAddGap = 6;              //补充里相邻两部分之间的距离
const int kAddImagePx = 72;         //补充里每张图的边长
const int kAddButtonH = 28;         //「＋ 追加补充」按钮高
const int kAddButtonPad = 12;       //「＋ 追加补充」文字两侧的内边距
const int kAddDeleteW = 40;         //补充标题行「删除」按钮宽
const int kSectionTitleH = 26;      //留言区标题行高
const int kCommentSpacing = 14;     //相邻两条留言之间的距离
const int kCommentAvatarPx = 28;    //留言人头像边长
const int kCommentAvatarGap = 10;   //留言人头像与右侧内容之间的距离
const int kCommentHeadH = 18;       //留言名字行高
const int kCommentGap = 4;          //一条留言里相邻两部分之间的距离
const int kActionRowH = 22;         //操作行（时间、回复、互动、删除）高
const int kActionPad = 6;           //操作行里文字按钮两侧的内边距
const int kReplyPadX = 10;          //回复块左右内边距
const int kReplyPadY = 6;           //回复块上下内边距
const int kReplyGap = 8;            //相邻两条回复之间的距离
const int kReplyRadius = 6;         //回复块圆角
const int kMoreRepliesH = 22;       //「查看更多回复」一行高
const int kLoadMoreH = 30;          //「加载更多留言」一行高
const int kCommentFacePx = 18;      //留言里表情图片的边长

//---- 颜色 ----
const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);               //详情底色
const COLORREF kTextColor = RGB(0x22, 0x22, 0x22);        //正文
const COLORREF kNameColor = RGB(0x1A, 0x1A, 0x1A);        //名字、标题
const COLORREF kFaintColor = RGB(0x9A, 0x9A, 0x9A);       //时间、「已编辑」
const COLORREF kAccent = RGB(0xFF, 0x7A, 0x45);           //主色：「楼主」标记底色、补充竖条
const COLORREF kOnAccent = RGB(0xFF, 0xFF, 0xFF);         //主色上的文字
const COLORREF kAddBg = RGB(0xFF, 0xF9, 0xF5);            //补充的底色
const COLORREF kAddHeadColor = RGB(0xC2, 0x50, 0x1F);     //补充标题行文字
const COLORREF kImagePlaceholder = RGB(0xEE, 0xEC, 0xE8); //图片还没下载时的占位底色
const COLORREF kDeletedColor = RGB(0x9A, 0x9A, 0x9A);     //「帖子已被删除」「该留言已删除」
const COLORREF kReplyBg = RGB(0xF7, 0xF6, 0xF3);          //回复块底色
const COLORREF kLineColor = RGB(0xEC, 0xEB, 0xE8);        //留言区标题下的分隔线
const COLORREF kLinkColor = RGB(0x2F, 0x6F, 0xD0);        //「查看更多回复」

//---- 界面文字 ----
const wchar_t* const kOwnerTag = TR_NOOP(L"楼主");
const wchar_t* const kEditedText = TR_NOOP(L"已编辑");
const wchar_t* const kEditText = TR_NOOP(L"编辑");
const wchar_t* const kPinText = TR_NOOP(L"置顶");
const wchar_t* const kUnpinText = TR_NOOP(L"取消置顶");
const wchar_t* const kDeleteText = TR_NOOP(L"删除");
const wchar_t* const kAddendumHead = TR_NOOP(L"补充 · {0}");
const wchar_t* const kAddAddendumText = TR_NOOP(L"＋ 追加补充");
const wchar_t* const kUnknownFace = TR_NOOP(L"[表情]");
const wchar_t* const kDeletedText = TR_NOOP(L"这条帖子已被删除");
const wchar_t* const kCommentsTitle = TR_NOOP(L"留言  {0} 条");
const wchar_t* const kReplyText = TR_NOOP(L"回复");
const wchar_t* const kCommentDeleted = TR_NOOP(L"该留言已删除");
const wchar_t* const kReplyPrefix = TR_NOOP(L"{0} 回复 {1}：");
const wchar_t* const kOwnerSuffix = TR_NOOP(L"{0}（楼主）");
const wchar_t* const kMoreReplies = TR_NOOP(L"查看更多回复（{0} 条）");
const wchar_t* const kLoadMoreComments = TR_NOOP(L"加载更多留言");
const wchar_t* const kCommentsLoading = TR_NOOP(L"正在加载留言…");
const wchar_t* const kNoComments = TR_NOOP(L"还没有留言");
const wchar_t* const kAddFacePostText = TR_NOOP(L"＋ 表情");
const wchar_t* const kAddFaceCommentText = TR_NOOP(L"＋");

/**
 *  可以单击的图片：圆角 DuiAvatar，整块接管命中，单击时以 NotifyParent(DUIN_CLICK) 通知窗口（控件 id 区分是哪一张）。
 *  只覆写命中与鼠标按键（子类化扩展，不改 balloonui）。
 */
class CClickableImage : public DuiAvatar
{
public:
    CClickableImage()
        : m_pressed(false)
    {
        SetShape(DuiAvatar::ShapeRoundRect);
        SetCornerRadius(kImageRadius);
        SetFallbackBgColor(kImagePlaceholder);
    }

    //点在图片里时返回自己
    virtual DuiControl* HitTest(POINT ptHostClient) override
    {
        if (!IsVisible())
            return NULL;
        const RECT rc = GetRect();
        return ::PtInRect(&rc, ptHostClient) ? this : NULL;
    }

    //按下：记下按在了图片上
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override
    {
        (void)pt;
        (void)mkFlags;
        m_pressed = true;
        return true;
    }

    //抬起：按下与抬起都在图片上时发出单击通知
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override
    {
        (void)mkFlags;
        const RECT rc = GetRect();
        const bool click = m_pressed && ::PtInRect(&rc, pt);
        m_pressed = false;
        if (click)
            NotifyParent(balloonwjui::DUIN_CLICK);
        return true;
    }

private:
    bool m_pressed;   //鼠标是否在图片上按下（还没抬起）
};

//建一个文字按钮（Ghost 样式：透明底、悬停浅灰）
std::unique_ptr<DuiButton> MakeGhostButton(const std::wstring& text, UINT id)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text.c_str());
    button->SetVariant(DuiButton::Variant::Ghost);
    button->SetCtrlId(id);
    button->SetTabStop(false);
    return button;
}

//把一组图片按每行 perRow 张排进 parent（每张 sizePx 见方）；idBase 大于 0 时图片可单击，控件 id 为 idBase + 序号
void AddImageGrid(DuiVBox* parent, const std::vector<HBITMAP>& images, int sizePx, int perRow, UINT idBase, int gapAfter)
{
    typedef DuiLayout::Hint Hint;
    for (size_t start = 0; start < images.size(); start += static_cast<size_t>(perRow))
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kImageGap);
        for (size_t i = start; i < images.size() && i < start + static_cast<size_t>(perRow); ++i)
        {
            std::unique_ptr<CClickableImage> image(new CClickableImage());
            if (images[i] != NULL)
                image->SetBitmap(images[i]);
            if (idBase > 0)
                image->SetCtrlId(idBase + static_cast<UINT>(i));
            row->AddChild(std::unique_ptr<DuiControl>(image.release()), Hint().Fixed(sizePx, sizePx));
        }
        row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        const bool lastRow = (start + static_cast<size_t>(perRow) >= images.size());
        parent->AddChild(std::unique_ptr<DuiControl>(row.release()),
                         Hint().Fixed(sizePx).Margin(0, 0, 0, lastRow ? gapAfter : kImageGap));
    }
}

} // namespace

CThreadDetailPane::CThreadDetailPane()
    : m_bodyFont(NULL)
    , m_boldFont(NULL)
    , m_titleFont(NULL)
    , m_faces(NULL)
    , m_postId(0)
    , m_viewerId(0)
    , m_atClickFn(NULL)
    , m_atClickUser(NULL)
{
    //内容高度由内容容器按各部分（含富文本框报告的高度）算出
    SetAutoContentHeight(true);
}

void CThreadDetailPane::Setup(HFONT bodyFont, HFONT boldFont, HFONT titleFont, const std::vector<ThreadFaceEntry>* faces)
{
    m_bodyFont = bodyFont;
    m_boldFont = boldFont;
    m_titleFont = titleFont;
    m_faces = faces;
}

void CThreadDetailPane::SetAtClickHandler(ThreadAtClickFn fn, void* user)
{
    m_atClickFn = fn;
    m_atClickUser = user;
}

int64_t CThreadDetailPane::PostId() const
{
    return m_postId;
}

void CThreadDetailPane::Rebuild(const DetailViewData& data)
{
    typedef DuiLayout::Hint Hint;
    const bool samePost = (data.m_postId == m_postId);
    const int scrollPos = samePost ? GetScrollPos() : 0;
    m_postId = data.m_postId;
    m_viewerId = data.m_myUserId;
    //留言区的按钮随内容重建，登记表一并重填
    m_actions.clear();
    m_commentRows.clear();
    const std::vector<ThreadFaceEntry> noFaces;
    const std::vector<ThreadFaceEntry>& faces = (m_faces != NULL) ? *m_faces : noFaces;

    std::unique_ptr<DuiVBox> content(new DuiVBox());
    content->SetBgColor(kBg);
    content->SetPadding(kPadX, kPadTop, kPadX, kPadBottom);

    if (data.m_deleted)
    {
        content->AddChild(std::unique_ptr<DuiControl>(
                              MakeTextLabel(TR(kDeletedText), kDeletedColor, m_bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                          Hint().Fixed(kHeaderHeight));
        SetContent(std::unique_ptr<DuiControl>(content.release()));
        LayoutTwice();
        SetScrollPos(0);
        return;
    }

    //作者行：头像 | 名字（楼主）与时间（已编辑）| 空白 | 编辑 置顶 删除
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetName(data.m_avatarGlyph.c_str());
    avatar->SetFallbackBgColor(data.m_avatarColor);
    if (data.m_avatarBitmap != NULL)
        avatar->SetBitmap(data.m_avatarBitmap);
    header->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                     Hint().Fixed(kAvatarPx, kAvatarPx).Margin(0, 0, kAvatarGap, 0).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiVBox> who(new DuiVBox());
    std::unique_ptr<DuiHBox> nameRow(new DuiHBox());
    nameRow->AddChild(std::unique_ptr<DuiControl>(
                          MakeTextLabel(data.m_authorName, kNameColor, m_boldFont,
                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX).release()),
                      Hint().Fixed(MeasureTextWidthPx(m_boldFont, data.m_authorName)));
    int tagWidth = 0;
    std::unique_ptr<DuiVBox> tag = MakePill(TR(kOwnerTag), kAccent, kOnAccent, m_bodyFont, kOwnerTagRadius, kOwnerTagPad, tagWidth);
    nameRow->AddChild(std::unique_ptr<DuiControl>(tag.release()),
                      Hint().Fixed(tagWidth, kOwnerTagH).Margin(kOwnerTagGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    nameRow->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    who->AddChild(std::unique_ptr<DuiControl>(nameRow.release()), Hint().Fixed(kNameRowH));
    std::wstring timeText = data.m_timeText;
    if (data.m_edited)
        timeText += std::wstring(L"  ") + TR(kEditedText);
    who->AddChild(std::unique_ptr<DuiControl>(
                      MakeTextLabel(timeText, kFaintColor, m_bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                  Hint().Fixed(kTimeRowH));
    header->AddChild(std::unique_ptr<DuiControl>(who.release()),
                     Hint().Weight(1).Fixed(-1, kNameRowH + kTimeRowH).AlignC(DuiLayout::AlignCenter));
    struct Op
    {
        bool           m_show;
        const wchar_t* m_text;
        UINT           m_id;
    };
    const Op ops[] =
    {
        { data.m_canEdit, kEditText, kIdDetailEdit },
        { data.m_canPin, data.m_pinned ? kUnpinText : kPinText, kIdDetailPin },
        { data.m_canDelete, kDeleteText, kIdDetailDelete },
    };
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i)
    {
        if (!ops[i].m_show)
            continue;
        const std::wstring text = TR(ops[i].m_text);
        const int width = MeasureTextWidthPx(m_bodyFont, text) + 2 * kOpButtonPad;
        header->AddChild(std::unique_ptr<DuiControl>(MakeGhostButton(text, ops[i].m_id).release()),
                         Hint().Fixed(width, kOpButtonH).Margin(kOpGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    }
    content->AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kHeaderHeight).Margin(0, 0, 0, kBlockGap));

    //标题与正文：只读富文本，高度随内容
    if (!data.m_title.empty())
    {
        //标题用加大加粗的字体；标题里不解析表情（发帖时标题只是普通文字）
        std::unique_ptr<DuiRichEdit> title = MakeReadOnlyRichText(m_titleFont, kNameColor, kBg);
        title->SetText(data.m_title.c_str());
        content->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Auto().Margin(0, 0, 0, kBlockGap));
    }
    if (!data.m_wireText.empty())
    {
        std::unique_ptr<CThreadAtRichEdit> body = MakeReadOnlyRichText(m_bodyFont, kTextColor, kBg);
        body->SetAtClickHandler(m_atClickFn, m_atClickUser);
        FillWireText(body.get(), data.m_wireText, faces, TR(kUnknownFace), kFacePx);
        HighlightAtSpans(body.get(), 0, data.m_wireText, data.m_at, m_viewerId, faces, TR(kUnknownFace));
        content->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Auto().Margin(0, 0, 0, kBlockGap));
    }

    //图片网格：每行三张，可单击
    if (!data.m_images.empty())
        AddImageGrid(content.get(), data.m_images, kImagePx, kImagesPerRow, kIdDetailImageBase, kBlockGap);

    //互动：「赞」与各表情标签（点了切换本人的表态）、「＋ 表情」
    {
        std::unique_ptr<DuiHBox> reacts(new DuiHBox());
        reacts->SetGap(kChipGap);
        AddReactionRow(reacts.get(), data.m_chips, thread_target_post, data.m_postId, TR(kAddFacePostText));
        reacts->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        content->AddChild(std::unique_ptr<DuiControl>(reacts.release()), Hint().Fixed(kReactRowH).Margin(0, 0, 0, kBlockGap));
    }

    //楼主的各段补充：左侧主色竖条 | 标题行、正文与图片
    for (size_t i = 0; i < data.m_addenda.size(); ++i)
    {
        const DetailAddendumView& add = data.m_addenda[i];
        std::unique_ptr<DuiHBox> block(new DuiHBox());
        std::unique_ptr<DuiVBox> bar(new DuiVBox());
        bar->SetBgColor(kAccent);
        block->AddChild(std::unique_ptr<DuiControl>(bar.release()), Hint().Fixed(kAddBarWidth));
        std::unique_ptr<DuiVBox> inner(new DuiVBox());
        inner->SetBgColor(kAddBg);
        inner->SetPadding(kAddPadX, kAddPadY, kAddPadX, kAddPadY);
        inner->SetGap(kAddGap);
        std::unique_ptr<DuiHBox> head(new DuiHBox());
        head->AddChild(std::unique_ptr<DuiControl>(
                           MakeTextLabel(TRF(kAddendumHead, add.m_timeText), kAddHeadColor, m_bodyFont,
                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                       Hint().Weight(1));
        if (add.m_canDelete && i < static_cast<size_t>(kMaxAddenda))
        {
            head->AddChild(std::unique_ptr<DuiControl>(
                               MakeGhostButton(TR(kDeleteText), kIdAddendumDeleteBase + static_cast<UINT>(i)).release()),
                           Hint().Fixed(kAddDeleteW, kAddHeadH));
        }
        inner->AddChild(std::unique_ptr<DuiControl>(head.release()), Hint().Fixed(kAddHeadH));
        if (!add.m_wireText.empty())
        {
            std::unique_ptr<CThreadAtRichEdit> text = MakeReadOnlyRichText(m_bodyFont, kTextColor, kAddBg);
            text->SetAtClickHandler(m_atClickFn, m_atClickUser);
            FillWireText(text.get(), add.m_wireText, faces, TR(kUnknownFace), kFacePx);
            HighlightAtSpans(text.get(), 0, add.m_wireText, add.m_at, m_viewerId, faces, TR(kUnknownFace));
            inner->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Auto());
        }
        if (!add.m_images.empty())
            AddImageGrid(inner.get(), add.m_images, kAddImagePx, kMaxImages, 0, 0);
        block->AddChild(std::unique_ptr<DuiControl>(inner.release()), Hint().Weight(1));
        content->AddChild(std::unique_ptr<DuiControl>(block.release()), Hint().Auto().Margin(0, 0, 0, kBlockGap));
    }

    //「＋ 追加补充」（楼主本人、补充还没满）
    if (data.m_canAddAddendum)
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        const std::wstring text = TR(kAddAddendumText);
        std::unique_ptr<DuiButton> add(new DuiButton());
        add->SetText(text.c_str());
        add->SetVariant(DuiButton::Variant::Outlined);
        add->SetCtrlId(kIdDetailAddAddendum);
        add->SetTabStop(false);
        row->AddChild(std::unique_ptr<DuiControl>(add.release()),
                      Hint().Fixed(MeasureTextWidthPx(m_bodyFont, text) + 2 * kAddButtonPad, kAddButtonH));
        row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        content->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kAddButtonH).Margin(0, 0, 0, kBlockGap));
    }

    BuildComments(content.get(), data);

    SetContent(std::unique_ptr<DuiControl>(content.release()));
    LayoutTwice();
    SetScrollPos(scrollPos);
    Invalidate();
}

int CThreadDetailPane::ActionAt(UINT ctrlId, int64_t& commentId) const
{
    commentId = 0;
    if (ctrlId < kIdCommentActionBase || ctrlId > kIdCommentActionLast)
        return detail_action_none;
    const size_t slot = static_cast<size_t>(ctrlId - kIdCommentActionBase);
    if (slot >= m_actions.size())
        return detail_action_none;
    commentId = m_actions[slot].m_targetId;
    return m_actions[slot].m_action;
}

bool CThreadDetailPane::SlotAt(UINT ctrlId, DetailActionSlot& out) const
{
    if (ctrlId < kIdCommentActionBase || ctrlId > kIdCommentActionLast)
        return false;
    const size_t slot = static_cast<size_t>(ctrlId - kIdCommentActionBase);
    if (slot >= m_actions.size())
        return false;
    out = m_actions[slot];
    return true;
}

UINT CThreadDetailPane::RegisterAction(int action, int64_t commentId)
{
    return RegisterSlot(action, thread_target_comment, commentId, 0);
}

UINT CThreadDetailPane::RegisterSlot(int action, int targetType, int64_t targetId, int reaction)
{
    const size_t capacity = static_cast<size_t>(kIdCommentActionLast - kIdCommentActionBase + 1);
    if (m_actions.size() >= capacity)
        return 0;
    DetailActionSlot slot;
    slot.m_action = action;
    slot.m_targetType = targetType;
    slot.m_targetId = targetId;
    slot.m_reaction = reaction;
    m_actions.push_back(slot);
    return kIdCommentActionBase + static_cast<UINT>(m_actions.size() - 1);
}

void CThreadDetailPane::AddReactionRow(DuiHBox* row, const std::vector<PostCardChip>& chips, int targetType, int64_t targetId,
                                       const std::wstring& plusText)
{
    typedef DuiLayout::Hint Hint;
    std::vector<UINT> ids;
    for (size_t i = 0; i < chips.size(); ++i)
        ids.push_back(RegisterSlot(detail_action_react, targetType, targetId, chips[i].m_reaction));
    AddReactionChips(row, chips, m_bodyFont, &ids);
    const UINT plusId = RegisterSlot(detail_action_add_face, targetType, targetId, 0);
    if (plusId != 0)
    {
        row->AddChild(std::unique_ptr<DuiControl>(MakeGhostButton(plusText, plusId).release()),
                      Hint().Fixed(MeasureTextWidthPx(m_bodyFont, plusText) + 2 * kActionPad, kReactionChipHeight)
                          .AlignC(DuiLayout::AlignCenter));
    }
}

void CThreadDetailPane::BuildComments(DuiVBox* content, const DetailViewData& data)
{
    typedef DuiLayout::Hint Hint;
    //标题行「留言 N 条」与下面的分隔线
    content->AddChild(std::unique_ptr<DuiControl>(
                          MakeTextLabel(TRF(kCommentsTitle, data.m_commentCount), kNameColor, m_boldFont,
                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                      Hint().Fixed(kSectionTitleH));
    std::unique_ptr<balloonwjui::DuiSeparator> line(new balloonwjui::DuiSeparator());
    line->SetColor(kLineColor);
    content->AddChild(std::unique_ptr<DuiControl>(line.release()), Hint().Fixed(1).Margin(0, 0, 0, kBlockGap));

    if (!data.m_commentsLoaded || data.m_comments.empty())
    {
        const std::wstring text = data.m_commentsLoaded ? TR(kNoComments) : TR(kCommentsLoading);
        content->AddChild(std::unique_ptr<DuiControl>(
                              MakeTextLabel(text, kFaintColor, m_bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                          Hint().Fixed(kLoadMoreH));
        return;
    }
    for (size_t i = 0; i < data.m_comments.size(); ++i)
    {
        content->AddChild(std::unique_ptr<DuiControl>(BuildComment(data.m_comments[i]).release()),
                          Hint().Auto().Margin(0, 0, 0, kCommentSpacing));
    }
    //「加载更多留言」或「正在加载留言…」
    if (data.m_commentsLoading || data.m_hasMoreComments)
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        if (data.m_commentsLoading)
        {
            row->AddChild(std::unique_ptr<DuiControl>(
                              MakeTextLabel(TR(kCommentsLoading), kFaintColor, m_bodyFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                          Hint().Weight(1));
        }
        else
        {
            row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
            const std::wstring text = TR(kLoadMoreComments);
            row->AddChild(std::unique_ptr<DuiControl>(MakeGhostButton(text, kIdLoadMoreComments).release()),
                          Hint().Fixed(MeasureTextWidthPx(m_bodyFont, text) + 2 * kAddButtonPad, kLoadMoreH));
            row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        }
        content->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kLoadMoreH));
    }
}

std::unique_ptr<DuiHBox> CThreadDetailPane::BuildComment(const DetailCommentView& comment)
{
    typedef DuiLayout::Hint Hint;
    const std::vector<ThreadFaceEntry> noFaces;
    const std::vector<ThreadFaceEntry>& faces = (m_faces != NULL) ? *m_faces : noFaces;
    std::unique_ptr<DuiHBox> item(new DuiHBox());

    //头像（占位留言画灰色的「?」）
    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetName(comment.m_deleted ? L"?" : comment.m_avatarGlyph.c_str());
    avatar->SetFallbackBgColor(comment.m_deleted ? kDeletedColor : comment.m_avatarColor);
    if (!comment.m_deleted && comment.m_avatarBitmap != NULL)
        avatar->SetBitmap(comment.m_avatarBitmap);
    item->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                   Hint().Fixed(kCommentAvatarPx, kCommentAvatarPx).Margin(0, 0, kCommentAvatarGap, 0).AlignC(DuiLayout::AlignNear));

    std::unique_ptr<DuiVBox> body(new DuiVBox());
    body->SetGap(kCommentGap);
    if (comment.m_deleted)
    {
        body->AddChild(std::unique_ptr<DuiControl>(
                           MakeTextLabel(TR(kCommentDeleted), kDeletedColor, m_bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                       Hint().Fixed(kCommentHeadH));
    }
    else
    {
        //名字行：名字（楼主标记）
        std::unique_ptr<DuiHBox> head(new DuiHBox());
        head->AddChild(std::unique_ptr<DuiControl>(
                           MakeTextLabel(comment.m_authorName, kNameColor, m_boldFont,
                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX).release()),
                       Hint().Fixed(MeasureTextWidthPx(m_boldFont, comment.m_authorName)));
        if (comment.m_authorIsOwner)
        {
            int tagWidth = 0;
            std::unique_ptr<DuiVBox> tag = MakePill(TR(kOwnerTag), kAccent, kOnAccent, m_bodyFont, kOwnerTagRadius, kOwnerTagPad, tagWidth);
            head->AddChild(std::unique_ptr<DuiControl>(tag.release()),
                           Hint().Fixed(tagWidth, kOwnerTagH).Margin(kOwnerTagGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
        }
        head->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        body->AddChild(std::unique_ptr<DuiControl>(head.release()), Hint().Fixed(kCommentHeadH));
        //正文
        std::unique_ptr<CThreadAtRichEdit> text = MakeReadOnlyRichText(m_bodyFont, kTextColor, kBg);
        text->SetAtClickHandler(m_atClickFn, m_atClickUser);
        FillWireText(text.get(), comment.m_wireText, faces, TR(kUnknownFace), kCommentFacePx);
        HighlightAtSpans(text.get(), 0, comment.m_wireText, comment.m_at, m_viewerId, faces, TR(kUnknownFace));
        body->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Auto());
        //操作行：时间、回复、互动、删除
        body->AddChild(std::unique_ptr<DuiControl>(
                           BuildActionRow(comment.m_timeText, comment.m_commentId, comment.m_chips, comment.m_canDelete).release()),
                       Hint().Fixed(kActionRowH));
    }

    //回复块：灰底圆角，回复按时间排列，最后是「查看更多回复」
    if (!comment.m_replies.empty() || comment.m_moreReplies > 0)
    {
        std::unique_ptr<DuiVBox> replies(new DuiVBox());
        replies->SetBgColor(kReplyBg);
        replies->SetCornerRadius(kReplyRadius);
        replies->SetPadding(kReplyPadX, kReplyPadY, kReplyPadX, kReplyPadY);
        replies->SetGap(kReplyGap);
        for (size_t i = 0; i < comment.m_replies.size(); ++i)
            replies->AddChild(std::unique_ptr<DuiControl>(BuildReply(comment.m_replies[i]).release()), Hint().Auto());
        if (comment.m_moreReplies > 0)
        {
            const UINT id = RegisterAction(detail_action_more_replies, comment.m_commentId);
            if (id != 0)
            {
                std::unique_ptr<DuiHBox> row(new DuiHBox());
                const std::wstring text = TRF(kMoreReplies, comment.m_moreReplies);
                std::unique_ptr<DuiButton> more = MakeGhostButton(text, id);
                more->SetTextColor(kLinkColor);
                row->AddChild(std::unique_ptr<DuiControl>(more.release()),
                              Hint().Fixed(MeasureTextWidthPx(m_bodyFont, text) + 2 * kActionPad, kMoreRepliesH));
                row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
                replies->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kMoreRepliesH));
            }
        }
        body->AddChild(std::unique_ptr<DuiControl>(replies.release()), Hint().Auto());
    }
    item->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));
    m_commentRows[comment.m_commentId] = item.get();
    return item;
}

std::unique_ptr<DuiVBox> CThreadDetailPane::BuildReply(const DetailReplyView& reply)
{
    typedef DuiLayout::Hint Hint;
    const std::vector<ThreadFaceEntry> noFaces;
    const std::vector<ThreadFaceEntry>& faces = (m_faces != NULL) ? *m_faces : noFaces;
    std::unique_ptr<DuiVBox> box(new DuiVBox());
    box->SetGap(kCommentGap);
    //「王五 回复 李四：」接正文（回复人是楼主时名字后面带「（楼主）」）
    const std::wstring author = reply.m_authorIsOwner ? TRF(kOwnerSuffix, reply.m_authorName) : reply.m_authorName;
    std::unique_ptr<CThreadAtRichEdit> text = MakeReadOnlyRichText(m_bodyFont, kTextColor, kReplyBg);
    text->SetAtClickHandler(m_atClickFn, m_atClickUser);
    text->SetText(TRF(kReplyPrefix, author, reply.m_replyToName).c_str());
    //正文接在前缀之后：片段的显示位置要加上前缀占的字符数
    const int replyBase = text->GetTextLength();
    AppendWireText(text.get(), reply.m_wireText, faces, TR(kUnknownFace), kCommentFacePx);
    HighlightAtSpans(text.get(), replyBase, reply.m_wireText, reply.m_at, m_viewerId, faces, TR(kUnknownFace));
    box->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Auto());
    box->AddChild(std::unique_ptr<DuiControl>(
                      BuildActionRow(reply.m_timeText, reply.m_commentId, reply.m_chips, reply.m_canDelete).release()),
                  Hint().Fixed(kActionRowH));
    m_commentRows[reply.m_commentId] = box.get();
    return box;
}

bool CThreadDetailPane::RevealComment(int64_t commentId, int64_t fallbackId)
{
    std::map<int64_t, DuiControl*>::const_iterator it = m_commentRows.find(commentId);
    if (it == m_commentRows.end() && fallbackId > 0)
        it = m_commentRows.find(fallbackId);
    DuiControl* content = GetContent();
    if (it == m_commentRows.end() || it->second == NULL || content == NULL)
        return false;
    //滚动时内容整体上移 scrollPos（DuiScrollView::ApplyScrollToContent），条目相对内容顶部的位置与滚动无关
    const RECT item = it->second->GetRect();
    const RECT contentRect = content->GetRect();
    const RECT view = GetRect();
    const int viewHeight = view.bottom - view.top;
    int maxScroll = GetContentHeight() - viewHeight;
    if (maxScroll < 0)
        maxScroll = 0;
    const int scrollPos = GetScrollPos();
    const int pos = ScrollToReveal(item.top - contentRect.top, item.bottom - contentRect.top, scrollPos, viewHeight,
                                   maxScroll);
    if (pos != scrollPos)
        SetScrollPos(pos);
    return true;
}

std::unique_ptr<DuiHBox> CThreadDetailPane::BuildActionRow(const std::wstring& timeText, int64_t commentId,
                                                           const std::vector<PostCardChip>& chips, bool canDelete)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kChipGap);
    if (!timeText.empty())
    {
        row->AddChild(std::unique_ptr<DuiControl>(
                          MakeTextLabel(timeText, kFaintColor, m_bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                      Hint().Fixed(MeasureTextWidthPx(m_bodyFont, timeText)));
    }
    const UINT replyId = RegisterAction(detail_action_reply, commentId);
    if (replyId != 0)
    {
        const std::wstring text = TR(kReplyText);
        row->AddChild(std::unique_ptr<DuiControl>(MakeGhostButton(text, replyId).release()),
                      Hint().Fixed(MeasureTextWidthPx(m_bodyFont, text) + 2 * kActionPad, kActionRowH));
    }
    AddReactionRow(row.get(), chips, thread_target_comment, commentId, TR(kAddFaceCommentText));
    row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    if (canDelete)
    {
        const UINT deleteId = RegisterAction(detail_action_delete, commentId);
        if (deleteId != 0)
        {
            const std::wstring text = TR(kDeleteText);
            row->AddChild(std::unique_ptr<DuiControl>(MakeGhostButton(text, deleteId).release()),
                          Hint().Fixed(MeasureTextWidthPx(m_bodyFont, text) + 2 * kActionPad, kActionRowH));
        }
    }
    return row;
}

void CThreadDetailPane::Relayout()
{
    LayoutTwice();
    Invalidate();
}

void CThreadDetailPane::LayoutTwice()
{
    //第一趟让富文本框拿到宽度，第二趟它们按这个宽度报告的高度才准（DuiRichEdit 自动增高的既定行为）
    const RECT rc = GetRect();
    ForceLayout(rc);
    ForceLayout(rc);
}

} // namespace threadgroup
