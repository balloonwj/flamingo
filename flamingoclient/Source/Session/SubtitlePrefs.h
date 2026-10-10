/**
 *  SubtitlePrefs.h
 *
 *  聊天字幕模式的偏好设置的<u>纯数据模型</u>：外观（字号、最多显示行数、共享屏幕或录屏时是否隐藏字幕）
 *  按账号存一份；每个会话的字幕位置、宽度与是否锁定各存一份。本模块只负责数据与配置文本之间的
 *  编码 / 解码，不读写文件，读写由 CUserConfig 完成（与 ChatFontPrefs 的分工相同）。
 *  方案见 docs/聊天字幕模式方案.md。
 *
 *  配置文件里的格式（全 ASCII）：
 *      [Subtitle]
 *      FontSize=28              ← 字号，96 DPI 下的像素，显示时按字幕所在显示器的 DPI 缩放
 *      MaxLines=2               ← 最多显示几行，1~3
 *      HideFromCapture=0        ← 共享屏幕或录屏时是否隐藏字幕（1 隐藏）
 *      [SubtitlePlacement]
 *      1_10023=120,860,960,1    ← 键为「会话类型_会话ID」，值为左、上、宽（屏幕坐标，物理像素）与是否锁定
 *
 *  典型用法：
 *      subtitle::SubtitlePlacement pl;
 *      if (cfg.Subtitles().GetPlacement(ctype, cid, pl)) { ... }   //进入字幕模式时
 *      cfg.Subtitles().SetPlacement(ctype, cid, pl);               //拖动、调宽、锁定或解锁之后
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <atlstr.h>       // CString
#include <stdint.h>
#include <map>
#include <utility>
#include <vector>

namespace subtitle {

//字号的可选档位（96 DPI 下的像素），设置菜单里依次显示为「小 / 中 / 大 / 特大」
const int kFontSizeSmall  = 22;
const int kFontSizeMedium = 28;
const int kFontSizeLarge  = 34;
const int kFontSizeHuge   = 42;
//默认字号：中
const int kDefaultFontSize = kFontSizeMedium;
//配置里的字号超出这个范围时按范围边界处理（手工改坏配置时不至于画出极端大小的字）
const int kMinFontSize = 16;
const int kMaxFontSize = 64;

//最多显示行数的取值范围与默认值（用户在方案确认时选定默认 2 行，可改为 1~3 行）
const int kMinMaxLines = 1;
const int kMaxMaxLines = 3;
const int kDefaultMaxLines = 2;

//字幕外观：一个账号一份，所有会话的字幕共用
struct SubtitleAppearance
{
    int  m_fontSize;         //字号，96 DPI 下的像素，取值在 [kMinFontSize, kMaxFontSize] 之内
    int  m_maxLines;         //最多显示行数，取值在 [kMinMaxLines, kMaxMaxLines] 之内
    bool m_hideFromCapture;  //共享屏幕、录屏时是否把字幕排除在画面之外；默认 false（用户选定默认不排除）

    SubtitleAppearance()
        : m_fontSize(kDefaultFontSize)
        , m_maxLines(kDefaultMaxLines)
        , m_hideFromCapture(false)
    {
    }
};

//一个会话的字幕上次所在的位置与锁定状态
struct SubtitlePlacement
{
    int  m_left;    //字幕窗口左边，屏幕坐标、物理像素；副屏在主屏左边时可以为负数
    int  m_top;     //字幕窗口上边，屏幕坐标、物理像素
    int  m_width;   //字幕窗口宽，物理像素，大于 0
    bool m_locked;  //是否锁定（锁定后点击穿透、不能拖动）

    SubtitlePlacement()
        : m_left(0)
        , m_top(0)
        , m_width(0)
        , m_locked(false)
    {
    }
};

/**
 *  把一个会话的字幕位置编码成配置文件里的一串 ASCII 文本。
 *    pl：位置与锁定状态。
 *  返回：形如 "120,860,960,1" 的文本，依次为左、上、宽、是否锁定。
 */
CString EncodePlacement(const SubtitlePlacement& pl);

/**
 *  把配置文件里的文本解码成字幕位置。
 *    text：配置文本。
 *    out： [出参] 解码结果；返回 false 时不改动。
 *  返回：格式正确返回 true；空串、字段数不是 4、含非数字、宽度不为正数时返回 false。
 */
bool DecodePlacement(LPCTSTR text, SubtitlePlacement& out);

/**
 *  拼出会话在 [SubtitlePlacement] 段里的键名。
 *    ctype：会话类型，1 单聊、2 群聊。
 *    cid：  单聊为对端 userid，群聊为群ID。
 *  返回：形如 "1_10023" 的键名。
 */
CString MakeConversationKey(int ctype, int64_t cid);

/**
 *  把配置里读到的外观整理到合法范围：字号限制在 [kMinFontSize, kMaxFontSize]，行数限制在
 *  [kMinMaxLines, kMaxMaxLines]。
 *    in：读到的外观（可能被手工改坏）。
 *  返回：整理后的外观。
 */
SubtitleAppearance NormalizeAppearance(const SubtitleAppearance& in);

/**
 *  SubtitlePrefs —— 一个账号的字幕偏好（外观 + 各会话的位置）。
 *
 *  由 CUserConfig 持有：LoadConfig 时灌入，SaveConfig 时导出；字幕会话经 CUserConfig::Subtitles()
 *  读写，改完由调用方负责存盘。
 */
class SubtitlePrefs
{
public:
    /**
     *  取字幕外观；没有设置过时为默认值（字号中、2 行、不排除采集）。
     */
    const SubtitleAppearance& GetAppearance() const;

    /**
     *  设置字幕外观。
     *    appearance：新的外观；超出范围的字段按 NormalizeAppearance 整理后保存。
     */
    void SetAppearance(const SubtitleAppearance& appearance);

    /**
     *  取某个会话的字幕位置。
     *    ctype / cid：同 MakeConversationKey。
     *    out：        [出参] 返回 true 时为该会话上次的位置；返回 false 时不改动。
     *  返回：该会话保存过位置返回 true。
     */
    bool GetPlacement(int ctype, int64_t cid, SubtitlePlacement& out) const;

    /**
     *  保存某个会话的字幕位置。
     *    ctype / cid：同 MakeConversationKey。
     *    pl：         位置与锁定状态；宽度不为正数时忽略本次保存。
     */
    void SetPlacement(int ctype, int64_t cid, const SubtitlePlacement& pl);

    /**
     *  把全部会话的位置导出成「键 = 值」列表，供写入 [SubtitlePlacement] 段；按会话类型、会话ID 升序排列。
     *    out：[出参] 导出结果；调用前会被清空。
     */
    void ExportPlacements(std::vector<std::pair<CString, CString> >& out) const;

    /**
     *  用配置文件里读出的「键 = 值」列表整体替换各会话的位置。键或值格式不对的行跳过，其余照常导入。
     *    entries：[SubtitlePlacement] 段的全部键值。
     */
    void ImportPlacements(const std::vector<std::pair<CString, CString> >& entries);

private:
    //字幕外观，所有会话共用
    SubtitleAppearance m_appearance;

    //各会话的字幕位置：键为 (会话类型, 会话ID)，值的宽度恒为正数
    std::map<std::pair<int, int64_t>, SubtitlePlacement> m_placements;
};

} // namespace subtitle
