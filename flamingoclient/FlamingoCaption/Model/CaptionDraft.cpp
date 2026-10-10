/**
 *  语音字幕的样式、草稿文件与 SRT 读写，实现见 CaptionDraft.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionDraft.h"

#include <windows.h>
#include <algorithm>

#include "CaptionJson.h"
#include "CaptionText.h"
#include "CaptionTime.h"

namespace caption {

namespace {

//默认外观（按 1080 高的画面计）：48 像素粗体、离底边 60 像素、6 像素描边、不画底框、显示说话人、白字黑边
const int kDefaultFontPx = 48;
const int kDefaultBottomMarginPx = 60;
const int kDefaultOutlinePx = 6;
const int kDefaultBoxAlpha = 0;
const uint32_t kDefaultTextColor = 0xFFFFFF;
const uint32_t kDefaultOutlineColor = 0x000000;
//不透明度的上限
const int kMaxAlpha = 255;
//草稿文件名的后缀与临时文件的后缀
const wchar_t kDraftSuffix[] = L".caption.json";
const wchar_t kTempSuffix[] = L".tmp";
//UTF-8 BOM
const char kUtf8Bom[] = "\xEF\xBB\xBF";
//SRT 时间行里起止时间之间的分隔
const char kSrtArrow[] = "-->";
//全角冒号：SRT 写出说话人前缀「说话人：文字」时用
const wchar_t kFullColon = 0xFF1A;
//读草稿时允许的最大文件字节数（64 MB），防止误读超大文件
const int64_t kMaxDraftBytes = 64LL * 1024 * 1024;

bool CueLess(const CaptionCue& a, const CaptionCue& b)
{
    if (a.m_startMs != b.m_startMs)
        return a.m_startMs < b.m_startMs;
    return a.m_id < b.m_id;
}

int ClampInt(int64_t v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return static_cast<int>(v);
}

double JsonDouble(const Json::Value& v, double fallback)
{
    if (v.isNumeric())
        return v.asDouble();
    return fallback;
}

bool ReadWholeFile(const std::wstring& path, std::string& out, std::string* error)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
    {
        if (error != NULL)
            *error = "open failed";
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > kMaxDraftBytes)
    {
        CloseHandle(f);
        if (error != NULL)
            *error = "size invalid";
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    const BOOL ok = size.QuadPart == 0 || ReadFile(f, &out[0], static_cast<DWORD>(size.QuadPart), &got, NULL);
    CloseHandle(f);
    if (!ok || got != static_cast<DWORD>(size.QuadPart))
    {
        if (error != NULL)
            *error = "read failed";
        return false;
    }
    return true;
}

//把 SRT 文本按行切开（去掉每行末尾的 \r）
std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == '\n')
        {
            std::string line = text.substr(start, i - start);
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            lines.push_back(line);
            start = i + 1;
        }
    }
    return lines;
}

bool IsBlank(const std::string& line)
{
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] != ' ' && line[i] != '\t')
            return false;
    }
    return true;
}

bool IsAllDigits(const std::string& line)
{
    size_t digits = 0;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] >= '0' && line[i] <= '9')
            ++digits;
        else if (line[i] != ' ' && line[i] != '\t')
            return false;
    }
    return digits > 0;
}

//解析时间行「A --> B [位置信息]」
bool ParseTimeLine(const std::string& line, int64_t* startMs, int64_t* endMs)
{
    const size_t arrow = line.find(kSrtArrow);
    if (arrow == std::string::npos)
        return false;
    std::string right = line.substr(arrow + sizeof(kSrtArrow) - 1);
    size_t p = 0;
    while (p < right.size() && (right[p] == ' ' || right[p] == '\t'))
        ++p;
    size_t q = p;
    while (q < right.size() && right[q] != ' ' && right[q] != '\t')
        ++q;
    return ParseSrtTime(line.substr(0, arrow), startMs) && ParseSrtTime(right.substr(p, q - p), endMs);
}

} // namespace

CaptionStyle::CaptionStyle()
    : m_fontPx(kDefaultFontPx)
    , m_bottomMarginPx(kDefaultBottomMarginPx)
    , m_outlinePx(kDefaultOutlinePx)
    , m_boxAlpha(kDefaultBoxAlpha)
    , m_showSpeaker(true)
    , m_textColor(kDefaultTextColor)
    , m_outlineColor(kDefaultOutlineColor)
{
}

CaptionPip::CaptionPip()
    : m_present(false)
    , m_left(0.0)
    , m_top(0.0)
    , m_right(0.0)
    , m_bottom(0.0)
{
}

CaptionExport::CaptionExport()
    : m_kind(caption_export_hard)
{
}

CaptionDraft::CaptionDraft()
    : m_version(kCaptionDraftVersion)
    , m_videoBytes(0)
    , m_videoDurationMs(0)
    , m_nextId(1)
{
}

std::string CaptionDraftIo::ToJson(const CaptionDraft& draft)
{
    Json::Value root(Json::objectValue);
    root["version"] = kCaptionDraftVersion;
    Json::Value video(Json::objectValue);
    video["name"] = WideToUtf8(draft.m_videoName);
    video["bytes"] = static_cast<Json::Int64>(draft.m_videoBytes);
    video["durationms"] = static_cast<Json::Int64>(draft.m_videoDurationMs);
    root["video"] = video;

    Json::Value style(Json::objectValue);
    style["fontpx"] = draft.m_style.m_fontPx;
    style["bottommarginpx"] = draft.m_style.m_bottomMarginPx;
    style["outlinepx"] = draft.m_style.m_outlinePx;
    style["boxalpha"] = draft.m_style.m_boxAlpha;
    style["showspeaker"] = draft.m_style.m_showSpeaker ? 1 : 0;
    style["textcolor"] = static_cast<Json::UInt>(draft.m_style.m_textColor);
    style["outlinecolor"] = static_cast<Json::UInt>(draft.m_style.m_outlineColor);
    root["style"] = style;

    Json::Value pip(Json::objectValue);
    pip["present"] = draft.m_pip.m_present ? 1 : 0;
    pip["left"] = draft.m_pip.m_left;
    pip["top"] = draft.m_pip.m_top;
    pip["right"] = draft.m_pip.m_right;
    pip["bottom"] = draft.m_pip.m_bottom;
    root["pip"] = pip;

    Json::Value cues(Json::arrayValue);
    for (size_t i = 0; i < draft.m_cues.size(); ++i)
    {
        const CaptionCue& c = draft.m_cues[i];
        Json::Value v(Json::objectValue);
        v["id"] = static_cast<Json::Int64>(c.m_id);
        v["start"] = static_cast<Json::Int64>(c.m_startMs);
        v["end"] = static_cast<Json::Int64>(c.m_endMs);
        v["speaker"] = WideToUtf8(c.m_speaker);
        v["text"] = WideToUtf8(c.m_text);
        v["source"] = c.m_source;
        v["edited"] = c.m_edited ? 1 : 0;
        cues.append(v);
    }
    root["cues"] = cues;
    root["nextid"] = static_cast<Json::Int64>(draft.m_nextId);
    Json::Value exports(Json::arrayValue);
    for (size_t i = 0; i < draft.m_exports.size(); ++i)
    {
        Json::Value e(Json::objectValue);
        e["name"] = WideToUtf8(draft.m_exports[i].m_name);
        e["kind"] = draft.m_exports[i].m_kind;
        exports.append(e);
    }
    root["exports"] = exports;
    return WriteJson(root, true);
}

bool CaptionDraftIo::FromJson(const std::string& json, CaptionDraft& draft, std::string* error)
{
    Json::Value root;
    if (!ParseJson(json, root) || !root.isObject())
    {
        if (error != NULL)
            *error = "not a json object";
        return false;
    }
    const int64_t version = JsonInt64(root["version"], 0);
    if (version > kCaptionDraftVersion)
    {
        if (error != NULL)
            *error = "draft version too new";
        return false;
    }
    CaptionDraft d;
    const Json::Value& video = root["video"];
    if (video.isObject())
    {
        d.m_videoName = Utf8ToWide(JsonString(video["name"]));
        d.m_videoBytes = JsonInt64(video["bytes"], 0);
        d.m_videoDurationMs = JsonInt64(video["durationms"], 0);
    }
    const Json::Value& style = root["style"];
    if (style.isObject())
    {
        d.m_style.m_fontPx = ClampInt(JsonInt64(style["fontpx"], kDefaultFontPx), 1, kCaptionStyleBaseHeight);
        d.m_style.m_bottomMarginPx = ClampInt(JsonInt64(style["bottommarginpx"], kDefaultBottomMarginPx), 0,
                                              kCaptionStyleBaseHeight);
        d.m_style.m_outlinePx = ClampInt(JsonInt64(style["outlinepx"], kDefaultOutlinePx), 0, kDefaultFontPx);
        d.m_style.m_boxAlpha = ClampInt(JsonInt64(style["boxalpha"], kDefaultBoxAlpha), 0, kMaxAlpha);
        d.m_style.m_showSpeaker = JsonInt64(style["showspeaker"], 1) != 0;
        d.m_style.m_textColor = static_cast<uint32_t>(JsonInt64(style["textcolor"], kDefaultTextColor));
        d.m_style.m_outlineColor = static_cast<uint32_t>(JsonInt64(style["outlinecolor"], kDefaultOutlineColor));
    }
    const Json::Value& pip = root["pip"];
    if (pip.isObject())
    {
        d.m_pip.m_present = JsonInt64(pip["present"], 0) != 0;
        d.m_pip.m_left = JsonDouble(pip["left"], 0.0);
        d.m_pip.m_top = JsonDouble(pip["top"], 0.0);
        d.m_pip.m_right = JsonDouble(pip["right"], 0.0);
        d.m_pip.m_bottom = JsonDouble(pip["bottom"], 0.0);
    }
    const Json::Value& cues = root["cues"];
    if (cues.isArray())
    {
        for (Json::ArrayIndex i = 0; i < cues.size(); ++i)
        {
            const Json::Value& v = cues[i];
            if (!v.isObject())
                continue;
            CaptionCue c;
            c.m_id = JsonInt64(v["id"], 0);
            c.m_startMs = JsonInt64(v["start"], -1);
            c.m_endMs = JsonInt64(v["end"], -1);
            c.m_speaker = Utf8ToWide(JsonString(v["speaker"]));
            c.m_text = Utf8ToWide(JsonString(v["text"]));
            c.m_source = ClampInt(JsonInt64(v["source"], caption_source_manual), 0, caption_source_count - 1);
            c.m_edited = JsonInt64(v["edited"], 0) != 0;
            if (c.m_startMs < 0 || c.m_endMs <= c.m_startMs)
                continue;
            d.m_cues.push_back(c);
        }
    }
    std::stable_sort(d.m_cues.begin(), d.m_cues.end(), CueLess);
    d.m_nextId = JsonInt64(root["nextid"], 1);
    const Json::Value& exports = root["exports"];
    if (exports.isArray())
    {
        for (Json::ArrayIndex i = 0; i < exports.size(); ++i)
        {
            const Json::Value& e = exports[i];
            if (!e.isObject())
                continue;
            CaptionExport x;
            x.m_name = Utf8ToWide(JsonString(e["name"]));
            x.m_kind = static_cast<int>(JsonInt64(e["kind"], 0));
            if (!x.m_name.empty() && x.m_kind >= caption_export_hard && x.m_kind <= caption_export_text)
                d.m_exports.push_back(x);
        }
    }
    draft = d;
    return true;
}

std::wstring CaptionDraftIo::DraftPathFor(const std::wstring& videoPath)
{
    return videoPath + kDraftSuffix;
}

bool CaptionDraftIo::SaveFile(const std::wstring& path, const CaptionDraft& draft)
{
    const std::string json = ToJson(draft);
    const std::wstring temp = path + kTempSuffix;
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &written, NULL) && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || written != json.size())
    {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool CaptionDraftIo::LoadFile(const std::wstring& path, CaptionDraft& draft, std::string* error)
{
    std::string text;
    if (!ReadWholeFile(path, text, error))
        return false;
    return FromJson(text, draft, error);
}

std::string CaptionSrt::Write(const std::vector<CaptionCue>& cues, bool withSpeaker)
{
    std::vector<CaptionCue> sorted = cues;
    std::stable_sort(sorted.begin(), sorted.end(), CueLess);
    std::string out = kUtf8Bom;
    int index = 0;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const CaptionCue& c = sorted[i];
        std::wstring text = c.m_text;
        if (withSpeaker && !c.m_speaker.empty())
            text = c.m_speaker + kFullColon + text;
        if (text.empty())
            continue;
        std::string body = WideToUtf8(text);
        std::string crlf;
        for (size_t k = 0; k < body.size(); ++k)
        {
            if (body[k] == '\n')
                crlf += "\r\n";
            else if (body[k] != '\r')
                crlf.push_back(body[k]);
        }
        ++index;
        out += std::to_string(static_cast<long long>(index));
        out += "\r\n";
        out += FormatSrtTime(c.m_startMs) + " --> " + FormatSrtTime(c.m_endMs) + "\r\n";
        out += crlf + "\r\n\r\n";
    }
    return out;
}

std::string CaptionPlainText::Write(const std::vector<CaptionCue>& cues, bool withSpeaker)
{
    std::vector<CaptionCue> sorted = cues;
    std::stable_sort(sorted.begin(), sorted.end(), CueLess);
    std::string out = kUtf8Bom;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const CaptionCue& c = sorted[i];
        if (c.m_text.empty())
            continue;
        std::wstring text = c.m_text;
        //两行字幕在纯文本里接成一行
        for (size_t k = 0; k < text.size(); ++k)
        {
            if (text[k] == L'\r' || text[k] == L'\n')
                text[k] = L' ';
        }
        if (withSpeaker && !c.m_speaker.empty())
            text = c.m_speaker + kFullColon + text;
        //时间取 SRT 时间「HH:MM:SS,mmm」逗号前的部分
        std::string clock = FormatSrtTime(c.m_startMs);
        const size_t comma = clock.find(',');
        if (comma != std::string::npos)
            clock.erase(comma);
        out += "[" + clock + "] " + WideToUtf8(text) + "\r\n";
    }
    return out;
}

bool CaptionSrt::Parse(const std::string& utf8, std::vector<CaptionCue>& out, int* skipped)
{
    std::string text = utf8;
    if (text.compare(0, sizeof(kUtf8Bom) - 1, kUtf8Bom) == 0)
        text.erase(0, sizeof(kUtf8Bom) - 1);
    const std::vector<std::string> lines = SplitLines(text);
    int bad = 0;
    size_t i = 0;
    out.clear();
    while (i < lines.size())
    {
        while (i < lines.size() && IsBlank(lines[i]))
            ++i;
        if (i >= lines.size())
            break;
        //一个段落：可选的序号行、时间行、若干文字行，以空行结束
        size_t blockEnd = i;
        while (blockEnd < lines.size() && !IsBlank(lines[blockEnd]))
            ++blockEnd;
        size_t k = i;
        if (IsAllDigits(lines[k]) && k + 1 < blockEnd)
            ++k;
        int64_t startMs = 0;
        int64_t endMs = 0;
        if (!ParseTimeLine(lines[k], &startMs, &endMs) || endMs <= startMs)
        {
            ++bad;
            i = blockEnd;
            continue;
        }
        std::string body;
        for (size_t t = k + 1; t < blockEnd; ++t)
        {
            if (!body.empty())
                body += "\n";
            body += lines[t];
        }
        const std::wstring cueText = NormalizeText(Utf8ToWide(body));
        if (cueText.empty())
        {
            ++bad;
            i = blockEnd;
            continue;
        }
        CaptionCue c;
        c.m_startMs = startMs;
        c.m_endMs = endMs;
        c.m_text = cueText;
        c.m_source = caption_source_manual;
        out.push_back(c);
        i = blockEnd;
    }
    std::stable_sort(out.begin(), out.end(), CueLess);
    if (skipped != NULL)
        *skipped = bad;
    return !out.empty();
}

} // namespace caption
