/**
 *  录制管理窗口纯逻辑的单元测试（标签 [recordmgr]），RecordManagerModelTests.cpp
 *
 *  覆盖 FlamingoMedia/Record/RecordManagerModel.h（docs/屏幕共享与录制方案.md 第 11.6 节 F9）：
 *    RMG1 副文字格式：「2026-10-05 13:18 · 01:23 · 15.6 MB」；时长不满一小时写分:秒、否则写时:分:秒；大小按 B / KB / MB / GB；
 *    RMG2 文件已不存在的行副文字为「文件已不存在」、只能从列表移除；文件在的行可播放、打开所在文件夹、发送、重命名、删除；
 *    RMG3 重命名校验：保留原扩展名、去掉首尾空白、空名或非法字符或系统保留名拒绝、与原名相同视为没改、目标已存在拒绝；
 *    RMG4 录制完成时交给宿主的记录：字段齐全，路径为空或过长时不生成；
 *    RMG5 原地编辑框的位置：按行号、行高、滚动位置算出，行滚出可见范围时不显示；
 *    CAPRM1 语音字幕（docs/语音字幕方案.md 第 2 节第 9 条）：字幕可用且文件在时多一项「编辑字幕」；字幕状态文字；草稿文件路径。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <cwctype>
#include <string>
#include "../../FlamingoMedia/Record/RecordManagerModel.h"

using namespace screenrecord;

namespace {

//1 KB、1 MB、1 GB 的字节数
const uint64_t kRmgKb = 1024ULL;
const uint64_t kRmgMb = 1024ULL * 1024ULL;
const uint64_t kRmgGb = 1024ULL * 1024ULL * 1024ULL;

//本地时间 2026-10-05 13:18
SYSTEMTIME RmgLocalTime()
{
    SYSTEMTIME t;
    ::ZeroMemory(&t, sizeof(t));
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 5;
    t.wHour = 13;
    t.wMinute = 18;
    t.wSecond = 7;
    return t;
}

//小写化后比较，模拟 Windows 文件名不区分大小写
std::wstring RmgLower(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (wchar_t)::towlower(out[i]);
    return out;
}

//假的「文件是否存在」：只有 D:\Videos\Meeting.mp4 与 D:\Videos\existing.mp4 存在
bool RmgFakeExists(const std::wstring& path)
{
    const std::wstring p = RmgLower(path);
    return p == L"d:\\videos\\meeting.mp4" || p == L"d:\\videos\\existing.mp4";
}

} // namespace

TEST_CASE("RMG1 副文字：录制结束时间 · 时长 · 大小", "[recordmgr]")
{
    CHECK(FormatRecordSize(0) == L"0 B");
    CHECK(FormatRecordSize(1023) == L"1023 B");
    CHECK(FormatRecordSize(kRmgKb) == L"1.0 KB");
    CHECK(FormatRecordSize(kRmgKb + kRmgKb / 2) == L"1.5 KB");
    //15.6 MB：15 MB + 0.6 MB（0.6 × 1048576 = 629145.6，取 629146）
    CHECK(FormatRecordSize(15 * kRmgMb + 629146) == L"15.6 MB");
    CHECK(FormatRecordSize(2 * kRmgGb + kRmgGb / 2) == L"2.5 GB");

    const SYSTEMTIME t = RmgLocalTime();
    CHECK(RecordRowSubText(true, t, 83000, 15 * kRmgMb + 629146) == L"2026-10-05 13:18 · 01:23 · 15.6 MB");
    //超过一小时：时:分:秒
    CHECK(RecordRowSubText(true, t, 3723000, 512) == L"2026-10-05 13:18 · 1:02:03 · 512 B");

    //UTC 毫秒换本地时间：UTC 2026-10-05 04:00 在任何时区都落在 10 月 4 日或 5 日
    SYSTEMTIME local;
    REQUIRE(RecordUtcMsToLocal(1791172800000LL, local));
    CHECK(local.wYear == 2026);
    CHECK(local.wMonth == 10);
    CHECK((local.wDay == 4 || local.wDay == 5));
    CHECK_FALSE(RecordUtcMsToLocal(-1, local));
}

TEST_CASE("RMG2 文件已不存在的行只能从列表移除，文件在的行五个操作都可用", "[recordmgr]")
{
    const int present = RecordRowActions(true);
    CHECK(present == (record_action_play | record_action_open_folder | record_action_send | record_action_rename |
                      record_action_delete));
    CHECK((present & record_action_remove) == 0);

    CHECK(RecordRowActions(false) == record_action_remove);
    CHECK(RecordRowSubText(false, RmgLocalTime(), 83000, kRmgMb) == L"文件已不存在");
}

TEST_CASE("RMG3 重命名校验", "[recordmgr]")
{
    const std::wstring oldPath = L"D:\\Videos\\20261005_131807.mp4";
    std::wstring newPath = L"untouched";

    //不写扩展名：补上原扩展名；首尾空白去掉
    REQUIRE(ValidateRecordRename(oldPath, L"周会录像", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\周会录像.mp4");
    REQUIRE(ValidateRecordRename(oldPath, L"  周会录像  ", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\周会录像.mp4");
    //写了原扩展名（大小写不同）：按原扩展名的写法
    REQUIRE(ValidateRecordRename(oldPath, L"周会录像.MP4", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\周会录像.mp4");
    //写了别的扩展名：算作文件名的一部分
    REQUIRE(ValidateRecordRename(oldPath, L"notes.txt", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\notes.txt.mp4");
    //exists 为 NULL：视为目标不存在
    REQUIRE(ValidateRecordRename(oldPath, L"existing", NULL, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\existing.mp4");

    //失败时出参不变
    newPath = L"untouched";
    CHECK(ValidateRecordRename(oldPath, L"", &RmgFakeExists, newPath) == record_rename_empty);
    CHECK(ValidateRecordRename(oldPath, L"   ", &RmgFakeExists, newPath) == record_rename_empty);
    CHECK(ValidateRecordRename(oldPath, L".mp4", &RmgFakeExists, newPath) == record_rename_empty);
    const wchar_t* const badNames[] = { L"a/b", L"a\\b", L"a:b", L"a*b", L"a?b", L"a\"b", L"a<b", L"a>b", L"a|b",
                                        L"a\tb" };
    for (size_t i = 0; i < sizeof(badNames) / sizeof(badNames[0]); ++i)
    {
        INFO("非法字符用例 " << i);
        CHECK(ValidateRecordRename(oldPath, badNames[i], &RmgFakeExists, newPath) == record_rename_invalid);
    }
    //系统保留名（不区分大小写）不能用；以保留名开头的普通名字可以
    CHECK(ValidateRecordRename(oldPath, L"CON", &RmgFakeExists, newPath) == record_rename_invalid);
    CHECK(ValidateRecordRename(oldPath, L"nul", &RmgFakeExists, newPath) == record_rename_invalid);
    CHECK(ValidateRecordRename(oldPath, L"com1", &RmgFakeExists, newPath) == record_rename_invalid);
    CHECK(ValidateRecordRename(oldPath, L"LPT9", &RmgFakeExists, newPath) == record_rename_invalid);
    CHECK(newPath == L"untouched");
    CHECK(ValidateRecordRename(oldPath, L"console", &RmgFakeExists, newPath) == record_rename_ok);

    //与原名相同：不需要改
    newPath = L"untouched";
    CHECK(ValidateRecordRename(oldPath, L"20261005_131807", &RmgFakeExists, newPath) == record_rename_unchanged);
    CHECK(ValidateRecordRename(oldPath, L"20261005_131807.mp4", &RmgFakeExists, newPath) == record_rename_unchanged);
    CHECK(newPath == L"untouched");

    //目标已存在：拒绝
    CHECK(ValidateRecordRename(oldPath, L"existing", &RmgFakeExists, newPath) == record_rename_exists);
    CHECK(newPath == L"untouched");

    //只改大小写：可以改名，不查目标是否存在（它就是原文件自己）
    REQUIRE(ValidateRecordRename(L"D:\\Videos\\Meeting.mp4", L"meeting", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\Videos\\meeting.mp4");

    //新路径太长
    CHECK(ValidateRecordRename(oldPath, std::wstring(1100, L'a'), &RmgFakeExists, newPath) == record_rename_too_long);

    //原文件没有扩展名：新名字也不加扩展名；目录名里的点不算扩展名
    REQUIRE(ValidateRecordRename(L"D:\\v.1\\clip", L"new", &RmgFakeExists, newPath) == record_rename_ok);
    CHECK(newPath == L"D:\\v.1\\new");

    //预填文字：文件名去掉扩展名
    CHECK(RecordRenameEditText(L"D:\\Videos\\周会录像.mp4") == L"周会录像");
    CHECK(RecordRenameEditText(L"D:\\v.1\\clip") == L"clip");
}

TEST_CASE("RMG4 录制完成时交给宿主的记录", "[recordmgr]")
{
    flamingomedia::MediaRecordingInfo info;
    ::memset(&info, 0xCC, sizeof(info));
    REQUIRE(MakeRecordingInfo(L"D:\\Videos\\20261005_131807.mp4", 83000, 15 * kRmgMb, 1791172800000LL, info));
    CHECK(info.m_cbSize == sizeof(flamingomedia::MediaRecordingInfo));
    CHECK(info.m_id == 0);
    CHECK(info.m_durationMs == 83000);
    CHECK(info.m_sizeBytes == (int64_t)(15 * kRmgMb));
    CHECK(info.m_endTimeMs == 1791172800000LL);
    CHECK(std::wstring(info.m_path) == L"D:\\Videos\\20261005_131807.mp4");

    //路径为空、长度达到容量：不生成
    CHECK_FALSE(MakeRecordingInfo(L"", 1, 1, 1, info));
    CHECK_FALSE(MakeRecordingInfo(std::wstring(flamingomedia::kMediaRecordingPathChars, L'a'), 1, 1, 1, info));
    //容量减一（留出结尾 L'\0'）可以
    CHECK(MakeRecordingInfo(std::wstring(flamingomedia::kMediaRecordingPathChars - 1, L'a'), 1, 1, 1, info));

    //录制结束时间由 FILETIME 换算：UTC 2026-10-05 04:00 的 FILETIME 换回同一个 Unix 毫秒；早于 1970 年时为 0
    SYSTEMTIME utc;
    ::ZeroMemory(&utc, sizeof(utc));
    utc.wYear = 2026;
    utc.wMonth = 10;
    utc.wDay = 5;
    utc.wHour = 4;
    FILETIME ft;
    REQUIRE(::SystemTimeToFileTime(&utc, &ft));
    CHECK(RecordFileTimeToUnixMs(ft) == 1791172800000LL);
    FILETIME zero = { 0, 0 };
    CHECK(RecordFileTimeToUnixMs(zero) == 0);
}

TEST_CASE("RMG5 原地编辑框的位置", "[recordmgr]")
{
    //列表矩形：左 10、上 50、右 410、下 350（高 300），行高 30，文字起点 12，右边让出 11
    RECT list = { 10, 50, 410, 350 };
    RECT rc = { 0, 0, 0, 0 };
    REQUIRE(RecordInlineEditRect(list, 0, 30, 0, 12, 11, rc));
    CHECK(rc.left == 22);
    CHECK(rc.top == 50);
    CHECK(rc.right == 399);
    CHECK(rc.bottom == 80);
    //最后一行正好贴着底边：可见
    REQUIRE(RecordInlineEditRect(list, 9, 30, 0, 12, 11, rc));
    CHECK(rc.top == 320);
    CHECK(rc.bottom == 350);
    //下一行在可见范围之外
    CHECK_FALSE(RecordInlineEditRect(list, 10, 30, 0, 12, 11, rc));
    //滚动 15 像素：第 0 行只露出一半，不显示；第 1 行在 65~95
    CHECK_FALSE(RecordInlineEditRect(list, 0, 30, 15, 12, 11, rc));
    REQUIRE(RecordInlineEditRect(list, 1, 30, 15, 12, 11, rc));
    CHECK(rc.top == 65);
    CHECK(rc.bottom == 95);
    //参数非法：行高不大于 0、行号小于 0、文字区域没有宽度
    CHECK_FALSE(RecordInlineEditRect(list, 0, 0, 0, 12, 11, rc));
    CHECK_FALSE(RecordInlineEditRect(list, -1, 30, 0, 12, 11, rc));
    CHECK_FALSE(RecordInlineEditRect(list, 0, 30, 0, 200, 200, rc));
}

TEST_CASE("CAPRM1 语音字幕：可用且文件在时多一项编辑字幕；字幕状态文字；草稿文件路径", "[recordmgr]")
{
    //字幕可用：文件在的行多一项，文件不在的行仍只能从列表移除
    CHECK((RecordRowActions(true, true) & record_action_caption) != 0);
    CHECK((RecordRowActions(true, false) & record_action_caption) == 0);
    CHECK(RecordRowActions(false, true) == record_action_remove);
    CHECK(RecordRowActions(true) == RecordRowActions(true, false));

    //字幕状态：没有字幕文件时为空；只有草稿时为「有字幕」；导出过时按固定顺序列出导出过的几种
    CHECK(RecordCaptionStatusText(caption::caption_file_none).empty());
    CHECK(RecordCaptionStatusText(caption::caption_file_draft) == L"有字幕");
    CHECK(RecordCaptionStatusText(caption::caption_file_draft | caption::caption_file_hard) == L"已导出字幕：硬字幕版");
    CHECK(RecordCaptionStatusText(caption::caption_file_draft | caption::caption_file_srt | caption::caption_file_hard |
                                  caption::caption_file_track) == L"已导出字幕：硬字幕版、字幕轨版、SRT");
    CHECK(RecordCaptionStatusText(caption::caption_file_track) == L"已导出字幕：字幕轨版");

    //草稿文件路径：与 FlamingoCaption 的命名规则一致
    CHECK(RecordCaptionDraftPath(L"C:\\v\\20261006_120000.mp4") == L"C:\\v\\20261006_120000.mp4.caption.json");
    CHECK(RecordCaptionDraftPath(L"").empty());
}
