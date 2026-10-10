/**
 *  录屏时屏幕上实时字幕的显示规则，见 RecordCaptionLiveRules.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "RecordCaptionLiveRules.h"

#include "../../FlamingoCaption/Model/CaptionCue.h"   // CaptionSource

namespace flamingomedia {

namespace {

//每一路第一句的编号
const uint32_t kFirstSentenceId = 1;

} // namespace

RecordCaptionSentences::RecordCaptionSentences()
{
}

uint32_t RecordCaptionSentences::Take(int source, bool final)
{
    std::map<int, uint32_t>::iterator it = m_current.find(source);
    if (it == m_current.end())
        it = m_current.insert(std::make_pair(source, kFirstSentenceId)).first;
    const uint32_t id = it->second;
    //定稿之后这一路的下一条结果属于新的一句
    if (final)
        ++it->second;
    return id;
}

void RecordCaptionSentences::Reset()
{
    m_current.clear();
}

std::wstring RecordCaptionSpeaker(int source, bool micOn, bool systemOn, const std::wstring& selfName,
                                  const std::wstring& systemLabel)
{
    //只有一路在录：屏幕上不会同时出现两路的字幕，不标说话人
    if (!micOn || !systemOn)
        return std::wstring();
    if (source == caption::caption_source_mic)
        return selfName;
    if (source == caption::caption_source_system)
        return systemLabel;
    return std::wstring();
}

bool RecordCaptionHasSource(bool micOn, bool systemOn)
{
    return micOn || systemOn;
}

} // namespace flamingomedia
