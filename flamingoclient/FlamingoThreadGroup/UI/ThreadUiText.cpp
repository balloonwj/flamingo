/**
 *  帖子群窗口里拼出来的界面文字，见 ThreadUiText.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadUiText.h"

#include "../Core/ThreadComposeModel.h"   // ComposeSubmitCheck
#include "../Core/ThreadRules.h"          // ComposeCheck / ComposeKind / TextLimitOf
#include "../Core/ThreadTime.h"
#include "../Core/ThreadTypes.h"          // ThreadError 与各项上限
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找

namespace threadgroup {

namespace {

//时、分、月、日补成两位数用的宽度与填充字符
const int kTwoDigits = 2;
const wchar_t kZero = L'0';

//各分档的文字模板（文件作用域只做标记，使用处再调 TRF）
const wchar_t* const kJustNow = TR_NOOP(L"刚刚");
const wchar_t* const kMinutesAgo = TR_NOOP(L"{0} 分钟前");
const wchar_t* const kToday = TR_NOOP(L"今天 {0}:{1}");
const wchar_t* const kYesterday = TR_NOOP(L"昨天 {0}:{1}");
const wchar_t* const kThisYear = TR_NOOP(L"{0}-{1} {2}:{3}");
const wchar_t* const kOlder = TR_NOOP(L"{0}-{1}-{2}");
const wchar_t* const kCommentCount = TR_NOOP(L"{0} 条留言");
const wchar_t* const kLikeChip = TR_NOOP(L"赞 {0}");
const wchar_t* const kLikeOnly = TR_NOOP(L"赞");

//操作失败的说明（FormatActionError）
const wchar_t* const kErrNotSent = TR_NOOP(L"没有发出去，请检查网络后重试");
const wchar_t* const kErrTimeout = TR_NOOP(L"操作没有完成，请检查网络后重试");
const wchar_t* const kErrMuted = TR_NOOP(L"你目前不能在本群发言");
const wchar_t* const kErrSensitive = TR_NOOP(L"内容包含敏感词，请修改后再发");
const wchar_t* const kErrMustRead = TR_NOOP(L"请先阅读并确认入群必读");
const wchar_t* const kErrNotMember = TR_NOOP(L"你已不在这个群里");
const wchar_t* const kErrDisabled = TR_NOOP(L"本企业已关闭帖子群");
const wchar_t* const kErrNotExist = TR_NOOP(L"内容已被删除");
const wchar_t* const kErrNoPermission = TR_NOOP(L"没有权限这样做");
const wchar_t* const kErrInvalid = TR_NOOP(L"内容不符合要求");
const wchar_t* const kErrPinFull = TR_NOOP(L"最多只能置顶 3 条帖子");
const wchar_t* const kErrAddendumFull = TR_NOOP(L"这个帖子的补充已经满 20 段了");
const wchar_t* const kErrOther = TR_NOOP(L"操作失败（错误码 {0}）");

//本地校验不通过的说明（FormatComposeCheck）
const wchar_t* const kErrEmpty = TR_NOOP(L"请先写点什么");
const wchar_t* const kErrEmptyWithImages = TR_NOOP(L"请先写点什么，或者加一张图片");
const wchar_t* const kErrTitleTooLong = TR_NOOP(L"标题最多 {0} 个字");
const wchar_t* const kErrPostTooLong = TR_NOOP(L"正文最多 {0} 个字");
const wchar_t* const kErrAddendumTooLong = TR_NOOP(L"补充最多 {0} 个字");
const wchar_t* const kErrTooLong = TR_NOOP(L"留言最多 {0} 个字");
const wchar_t* const kErrTooManyImages = TR_NOOP(L"最多 {0} 张图片");
const wchar_t* const kErrTooManyMentions = TR_NOOP(L"一次最多 @ {0} 个人");
const wchar_t* const kErrUploading = TR_NOOP(L"图片还在上传，请稍候");
const wchar_t* const kErrUploadFailed = TR_NOOP(L"有图片没有上传成功，请点击它重试或删除");

//补成两位数，如 7 → "07"
std::wstring TwoDigits(int value)
{
    std::wstring text = std::to_wstring(static_cast<long long>(value));
    if (static_cast<int>(text.size()) < kTwoDigits)
        text.insert(text.begin(), kTwoDigits - text.size(), kZero);
    return text;
}

} // namespace

std::wstring FormatPostTime(int64_t timeMs)
{
    if (timeMs <= 0)
        return std::wstring();
    const TimeParts t = ClassifyTime(timeMs, NowMs(), LocalOffsetMs());
    switch (t.m_kind)
    {
    //1 分钟以内（含本机时钟比服务端慢时的将来时间）
    case time_just_now:
        return TR(kJustNow);

    //1 小时以内
    case time_minutes_ago:
        return TRF(kMinutesAgo, t.m_minutes);

    //今天更早
    case time_today:
        return TRF(kToday, TwoDigits(t.m_hour), TwoDigits(t.m_minute));

    //昨天
    case time_yesterday:
        return TRF(kYesterday, TwoDigits(t.m_hour), TwoDigits(t.m_minute));

    //今年更早
    case time_this_year:
        return TRF(kThisYear, TwoDigits(t.m_month), TwoDigits(t.m_day), TwoDigits(t.m_hour), TwoDigits(t.m_minute));

    //往年
    default:
        return TRF(kOlder, t.m_year, TwoDigits(t.m_month), TwoDigits(t.m_day));
    }
}

std::wstring FormatCommentCount(int count)
{
    if (count <= 0)
        return std::wstring();
    return TRF(kCommentCount, count);
}

std::wstring FormatLikeChip(int count)
{
    //没人点过时只写「赞」
    if (count <= 0)
        return TR(kLikeOnly);
    return TRF(kLikeChip, count);
}

std::wstring FormatActionError(int code)
{
    switch (code)
    {
    //没有发出去或断线
    case thread_error_timeout:
        return TR(kErrTimeout);

    //被禁言，或仅通知模式下不是管理员
    case thread_error_member_muted:
        return TR(kErrMuted);

    //包含敏感词
    case thread_error_sensitive_word:
        return TR(kErrSensitive);

    //还没有确认入群必读
    case thread_error_must_read_pending:
        return TR(kErrMustRead);

    //已不是群成员（退群、被移出、群被解散）
    case thread_error_not_group_member:
    case thread_error_group_not_exist:
        return TR(kErrNotMember);

    //本企业关闭了帖子群
    case thread_error_disabled:
        return TR(kErrDisabled);

    //帖子、补充、留言或回复已被删除
    case thread_error_not_exist:
        return TR(kErrNotExist);

    //没有权限
    case thread_error_no_permission:
        return TR(kErrNoPermission);

    //字段不合法
    case thread_error_invalid:
        return TR(kErrInvalid);

    //置顶已满
    case thread_error_pin_full:
        return TR(kErrPinFull);

    //补充已满
    case thread_error_addendum_full:
        return TR(kErrAddendumFull);

    //其它错误：带上错误码，方便排查
    default:
        return TRF(kErrOther, code);
    }
}

std::wstring FormatComposeCheck(int check, int kind)
{
    switch (check)
    {
    //合法：没有说明
    case compose_ok:
        return std::wstring();

    //没有内容：帖子与补充可以只发图片，提示里一并说明
    case compose_empty:
        return (kind == compose_comment) ? std::wstring(TR(kErrEmpty)) : std::wstring(TR(kErrEmptyWithImages));

    //标题太长
    case compose_title_too_long:
        return TRF(kErrTitleTooLong, kTitleMaxChars);

    //正文太长：上限随种类不同
    case compose_text_too_long:
        if (kind == compose_post)
            return TRF(kErrPostTooLong, TextLimitOf(kind));
        if (kind == compose_addendum)
            return TRF(kErrAddendumTooLong, TextLimitOf(kind));
        return TRF(kErrTooLong, TextLimitOf(kind));

    //图片太多
    case compose_too_many_images:
        return TRF(kErrTooManyImages, kMaxImages);

    //@ 的人太多
    case compose_too_many_mentions:
        return TRF(kErrTooManyMentions, kMaxMentions);

    //还有图片在上传
    case compose_images_uploading:
        return TR(kErrUploading);

    //有图片上传失败
    case compose_images_failed:
        return TR(kErrUploadFailed);

    //其余（该种类不允许的内容等，界面上不会出现）
    default:
        return TR(kErrInvalid);
    }
}

std::wstring NotSentText()
{
    return TR(kErrNotSent);
}

} // namespace threadgroup
