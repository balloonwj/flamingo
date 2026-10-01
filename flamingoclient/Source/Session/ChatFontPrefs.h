/**
 *  ChatFontPrefs.h
 *
 *  聊天字体偏好的<u>纯数据模型</u>：全局默认字体 + 各会话的自定义字体，以及它们与
 *  账号配置文件（.ini）里那串 ASCII 文本之间的转换。
 *
 *  两层字体的规则：
 *    · 全局默认：在系统设置的「聊天」页设置；
 *    · 会话自定义：在某个聊天窗经「AF」字体浮层设置，只对这个会话（单聊的某个人
 *      或某个群）生效；
 *    · 取用顺序：会话自定义 > 全局默认 > 未设置（发出的消息不带字体，接收方按气泡
 *      默认样式显示）。
 *  两层都由 CUserConfig 持有并存进账号配置，重启后仍然有效。
 *
 *  本模块不读写文件、不碰窗口，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个
 *  工程逐字共用（见 Tests/ChatFontPrefsTests.cpp）。在 Flamingo.vcxproj 里本文件
 *  需标记 PrecompiledHeader=NotUsing。
 *
 *  配置文件里的格式（全 ASCII）：
 *      [ChatDlg]
 *      DefaultChatFont=1,14,0,0,0,0,1710618     ← 是否设置,字号,粗体,斜体,下划线,删除线,颜色
 *      [ChatFontOverride]
 *      2_268435458=1,20,1,0,0,0,16711680        ← 键为"会话类型_会话ID"
 *
 *  典型用法：
 *      net::TextFont font = cfg.ChatFonts().Resolve(ctype, cid);   //聊天窗打开时
 *      cfg.ChatFonts().SetOverride(ctype, cid, font);              //「AF」浮层改动后
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <atlstr.h>       // CString
#include <stdint.h>
#include <map>
#include <utility>
#include <vector>

#include "../net/MessageProtocolClient.h"   // net::TextFont

namespace chatfont {

/**
 *  把一个字体设置编码成配置文件里的一串 ASCII 文本。
 *    font：字体设置。未设置（m_valid 为 false）时编码为 "0"。
 *  返回：形如 "1,14,0,0,0,0,1710618" 的文本，依次为是否设置、字号（像素）、粗体、
 *        斜体、下划线、删除线、颜色（0xRRGGBB 的十进制）。
 */
CString EncodeFont(const net::TextFont& font);

/**
 *  把配置文件里的文本解码成字体设置。
 *    text：配置文本；"0" 表示未设置。
 *    out： [出参] 解码结果；返回 false 时不改动。字号夹在 8..72 像素之间，颜色只取
 *          低 24 位。
 *  返回：格式正确返回 true；空串、字段数不对、含非数字时返回 false。
 */
bool DecodeFont(LPCTSTR text, net::TextFont& out);

/**
 *  ChatFontPrefs —— 一个账号的聊天字体偏好（全局默认 + 各会话自定义）。
 *
 *  由 CUserConfig 持有：LoadConfig 时灌入，SaveConfig 时导出；各界面经
 *  CUserConfig::ChatFonts() 读写，改完由调用方负责存盘。
 */
class ChatFontPrefs
{
public:
    /**
     *  取全局默认字体；未设置时 m_valid 为 false。
     */
    const net::TextFont& GetDefault() const;

    /**
     *  设置全局默认字体。
     *    font：新的默认字体；传 m_valid 为 false 的字体即清除全局默认（恢复为不带字体）。
     */
    void SetDefault(const net::TextFont& font);

    /**
     *  某个会话是否设过自定义字体。
     *    ctype：会话类型，1 单聊、2 群聊。
     *    cid：  单聊为对端 userid，群聊为群ID。
     */
    bool HasOverride(int ctype, int64_t cid) const;

    /**
     *  取某个会话的自定义字体。
     *    ctype / cid：同 HasOverride。
     *    out：        [出参] 返回 true 时为该会话的自定义字体；返回 false 时不改动。
     *  返回：该会话设过自定义字体返回 true。
     */
    bool GetOverride(int ctype, int64_t cid, net::TextFont& out) const;

    /**
     *  设置某个会话的自定义字体。
     *    ctype / cid：同 HasOverride。
     *    font：       新的自定义字体；m_valid 为 false 时等同于 RemoveOverride。
     */
    void SetOverride(int ctype, int64_t cid, const net::TextFont& font);

    /**
     *  删除某个会话的自定义字体，该会话改回使用全局默认。
     *    ctype / cid：同 HasOverride。
     *  返回：原先设过、确实删掉了返回 true；原本就没有返回 false。
     */
    bool RemoveOverride(int ctype, int64_t cid);

    /**
     *  取某个会话实际生效的字体：有自定义用自定义，否则用全局默认；两者都没有时
     *  返回 m_valid 为 false 的字体（发出的消息不带字体）。
     *    ctype / cid：同 HasOverride。
     */
    net::TextFont Resolve(int ctype, int64_t cid) const;

    /**
     *  把全部会话自定义字体导出成"键 = 值"列表，供写入配置文件的 [ChatFontOverride] 段。
     *  键为"会话类型_会话ID"，值为 EncodeFont 的结果；按会话类型、会话ID 升序排列。
     *    out：[出参] 导出结果；调用前会被清空。
     */
    void ExportOverrides(std::vector<std::pair<CString, CString> >& out) const;

    /**
     *  用配置文件里读出的"键 = 值"列表整体替换会话自定义字体。键或值格式不对、或值
     *  是未设置（"0"）的行跳过，其余照常导入。
     *    entries：[ChatFontOverride] 段的全部键值。
     */
    void ImportOverrides(const std::vector<std::pair<CString, CString> >& entries);

private:
    // 全局默认字体；m_valid 为 false 表示未设置。
    net::TextFont m_default;

    // 会话自定义字体：键为 (会话类型, 会话ID)，值恒为已设置（m_valid 为 true）的字体。
    std::map<std::pair<int, int64_t>, net::TextFont> m_overrides;
};

} // namespace chatfont
