/**
 *  MarkdownEditorHost.h
 *
 *  Markdown 编辑器与登录后的客户端之间的接口（docs/Markdown消息方案.md 第 11.3 节）：编辑器不依赖登录状态，
 *  发送相关的能力都经这个接口取得。登录后由主面板实现并通过 CMarkdownEditorManager::SetHost 交给管理器，登出时
 *  传 NULL；没有宿主时编辑器按「未登录」处理，底部显示「登录后可发送」。
 *
 *  典型用法（主面板）：
 *      class CBalloonMainPanel : public balloonmd::IMarkdownEditorHost { ... };
 *      balloonmd::CMarkdownEditorManager::GetInstance().SetHost(this);    //登录完成
 *      balloonmd::CMarkdownEditorManager::GetInstance().SetHost(NULL);    //登出、主面板销毁前
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "MarkdownEditorManager.h"   // MarkdownSendTarget

namespace balloonmd {

/**
 *  发送对象选择结果的接收方，由编辑器窗口实现。
 */
class IMarkdownTargetSink
{
public:
    virtual ~IMarkdownTargetSink()
    {
    }

    /**
     *  用户在选择窗里点了「确定」。
     *  @param targets 选定的对象，按勾选先后排列，至少一个；调用期间有效，接收方需要时自行复制
     */
    virtual void OnMarkdownTargetsPicked(const std::vector<MarkdownSendTarget>& targets) = 0;
};

/**
 *  编辑器的宿主接口，登录后由主面板实现。全部方法只在界面线程上调用。
 */
class IMarkdownEditorHost
{
public:
    virtual ~IMarkdownEditorHost()
    {
    }

    //当前是否已登录且连接可用于发送
    virtual bool IsMarkdownSendReady() const = 0;

    /**
     *  打开发送对象选择窗（多选单聊、群聊）。选择窗是非模态的，结果经 sink 异步交回。
     *  @param title  文档标题，显示在选择窗右侧的预览区
     *  @param digest 文档摘要（纯文字），显示在标题下面
     *  @param sink   结果接收方，借用；接收方销毁前须调用 CancelMarkdownTargetPicker
     *  @return 打开了选择窗返回 true；未登录等原因无法打开返回 false
     */
    virtual bool OpenMarkdownTargetPicker(const std::wstring& title, const std::wstring& digest,
                                          IMarkdownTargetSink* sink) = 0;

    /**
     *  接收方即将销毁：如果选择窗正在为它打开着，关掉选择窗并不再回调它。
     *  @param sink 接收方
     */
    virtual void CancelMarkdownTargetPicker(IMarkdownTargetSink* sink) = 0;

    /**
     *  把一篇 Markdown 文档作为 content.format = "markdown" 的文字消息逐个发给 targets。
     *  @param sourceUtf8 源文本（UTF-8），非空且不超过文字消息上限（调用方已检查）
     *  @param targets    发送对象，至少一个
     *  @return 实际发出的条数（未登录时为 0）
     */
    virtual int SendMarkdownDocument(const std::string& sourceUtf8, const std::vector<MarkdownSendTarget>& targets) = 0;
};

} // namespace balloonmd
