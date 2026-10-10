/**
 *  按账号保存的字幕设置（docs/语音字幕方案.md 第 2 节 D1～D4、5.2），CaptionSettings.h
 *
 *  设置以一段 JSON 文本存在 exe 的账号配置里：exe 登录后经 FlamingoMedia 推给本 DLL（ICaptionModule::SetSettings），
 *  本 DLL 修改后经 ICaptionHost::SaveSettings 交回。FlamingoMedia 也读写其中与录制、共享有关的几项，
 *  所以序列化时保留不认识的键，避免两边各改各的时互相覆盖。
 *
 *  JSON 格式：
 *      {"replace":[...替换表...],
 *       "record":{"mic":1,"system":0},
 *       "export":{"hard":1,"track":0,"srt":0},
 *       "live":{"self":0,"show":1}}
 *
 *  典型用法：
 *      caption::CaptionSettings s;
 *      s.FromJson(text);
 *      s.m_exportTrack = true;
 *      std::string saved = s.ToJson();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "CaptionSegmenter.h"

namespace caption {

//字幕设置
class CaptionSettings
{
public:
    CaptionSettings();

    /**
     *  从 JSON 恢复。缺少的项取默认值；文本为空或不是 JSON 对象时全部取默认值。
     *  @return 文本是合法的 JSON 对象（或为空）返回 true
     */
    bool FromJson(const std::string& json);

    //序列化为 JSON（UTF-8），保留 FromJson 时读到的、本类不认识的键
    std::string ToJson() const;

    CaptionReplaceTable m_replace;        //自动替换表
    bool                m_recordMic;      //录制选项「生成字幕」默认勾选（识别本人麦克风），默认是
    bool                m_recordSystem;   //原录制选项「为电脑声音生成字幕」的默认勾选，默认否；2026-10-06 起录屏不再使用，字段保留
    bool                m_exportHard;     //导出窗口「硬字幕版」默认勾选，默认是
    bool                m_exportTrack;    //导出窗口「字幕轨版」默认勾选，默认否
    bool                m_exportSrt;      //导出窗口「SRT」默认勾选，默认否
    bool                m_exportText;     //导出窗口「纯文本」默认勾选（14.13），默认否
    bool                m_liveSelf;       //屏幕共享「为我的发言生成字幕」默认打开，默认否
    bool                m_liveShow;       //屏幕共享观看窗默认显示别人的字幕，默认是

private:
    std::string m_raw;   //FromJson 读到的原文，ToJson 时以它为底，保留不认识的键
};

} // namespace caption
