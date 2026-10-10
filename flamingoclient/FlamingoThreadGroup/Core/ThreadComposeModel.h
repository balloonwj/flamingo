/**
 *  发帖窗口的内容模型（纯逻辑，不含界面，docs/帖子群方案.md 4.2、4.3、4.6）：发帖、编辑帖子与追加补充三种模式共用。
 *  管理图片列表（每张图的上传状态与本机路径）和 @ 的人，提交前给出能否提交的判定，关闭前判断内容有没有改动。
 *  界面只负责把输入框里的文字与按钮操作交给它，再按它的状态重建图片行与名单行。
 *
 *  典型用法：
 *      CThreadComposeModel model(compose_post);
 *      model.AddUploading(requestId, path);                 //选了一张图，同时交给宿主上传
 *      model.OnUploaded(requestId, ok, image, cachePath);   //宿主回报上传结果
 *      if (model.CheckSubmit(title, wire) == compose_ok)
 *          session->PublishPost(title, wire, model.UploadedImages(), model.Mentions());
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ThreadTypes.h"

namespace threadgroup {

//发帖窗口里一张图片的状态
enum ComposeImageState
{
    compose_image_uploading = 1,   //刚选的本机图片，正在上传
    compose_image_done      = 2,   //已在服务端：上传成功，或编辑帖子时带入的原有图片
    compose_image_failed    = 3,   //上传失败，可以点击重试或删除
};

//提交判定在 ComposeCheck（ThreadRules.h）之外的两种结果
enum ComposeSubmitCheck
{
    compose_images_uploading = 101,   //还有图片在上传，需等上传完成
    compose_images_failed    = 102,   //有图片上传失败，需重试或删除后才能提交
};

//发帖窗口里的一张图片
struct ComposeImage
{
    uint32_t     m_requestId;   //尚未完成的请求号：上传中为上传请求，原有图片的缩略图未到时为下载请求；没有时为 0
    std::wstring m_localPath;   //本机路径，画缩略图用；原有图片下载完成前为空
    int          m_state;       //状态，取值见 ComposeImageState
    ThreadImage  m_image;       //服务端的图片信息，状态为 compose_image_done 时有效

    ComposeImage()
        : m_requestId(0)
        , m_state(compose_image_uploading)
    {
    }
};

/**
 *  发帖窗口的内容模型。一个发帖窗口持有一个，生命周期与窗口相同。
 *  文字（标题与正文）不存在模型里：始终以输入框为准，提交与判断改动时由界面传入。
 */
class CThreadComposeModel
{
public:
    /**
     *  @param kind 内容种类：compose_post（发帖与编辑帖子）或 compose_addendum（追加补充），取值见 ComposeKind
     */
    explicit CThreadComposeModel(int kind);

    //内容种类，取值见 ComposeKind
    int Kind() const;

    //---- 图片 ----

    //当前图片张数（含上传中与上传失败的）
    int ImageCount() const;

    //还能再加几张图（0~kMaxImages）
    int RoomForImages() const;

    /**
     *  加一张刚选的本机图片，状态为上传中。
     *  @param requestId 交给宿主上传时用的请求号，须非 0
     *  @param localPath 本机路径
     *  @return 图片已满或请求号为 0 时返回 false，不加入
     */
    bool AddUploading(uint32_t requestId, const std::wstring& localPath);

    /**
     *  加一张编辑帖子时带入的原有图片，状态为已在服务端。
     *  @param image             原有图片的服务端信息
     *  @param downloadRequestId 下载缩略图的请求号；为 0 表示不下载（缩略图画占位底色）
     *  @return 图片已满时返回 false
     */
    bool AddExisting(const ThreadImage& image, uint32_t downloadRequestId);

    /**
     *  上传结果到了。
     *  @param requestId 上传请求号
     *  @param ok        是否成功
     *  @param image     成功时服务端回报的图片信息
     *  @param localPath 宿主回报的本机缓存路径；为空时保留原来的本机路径
     *  @return 请求号属于本模型里某张上传中的图片时返回 true（界面据此重建图片行）
     */
    bool OnUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath);

    /**
     *  原有图片的缩略图下载结果到了。
     *  @return 请求号属于本模型时返回 true
     */
    bool OnDownloaded(uint32_t requestId, bool ok, const std::wstring& localPath);

    /**
     *  重新上传一张上传失败的图片。
     *  @param index        图片序号（0 起）
     *  @param newRequestId 新的上传请求号，须非 0
     *  @return 序号越界、该图不是上传失败状态或请求号为 0 时返回 false
     */
    bool RetryUpload(size_t index, uint32_t newRequestId);

    /**
     *  删除一张图片（任何状态都可以删除；上传中的图片删除后，迟到的上传结果按「不属于本模型」处理）。
     *  @return 序号越界时返回 false
     */
    bool RemoveImage(size_t index);

    //全部图片，按加入顺序
    const std::vector<ComposeImage>& Images() const;

    //请求号是否属于本模型里的某张图片（上传或下载）
    bool HasRequest(uint32_t requestId) const;

    //上传中的张数
    int UploadingCount() const;

    //上传失败的张数
    int FailedCount() const;

    //已在服务端的图片，按显示顺序，提交时用
    std::vector<ThreadImage> UploadedImages() const;

    //---- @ 的人 ----

    /**
     *  @ 一个人。
     *  @return 已在名单里、名单已满 kMaxMentions 人或 userId 不合法时返回 false
     */
    bool AddMention(int64_t userId);

    //从名单里去掉一个人；不在名单里时返回 false
    bool RemoveMention(int64_t userId);

    //是否已 @ 了这个人
    bool HasMention(int64_t userId) const;

    //@ 的人，按加入顺序
    const std::vector<int64_t>& Mentions() const;

    //---- 提交与改动 ----

    /**
     *  提交前的判定：先看图片是否都已上传成功，再按 CheckCompose 校验内容。
     *  @param title    标题（追加补充时须为空）
     *  @param wireText 线上正文（表情已编码）
     *  @return compose_ok 可以提交；compose_images_uploading / compose_images_failed（ComposeSubmitCheck），
     *          或 CheckCompose 的其余结果（ComposeCheck）
     */
    int CheckSubmit(const std::wstring& title, const std::wstring& wireText) const;

    /**
     *  记下当前内容作为「没有改动」的基准：新窗口为空内容，编辑帖子时为带入的原帖。
     *  @param title    当前标题
     *  @param wireText 当前线上正文
     */
    void MarkClean(const std::wstring& title, const std::wstring& wireText);

    /**
     *  与 MarkClean 记下的基准相比，内容是否有改动（标题、正文、图片、@ 的人任一不同即算改动）。
     *  关闭窗口前据此决定要不要先确认。
     */
    bool IsDirty(const std::wstring& title, const std::wstring& wireText) const;

private:
    //图片序列的比较键：每张图的 fileid（上传中、失败的图片用请求号），用于判断改动
    std::vector<std::string> ImageKeys() const;

private:
    int                       m_kind;          //内容种类，取值见 ComposeKind
    std::vector<ComposeImage> m_images;        //图片，按加入顺序
    std::vector<int64_t>      m_mentions;      //@ 的人，按加入顺序
    std::wstring              m_cleanTitle;    //改动基准：标题
    std::wstring              m_cleanText;     //改动基准：线上正文
    std::vector<std::string>  m_cleanImages;   //改动基准：图片序列的比较键
    std::vector<int64_t>      m_cleanMentions; //改动基准：@ 的人
};

/**
 *  拆开多选文件对话框（GetOpenFileName 带 OFN_ALLOWMULTISELECT | OFN_EXPLORER）返回的缓冲区：
 *  选了一个文件时是「完整路径\0\0」，选了多个时是「目录\0文件名1\0文件名2\0\0」。
 *  @param buffer 对话框写好的缓冲区（借用）
 *  @param chars  缓冲区长度（字符），拆分不会越过它
 *  @return 各文件的完整路径，按对话框给出的顺序；缓冲区为空时为空表
 */
std::vector<std::wstring> SplitFileSelection(const wchar_t* buffer, size_t chars);

} // namespace threadgroup
