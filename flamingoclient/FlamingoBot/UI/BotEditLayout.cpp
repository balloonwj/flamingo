/**
 *  机器人编辑窗的控件树，见 BotEditLayout.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotEditLayout.h"

#include "../Core/BotStrings.h"
#include "../Core/BotText.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace flamingobot {

namespace {

typedef DuiLayout::Hint Hint;

//---- 尺寸，单位：像素 ----
const int kPreviewAvatarSize = 56;   //头像预览的边长
const int kShortEditWidth = 80;      //头像字形输入框宽
const int kColorComboWidth = 140;    //头像底色下拉框宽
const int kPresenceComboWidth = 110; //模拟在线状态下拉框宽
const int kIndexWidth = 26;          //规则卡片左侧序号的宽度
const int kCheckRowHeight = 24;      //复选框一行高
const int kRuleListGap = 6;          //规则卡片之间的距离
const int kPreviewGap = 16;          //头像预览与右侧输入框之间的距离

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kLabelName = TR_NOOP(L"名称");
const wchar_t* const kLabelDesc = TR_NOOP(L"简介");
const wchar_t* const kLabelGlyph = TR_NOOP(L"头像字形");
const wchar_t* const kLabelColor = TR_NOOP(L"头像底色");
const wchar_t* const kLabelTryText = TR_NOOP(L"输入");
const wchar_t* const kLabelPresence = TR_NOOP(L"你的状态");
const wchar_t* const kLabelToken = TR_NOOP(L"令牌");
const wchar_t* const kLabelSecret = TR_NOOP(L"签名密钥");
const wchar_t* const kLabelWebhookUrl = TR_NOOP(L"地址");

//在内容区里加一行固定高度的控件
void AddRow(DuiVBox* content, std::unique_ptr<DuiControl> row, int height)
{
    content->AddChild(std::move(row), Hint().Fixed(height));
}

//加一段灰色的说明，按宽度自动换行
void AddNote(DuiVBox* content, const std::wstring& text, HFONT font, int indent, int width)
{
    const int height = MeasureWrappedHeight(font, text, width - indent);
    content->AddChild(std::unique_ptr<DuiControl>(MakeLabel(text, kBotFaint, font,
                                                            DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                      Hint().Fixed(height).Margin(indent, 0, 0, 0));
}

//加一行只读的输入框（令牌、签名密钥，可选中复制）
void AddReadOnlyField(DuiVBox* content, const std::wstring& label, UINT id, const std::wstring& text, HFONT font,
                      int labelWidth)
{
    std::unique_ptr<DuiEdit> edit = MakeEdit(id, std::wstring(), text, font, false, 0);
    edit->SetReadOnly(true);
    std::unique_ptr<DuiHBox> row = MakeFieldRow(label, font, labelWidth, std::unique_ptr<DuiControl>(edit.release()));
    AddRow(content, std::unique_ptr<DuiControl>(row.release()), kBotEditHeight);
}

} // namespace

EditControls::EditControls()
    : m_avatar(NULL)
    , m_name(NULL)
    , m_desc(NULL)
    , m_glyph(NULL)
    , m_color(NULL)
    , m_addRule(NULL)
    , m_rules(NULL)
    , m_tryText(NULL)
    , m_tryAt(NULL)
    , m_tryPresence(NULL)
    , m_tryButton(NULL)
    , m_tryResult(NULL)
    , m_save(NULL)
{
}

std::unique_ptr<DuiVBox> BuildEditBody(const EditView& view, HFONT font, EditControls& out)
{
    out = EditControls();
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec cancel;
    cancel.m_text = TR(L"取消");
    cancel.m_id = kIdEditCancel;
    cancel.m_variant = DuiButton::Variant::Default;
    buttons.push_back(cancel);
    FooterButtonSpec save;
    save.m_text = TR(L"保存");
    save.m_id = kIdEditSave;
    save.m_variant = DuiButton::Variant::Primary;
    buttons.push_back(save);
    std::unique_ptr<DuiVBox> root = BuildScrollFrame(font, buttons, out.m_frame);
    if (out.m_frame.m_buttons.size() == buttons.size())
        out.m_save = out.m_frame.m_buttons[1];
    DuiVBox* content = out.m_frame.m_content;
    const bool personal = (view.m_ownerType == bot_owner_personal);

    std::vector<std::wstring> labels;
    labels.push_back(TR(kLabelName));
    labels.push_back(TR(kLabelDesc));
    labels.push_back(TR(kLabelGlyph));
    labels.push_back(TR(kLabelColor));
    labels.push_back(TR(kLabelTryText));
    labels.push_back(TR(kLabelPresence));
    labels.push_back(TR(kLabelToken));
    labels.push_back(TR(kLabelSecret));
    labels.push_back(TR(kLabelWebhookUrl));
    const int labelWidth = FieldLabelWidth(font, labels);
    const int indent = labelWidth + kBotRowGap;
    int width = 0;

    //基本信息：左侧头像预览，右侧名称与简介
    AddRow(content, std::unique_ptr<DuiControl>(MakeSectionRow(TR(L"基本信息"), std::wstring(), 0, font, NULL).release()),
           kBotSectionHeight);
    std::unique_ptr<DuiHBox> basic(new DuiHBox());
    basic->SetGap(kPreviewGap);
    std::unique_ptr<DuiAvatar> avatar = MakeBotAvatar(view.m_glyph, view.m_name, view.m_color);
    out.m_avatar = avatar.get();
    basic->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                    Hint().Fixed(kPreviewAvatarSize, kPreviewAvatarSize).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiVBox> fields(new DuiVBox());
    fields->SetGap(kBotRowGap);
    std::unique_ptr<DuiEdit> name = MakeEdit(kIdEditName, TRF(L"必填，最多 {0} 字", kBotNameMaxChars), view.m_name, font,
                                             false, kBotNameMaxChars);
    out.m_name = name.get();
    fields->AddChild(std::unique_ptr<DuiControl>(MakeFieldRow(TR(kLabelName), font, labelWidth,
                                                              std::unique_ptr<DuiControl>(name.release())).release()),
                     Hint().Fixed(kBotEditHeight));
    //个人机器人不出现在群里，简介不会显示在群成员页签的资料卡上，提示里不提这一句（2026-10-08 实机核对）
    const std::wstring descHint = (view.m_ownerType == bot_owner_personal)
                                      ? TRF(L"可不填，最多 {0} 字", kBotDescMaxChars)
                                      : TRF(L"可不填，最多 {0} 字；显示在群成员页签的资料卡上", kBotDescMaxChars);
    std::unique_ptr<DuiEdit> desc = MakeEdit(kIdEditDesc, descHint, view.m_desc, font, false, kBotDescMaxChars);
    out.m_desc = desc.get();
    fields->AddChild(std::unique_ptr<DuiControl>(MakeFieldRow(TR(kLabelDesc), font, labelWidth,
                                                              std::unique_ptr<DuiControl>(desc.release())).release()),
                     Hint().Fixed(kBotEditHeight));
    basic->AddChild(std::unique_ptr<DuiControl>(fields.release()), Hint().Weight(1));
    AddRow(content, std::unique_ptr<DuiControl>(basic.release()), kBotEditHeight * 2 + kBotRowGap);

    //头像字形与底色
    std::unique_ptr<DuiHBox> glyphRow(new DuiHBox());
    glyphRow->SetGap(kBotRowGap);
    std::unique_ptr<DuiEdit> glyph = MakeEdit(kIdEditGlyph, std::wstring(), view.m_glyph, font, false, kBotGlyphMaxChars);
    out.m_glyph = glyph.get();
    glyphRow->AddChild(std::unique_ptr<DuiControl>(glyph.release()), Hint().Fixed(kShortEditWidth));
    glyphRow->AddChild(std::unique_ptr<DuiControl>(MakeLabel(TRF(L"1～{0} 个字，不填时取名称的第一个字", kBotGlyphMaxChars),
                                                             kBotFaint, font,
                                                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                       Hint().Weight(1));
    AddRow(content, std::unique_ptr<DuiControl>(MakeFieldRow(TR(kLabelGlyph), font, labelWidth,
                                                             std::unique_ptr<DuiControl>(glyphRow.release())).release()),
           kBotEditHeight);
    size_t presetCount = 0;
    const BotColorPreset* presets = ColorPresets(presetCount);
    std::vector<std::wstring> colorNames;
    for (size_t i = 0; i < presetCount; ++i)
        colorNames.push_back(TR(presets[i].m_name));
    std::unique_ptr<DuiHBox> colorRow(new DuiHBox());
    std::unique_ptr<DuiComboBox> color = MakeCombo(kIdEditColor, colorNames, ColorPresetIndex(WideToUtf8(view.m_color)));
    out.m_color = color.get();
    colorRow->AddChild(std::unique_ptr<DuiControl>(color.release()), Hint().Fixed(kColorComboWidth));
    colorRow->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    AddRow(content, std::unique_ptr<DuiControl>(MakeFieldRow(TR(kLabelColor), font, labelWidth,
                                                             std::unique_ptr<DuiControl>(colorRow.release())).release()),
           kBotEditHeight);

    //规则
    DuiButton* addRule = NULL;
    AddRow(content, std::unique_ptr<DuiControl>(MakeSectionRow(TR(L"规则"), TR(L"添加规则"), kIdEditAddRule, font,
                                                               &addRule).release()),
           kBotSectionHeight + kBotRowGap);
    out.m_addRule = addRule;
    AddNote(content, TRF(L"按顺序判定，命中第一条后不再往下判定；最多 {0} 条。", kBotRuleMax), font, 0, kEditContentWidth);
    std::unique_ptr<DuiVBox> rules(new DuiVBox());
    rules->SetGap(kRuleListGap);
    out.m_rules = rules.get();
    AddRow(content, std::unique_ptr<DuiControl>(rules.release()), 0);

    //试一试
    AddRow(content, std::unique_ptr<DuiControl>(MakeSectionRow(TR(L"试一试"), std::wstring(), 0, font, NULL).release()),
           kBotSectionHeight);
    AddNote(content, personal ? TR(L"用编辑中的内容（不必先保存）判定一段别人发给你的单聊消息，不会真的发出任何消息。")
                              : TR(L"用编辑中的内容（不必先保存）判定一段本群的群消息，不会真的发出任何消息。"),
            font, 0, kEditContentWidth);
    std::unique_ptr<DuiEdit> tryText = MakeEdit(kIdEditTryText, TR(L"输入一段文字，看机器人会不会回复、回复什么"),
                                                std::wstring(), font, false, 0);
    out.m_tryText = tryText.get();
    AddRow(content, std::unique_ptr<DuiControl>(MakeFieldRow(TR(kLabelTryText), font, labelWidth,
                                                             std::unique_ptr<DuiControl>(tryText.release())).release()),
           kBotEditHeight);
    std::unique_ptr<DuiHBox> options(new DuiHBox());
    options->SetGap(kBotRowGap);
    if (personal)
    {
        std::unique_ptr<DuiLabel> label = MakeLabel(TR(kLabelPresence), kBotSubText, font,
                                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        options->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(labelWidth));
        std::vector<std::wstring> states;
        for (int p = bot_presence_offline; p <= bot_presence_busy; ++p)
            states.push_back(PresenceName(p));
        std::unique_ptr<DuiComboBox> presence = MakeCombo(kIdEditTryPresence, states, bot_presence_away);
        out.m_tryPresence = presence.get();
        options->AddChild(std::unique_ptr<DuiControl>(presence.release()), Hint().Fixed(kPresenceComboWidth));
    }
    else
    {
        std::unique_ptr<DuiButton> at = MakeCheckBox(TR(L"消息里 @ 了机器人"), kIdEditTryAt, true, font, width);
        out.m_tryAt = at.get();
        options->AddChild(std::unique_ptr<DuiControl>(at.release()),
                          Hint().Fixed(width).Margin(indent, 0, 0, 0));
    }
    options->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> tryButton = MakeButton(TR(L"试一试"), kIdEditTry, DuiButton::Variant::Outlined, font, width);
    out.m_tryButton = tryButton.get();
    options->AddChild(std::unique_ptr<DuiControl>(tryButton.release()), Hint().Fixed(width));
    AddRow(content, std::unique_ptr<DuiControl>(options.release()), kBotButtonHeight);
    std::unique_ptr<DuiLabel> result = MakeLabel(std::wstring(), kBotText, font, DT_LEFT | DT_TOP | DT_WORDBREAK);
    out.m_tryResult = result.get();
    AddRow(content, std::unique_ptr<DuiControl>(result.release()), 0);

    //外部系统推送（Webhook）
    if (view.m_showWebhook)
    {
        AddRow(content, std::unique_ptr<DuiControl>(MakeSectionRow(TR(L"外部系统推送（Webhook）"), std::wstring(), 0, font,
                                                                   NULL).release()),
               kBotSectionHeight);
        //服务端配置了对外地址时直接给出完整地址；没有配置时只能说明地址的格式，由企业管理员告知服务器地址与端口
        if (view.m_webhookUrl.empty())
        {
            AddNote(content,
                    TR(L"规则里有「外部系统推送」时，外部系统向 http://<服务器地址>:<端口>/bot/webhook.do 发 POST 请求，带上下面的令牌即可把消息发到本群（服务器地址与端口请向企业管理员询问）。带签名时用签名密钥计算 HMAC-SHA256。"),
                    font, 0, kEditContentWidth);
        }
        else
        {
            AddNote(content,
                    TR(L"规则里有「外部系统推送」时，外部系统向下面的地址发 POST 请求，带上令牌即可把消息发到本群。带签名时用签名密钥计算 HMAC-SHA256。"),
                    font, 0, kEditContentWidth);
        }
        if (view.m_webhookToken.empty())
        {
            AddNote(content, TR(L"保存之后生成令牌与签名密钥，重新打开本窗口即可看到。"), font, indent, kEditContentWidth);
        }
        else
        {
            if (!view.m_webhookUrl.empty())
                AddReadOnlyField(content, TR(kLabelWebhookUrl), kIdEditWebhookUrl, view.m_webhookUrl, font, labelWidth);
            AddReadOnlyField(content, TR(kLabelToken), kIdEditToken, view.m_webhookToken, font, labelWidth);
            AddReadOnlyField(content, TR(kLabelSecret), kIdEditSecret, view.m_secret, font, labelWidth);
        }
    }
    return root;
}

void FillRuleList(EditControls& controls, const std::vector<BotRule>& rules, int ownerType, HFONT font)
{
    DuiVBox* list = controls.m_rules;
    DuiVBox* content = controls.m_frame.m_content;
    if (list == NULL || content == NULL)
        return;
    RemoveAllChildren(list);
    int total = 0;
    if (rules.empty())
    {
        const std::wstring empty = TR(L"还没有规则，点「添加规则」开始。");
        const int height = MeasureWrappedHeight(font, empty, kEditContentWidth);
        list->AddChild(std::unique_ptr<DuiControl>(MakeLabel(empty, kBotFaint, font,
                                                             DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                       Hint().Fixed(height));
        total = height;
    }
    for (size_t i = 0; i < rules.size(); ++i)
    {
        const BotRule& rule = rules[i];
        std::unique_ptr<DuiVBox> card = MakeCard();
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kBotRowGap);
        row->AddChild(std::unique_ptr<DuiControl>(MakeLabel(std::to_wstring(static_cast<long long>(i + 1)) + L".",
                                                            kBotSubText, font,
                                                            DT_LEFT | DT_TOP | DT_SINGLELINE).release()),
                      Hint().Fixed(kIndexWidth));
        //右侧四个按钮：编辑、上移、下移、删除（顺序与 EditRuleButton 一致）
        std::vector<std::wstring> buttonTexts;
        buttonTexts.push_back(TR(L"编辑"));
        buttonTexts.push_back(TR(L"上移"));
        buttonTexts.push_back(TR(L"下移"));
        buttonTexts.push_back(TR(L"删除"));
        std::vector<std::unique_ptr<DuiButton> > buttons;
        std::vector<int> widths;
        int buttonsWidth = 0;
        for (size_t b = 0; b < buttonTexts.size(); ++b)
        {
            int width = 0;
            std::unique_ptr<DuiButton> button = MakeButton(buttonTexts[b], EditRuleButtonId(i, static_cast<int>(b)),
                                                           DuiButton::Variant::Text, font, width);
            const bool disabled = (b == edit_rule_up && i == 0) || (b == edit_rule_down && i + 1 == rules.size());
            if (disabled)
                button->SetEnabled(false);
            buttonsWidth += width + kBotRowGap;
            widths.push_back(width);
            buttons.push_back(std::move(button));
        }
        std::wstring summary = RuleSummary(rule, ownerType);
        if (!rule.m_name.empty())
            summary = TRF(L"{0}：{1}", Utf8ToWide(rule.m_name), summary);
        if (!rule.m_enabled)
            summary = TRF(L"（已停用）{0}", summary);
        const int textWidth = kEditContentWidth - 2 * kBotCardPad - kIndexWidth - kBotRowGap - buttonsWidth;
        const int textHeight = MeasureWrappedHeight(font, summary, textWidth);
        row->AddChild(std::unique_ptr<DuiControl>(MakeLabel(summary, rule.m_enabled ? kBotText : kBotFaint, font,
                                                            DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                      Hint().Weight(1));
        for (size_t b = 0; b < buttons.size(); ++b)
        {
            row->AddChild(std::unique_ptr<DuiControl>(buttons[b].release()),
                          Hint().Fixed(widths[b], kBotButtonHeight).AlignC(DuiLayout::AlignNear));
        }
        const int rowHeight = (textHeight > kBotButtonHeight) ? textHeight : kBotButtonHeight;
        card->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(rowHeight));
        const int cardHeight = rowHeight + 2 * kBotCardPad;
        list->AddChild(std::unique_ptr<DuiControl>(card.release()), Hint().Fixed(cardHeight));
        total += cardHeight + (i > 0 ? kRuleListGap : 0);
    }
    content->SetHint(list, Hint().Fixed(total));
    if (controls.m_addRule != NULL)
        controls.m_addRule->SetEnabled(static_cast<int>(rules.size()) < kBotRuleMax);
    RelayoutScroll(controls.m_frame.m_scroll);
    //规则列表嵌在滚动内容里面一层：RelayoutScroll 对内容 ForceLayout 时给规则列表调的是 SetRect，总高度不变（如编辑一条
    //规则后摘要行数没变）时矩形没变、直接返回，新建的卡片没有排版，那一行画不出来（开发约定文档 UI 约定
    //第 5 条：ForceLayout 只强制它作用的那一层）。所以再对规则列表本身 ForceLayout 一次（用例 BOTL8）
    list->ForceLayout(list->GetRect());
    list->Invalidate();
}

void SetTryResult(EditControls& controls, const std::wstring& text, bool isError, HFONT font)
{
    DuiLabel* label = controls.m_tryResult;
    DuiVBox* content = controls.m_frame.m_content;
    if (label == NULL || content == NULL)
        return;
    label->SetTextColor(isError ? kBotError : kBotText);
    label->SetText(text.c_str());
    const int height = text.empty() ? 0 : MeasureWrappedHeight(font, text, kEditContentWidth);
    content->SetHint(label, Hint().Fixed(height));
    RelayoutScroll(controls.m_frame.m_scroll);
}

UINT EditRuleButtonId(size_t index, int button)
{
    return kIdEditRuleFirst + static_cast<UINT>(index) * kEditRuleIdStride + static_cast<UINT>(button);
}

bool ParseEditRuleButtonId(UINT id, size_t& index, int& button)
{
    if (id < kIdEditRuleFirst || id > kIdEditRuleLast)
        return false;
    index = (id - kIdEditRuleFirst) / kEditRuleIdStride;
    button = static_cast<int>((id - kIdEditRuleFirst) % kEditRuleIdStride);
    return true;
}

} // namespace flamingobot
