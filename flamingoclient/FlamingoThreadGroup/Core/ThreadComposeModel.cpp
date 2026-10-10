/**
 *  发帖窗口内容模型的实现，见 ThreadComposeModel.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadComposeModel.h"

#include <algorithm>

#include "ThreadRules.h"

namespace threadgroup {

namespace {

//图片比较键里上传中、上传失败的图片的前缀（这些图片还没有 fileid，用请求号区分）
const char* const kPendingKeyPrefix = "req:";

//路径分隔符
const wchar_t kPathSeparator = L'\\';

/**
 *  从 pos 起读出一个以 \0 结尾的字符串，pos 移到下一个字符串的开头。
 *  @return 读到的字符串；pos 已到缓冲区末尾或遇到连续两个 \0 时为空串
 */
std::wstring ReadPart(const wchar_t* buffer, size_t chars, size_t& pos)
{
    std::wstring part;
    while (pos < chars && buffer[pos] != L'\0')
    {
        part.push_back(buffer[pos]);
        ++pos;
    }
    //越过结尾的 \0
    if (pos < chars)
        ++pos;
    return part;
}

} // namespace

std::vector<std::wstring> SplitFileSelection(const wchar_t* buffer, size_t chars)
{
    std::vector<std::wstring> files;
    if (buffer == NULL || chars == 0)
        return files;
    size_t pos = 0;
    const std::wstring first = ReadPart(buffer, chars, pos);
    if (first.empty())
        return files;
    std::vector<std::wstring> names;
    for (;;)
    {
        const std::wstring name = ReadPart(buffer, chars, pos);
        if (name.empty())
            break;
        names.push_back(name);
    }
    //只选了一个：第一段就是完整路径
    if (names.empty())
    {
        files.push_back(first);
        return files;
    }
    //选了多个：第一段是目录（根目录时已带分隔符，如 C:\）
    std::wstring dir = first;
    if (dir[dir.size() - 1] != kPathSeparator)
        dir.push_back(kPathSeparator);
    for (size_t i = 0; i < names.size(); ++i)
        files.push_back(dir + names[i]);
    return files;
}

CThreadComposeModel::CThreadComposeModel(int kind)
    : m_kind(kind)
{
}

int CThreadComposeModel::Kind() const
{
    return m_kind;
}

int CThreadComposeModel::ImageCount() const
{
    return static_cast<int>(m_images.size());
}

int CThreadComposeModel::RoomForImages() const
{
    const int room = kMaxImages - ImageCount();
    return (room > 0) ? room : 0;
}

bool CThreadComposeModel::AddUploading(uint32_t requestId, const std::wstring& localPath)
{
    if (requestId == 0 || RoomForImages() <= 0)
        return false;
    ComposeImage item;
    item.m_requestId = requestId;
    item.m_localPath = localPath;
    item.m_state = compose_image_uploading;
    m_images.push_back(item);
    return true;
}

bool CThreadComposeModel::AddExisting(const ThreadImage& image, uint32_t downloadRequestId)
{
    if (RoomForImages() <= 0)
        return false;
    ComposeImage item;
    item.m_requestId = downloadRequestId;
    item.m_state = compose_image_done;
    item.m_image = image;
    m_images.push_back(item);
    return true;
}

bool CThreadComposeModel::OnUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath)
{
    if (requestId == 0)
        return false;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        ComposeImage& item = m_images[i];
        if (item.m_requestId != requestId || item.m_state != compose_image_uploading)
            continue;
        item.m_requestId = 0;
        if (ok && !image.m_fileId.empty())
        {
            item.m_state = compose_image_done;
            item.m_image = image;
            if (!localPath.empty())
                item.m_localPath = localPath;
        }
        else
        {
            item.m_state = compose_image_failed;
        }
        return true;
    }
    return false;
}

bool CThreadComposeModel::OnDownloaded(uint32_t requestId, bool ok, const std::wstring& localPath)
{
    if (requestId == 0)
        return false;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        ComposeImage& item = m_images[i];
        if (item.m_requestId != requestId || item.m_state != compose_image_done)
            continue;
        item.m_requestId = 0;
        if (ok)
            item.m_localPath = localPath;
        return true;
    }
    return false;
}

bool CThreadComposeModel::RetryUpload(size_t index, uint32_t newRequestId)
{
    if (index >= m_images.size() || newRequestId == 0 || m_images[index].m_state != compose_image_failed)
        return false;
    m_images[index].m_requestId = newRequestId;
    m_images[index].m_state = compose_image_uploading;
    return true;
}

bool CThreadComposeModel::RemoveImage(size_t index)
{
    if (index >= m_images.size())
        return false;
    m_images.erase(m_images.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

const std::vector<ComposeImage>& CThreadComposeModel::Images() const
{
    return m_images;
}

bool CThreadComposeModel::HasRequest(uint32_t requestId) const
{
    if (requestId == 0)
        return false;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        if (m_images[i].m_requestId == requestId)
            return true;
    }
    return false;
}

int CThreadComposeModel::UploadingCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        if (m_images[i].m_state == compose_image_uploading)
            ++count;
    }
    return count;
}

int CThreadComposeModel::FailedCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        if (m_images[i].m_state == compose_image_failed)
            ++count;
    }
    return count;
}

std::vector<ThreadImage> CThreadComposeModel::UploadedImages() const
{
    std::vector<ThreadImage> result;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        if (m_images[i].m_state == compose_image_done)
            result.push_back(m_images[i].m_image);
    }
    return result;
}

bool CThreadComposeModel::AddMention(int64_t userId)
{
    //userid 为 0 的是「@所有人」（kThreadAtAllUserId），同样记在名单里
    if (userId < 0 || HasMention(userId) || static_cast<int>(m_mentions.size()) >= kMaxMentions)
        return false;
    m_mentions.push_back(userId);
    return true;
}

bool CThreadComposeModel::RemoveMention(int64_t userId)
{
    std::vector<int64_t>::iterator it = std::find(m_mentions.begin(), m_mentions.end(), userId);
    if (it == m_mentions.end())
        return false;
    m_mentions.erase(it);
    return true;
}

bool CThreadComposeModel::HasMention(int64_t userId) const
{
    return std::find(m_mentions.begin(), m_mentions.end(), userId) != m_mentions.end();
}

const std::vector<int64_t>& CThreadComposeModel::Mentions() const
{
    return m_mentions;
}

int CThreadComposeModel::CheckSubmit(const std::wstring& title, const std::wstring& wireText) const
{
    //图片要先全部到服务端：上传中的等它完成，失败的要用户重试或删除，不能悄悄丢掉
    if (UploadingCount() > 0)
        return compose_images_uploading;
    if (FailedCount() > 0)
        return compose_images_failed;
    return CheckCompose(m_kind, title, wireText, ImageCount(), static_cast<int>(m_mentions.size()));
}

void CThreadComposeModel::MarkClean(const std::wstring& title, const std::wstring& wireText)
{
    m_cleanTitle = title;
    m_cleanText = wireText;
    m_cleanImages = ImageKeys();
    m_cleanMentions = m_mentions;
}

bool CThreadComposeModel::IsDirty(const std::wstring& title, const std::wstring& wireText) const
{
    return title != m_cleanTitle || wireText != m_cleanText || ImageKeys() != m_cleanImages
        || m_mentions != m_cleanMentions;
}

std::vector<std::string> CThreadComposeModel::ImageKeys() const
{
    std::vector<std::string> keys;
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        const ComposeImage& item = m_images[i];
        if (item.m_state == compose_image_done)
            keys.push_back(item.m_image.m_fileId);
        else
            keys.push_back(std::string(kPendingKeyPrefix) + std::to_string(static_cast<unsigned long long>(item.m_requestId)));
    }
    return keys;
}

} // namespace threadgroup
