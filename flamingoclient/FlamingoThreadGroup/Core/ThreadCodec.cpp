/**
 *  帖子群报文编解码的实现，见 ThreadCodec.h。字段名与服务端 chatserversrc/ThreadProtocol.cpp 的 postToJson /
 *  commentToJson / addendumToJson / buildChangedNotify 一一对应，改一边要同步改另一边。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadCodec.h"

#include <stdlib.h>

#include "ThreadText.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace threadgroup {

namespace {

//64 位整数十进制文本的最大长度（不含结尾 0），超过即视为不合法
const size_t kMaxIdTextLen = 20;

//把 Json::Value 写成紧凑 JSON（无缩进、无换行）
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本；失败或顶层不是对象返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//取 ID：十进制字符串与整数两种写法都接受；其余返回 0
int64_t ValueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty() || text.size() > kMaxIdTextLen)
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(_atoi64(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//ID 是否明确写成 0（字符串 "0" 或整数 0）：「@所有人」片段的 uid 为 0，要与写错的 uid（ValueToId 同样返回 0）区分开
bool IsExplicitZeroId(const Json::Value& v)
{
    if (v.isString())
        return v.asString() == "0";
    return v.isInt64() && v.asInt64() == 0;
}

//取对象里的 ID 字段；缺失为 0
int64_t GetId(const Json::Value& obj, const char* key)
{
    if (!obj.isObject() || !obj.isMember(key))
        return 0;
    return ValueToId(obj[key]);
}

//取整数字段；缺失或类型不对时为 def
int GetInt(const Json::Value& obj, const char* key, int def)
{
    if (!obj.isObject() || !obj.isMember(key) || !obj[key].isInt())
        return def;
    return obj[key].asInt();
}

//取 64 位整数字段；缺失或类型不对时为 def
int64_t GetInt64(const Json::Value& obj, const char* key, int64_t def)
{
    if (!obj.isObject() || !obj.isMember(key) || !obj[key].isInt64())
        return def;
    return obj[key].asInt64();
}

//取字符串字段（UTF-8）；缺失或类型不对时为空
std::string GetString(const Json::Value& obj, const char* key)
{
    if (!obj.isObject() || !obj.isMember(key) || !obj[key].isString())
        return std::string();
    return obj[key].asString();
}

//取应答的 code：缺失按服务器内部错误
int GetCode(const Json::Value& root)
{
    return GetInt(root, "code", thread_error_server_internal);
}

//ID 列表 → 字符串数组
Json::Value IdsToJson(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(IdToString(ids[i]));
    return arr;
}

//@ 片段 → [{uid, off, len}]
Json::Value AtToJson(const std::vector<ThreadAtSpan>& at)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < at.size(); ++i)
    {
        Json::Value v(Json::objectValue);
        v["uid"] = IdToString(at[i].m_userId);
        v["off"] = at[i].m_off;
        v["len"] = at[i].m_len;
        arr.append(v);
    }
    return arr;
}

//@ 的人、@ 片段与是否 @ 所有人写进请求（发帖、修改、补充、留言共用，docs/帖子群方案.md §15）：mentions 只写真实的人
//（去掉「@所有人」的 0）；片段里有「@所有人」时带 "mentionall": true，没有时不写
void WriteMentionFields(Json::Value& v, const std::vector<int64_t>& mentions, const std::vector<ThreadAtSpan>& at)
{
    std::vector<int64_t> people;
    for (size_t i = 0; i < mentions.size(); ++i)
    {
        if (mentions[i] > 0)
            people.push_back(mentions[i]);
    }
    v["mentions"] = IdsToJson(people);
    if (!at.empty())
        v["at"] = AtToJson(at);
    if (HasAtAll(at))
        v["mentionall"] = true;
}

//[{uid, off, len}] → @ 片段（不是对象、uid 不合法、off / len 不是整数的项跳过）
std::vector<ThreadAtSpan> AtFromJson(const Json::Value& arr)
{
    std::vector<ThreadAtSpan> at;
    if (!arr.isArray())
        return at;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject() || !item["off"].isInt() || !item["len"].isInt())
            continue;
        ThreadAtSpan s;
        s.m_userId = GetId(item, "uid");
        s.m_off = item["off"].asInt();
        s.m_len = item["len"].asInt();
        //uid 为 0 的是「@所有人」（docs/帖子群方案.md §15），只认明确写成 0 的
        const bool atAll = (s.m_userId == kThreadAtAllUserId) && IsExplicitZeroId(item["uid"]);
        if ((s.m_userId > 0 || atAll) && s.m_off >= 0 && s.m_len > 0)
            at.push_back(s);
    }
    return at;
}

//字符串或整数数组 → ID 列表（不合法的项跳过）
std::vector<int64_t> IdsFromJson(const Json::Value& arr)
{
    std::vector<int64_t> ids;
    if (!arr.isArray())
        return ids;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = ValueToId(arr[i]);
        if (id > 0)
            ids.push_back(id);
    }
    return ids;
}

//图片 → JSON
Json::Value ImageToJson(const ThreadImage& img)
{
    Json::Value v(Json::objectValue);
    v["fileid"] = img.m_fileId;
    v["md5"] = img.m_md5;
    v["width"] = img.m_width;
    v["height"] = img.m_height;
    v["size"] = static_cast<Json::Int64>(img.m_size);
    v["thumbfileid"] = img.m_thumbFileId;
    return v;
}

//图片数组 → JSON
Json::Value ImagesToJson(const std::vector<ThreadImage>& images)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < images.size(); ++i)
        arr.append(ImageToJson(images[i]));
    return arr;
}

//JSON → 图片数组（不是对象的项跳过）
std::vector<ThreadImage> ImagesFromJson(const Json::Value& arr)
{
    std::vector<ThreadImage> images;
    if (!arr.isArray())
        return images;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& v = arr[i];
        if (!v.isObject())
            continue;
        ThreadImage img;
        img.m_fileId = GetString(v, "fileid");
        img.m_md5 = GetString(v, "md5");
        img.m_width = GetInt(v, "width", 0);
        img.m_height = GetInt(v, "height", 0);
        img.m_size = GetInt64(v, "size", 0);
        img.m_thumbFileId = GetString(v, "thumbfileid");
        images.push_back(img);
    }
    return images;
}

//JSON → 互动汇总 [{r,n,me}]
std::vector<ThreadReaction> ReactionsFromJson(const Json::Value& arr)
{
    std::vector<ThreadReaction> reactions;
    if (!arr.isArray())
        return reactions;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& v = arr[i];
        if (!v.isObject())
            continue;
        ThreadReaction r;
        r.m_reaction = GetInt(v, "r", 0);
        r.m_count = GetInt(v, "n", 0);
        r.m_mine = GetInt(v, "me", 0) != 0;
        if (r.m_count > 0)
            reactions.push_back(r);
    }
    return reactions;
}

//JSON → 补充
ThreadAddendum AddendumFromJson(const Json::Value& v)
{
    ThreadAddendum a;
    a.m_addId = GetId(v, "addid");
    a.m_postId = GetId(v, "postid");
    a.m_author = GetId(v, "author");
    a.m_text = Utf8ToWide(GetString(v, "text"));
    a.m_images = ImagesFromJson(v["images"]);
    a.m_mentions = IdsFromJson(v["mentions"]);
    a.m_at = AtFromJson(v["at"]);
    a.m_createTime = GetInt64(v, "createtime", 0);
    return a;
}

//JSON → 帖子
ThreadPost PostFromJson(const Json::Value& v)
{
    ThreadPost p;
    p.m_postId = GetId(v, "postid");
    p.m_groupId = GetId(v, "groupid");
    p.m_author = GetId(v, "author");
    p.m_title = Utf8ToWide(GetString(v, "title"));
    p.m_text = Utf8ToWide(GetString(v, "text"));
    p.m_textTruncated = GetInt(v, "texttruncated", 0) != 0;
    p.m_images = ImagesFromJson(v["images"]);
    p.m_imageCount = GetInt(v, "imagecount", static_cast<int>(p.m_images.size()));
    p.m_mentions = IdsFromJson(v["mentions"]);
    p.m_at = AtFromJson(v["at"]);
    p.m_createTime = GetInt64(v, "createtime", 0);
    p.m_editTime = GetInt64(v, "edittime", 0);
    p.m_activeTime = GetInt64(v, "activetime", 0);
    p.m_commentCount = GetInt(v, "commentcount", 0);
    p.m_pinTime = GetInt64(v, "pintime", 0);
    p.m_addendumCount = GetInt(v, "addendumcount", 0);
    p.m_reactions = ReactionsFromJson(v["reactions"]);
    p.m_full = v.isMember("addenda");
    if (p.m_full)
    {
        const Json::Value& arr = v["addenda"];
        for (Json::ArrayIndex i = 0; arr.isArray() && i < arr.size(); ++i)
        {
            if (arr[i].isObject())
                p.m_addenda.push_back(AddendumFromJson(arr[i]));
        }
        p.m_addendumCount = static_cast<int>(p.m_addenda.size());
    }
    return p;
}

//JSON → 留言或回复（含顺带的回复）
ThreadComment CommentFromJson(const Json::Value& v)
{
    ThreadComment c;
    c.m_commentId = GetId(v, "commentid");
    c.m_postId = GetId(v, "postid");
    c.m_author = GetId(v, "author");
    c.m_parentId = GetId(v, "parentid");
    c.m_replyTo = GetId(v, "replyto");
    c.m_text = Utf8ToWide(GetString(v, "text"));
    c.m_mentions = IdsFromJson(v["mentions"]);
    c.m_at = AtFromJson(v["at"]);
    c.m_createTime = GetInt64(v, "createtime", 0);
    c.m_deleted = GetInt(v, "deleted", 0) != 0;
    c.m_replyCount = GetInt(v, "replycount", 0);
    c.m_reactions = ReactionsFromJson(v["reactions"]);
    if (v.isMember("replies") && v["replies"].isArray())
    {
        const Json::Value& arr = v["replies"];
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (arr[i].isObject())
                c.m_replies.push_back(CommentFromJson(arr[i]));
        }
        if (!c.m_replies.empty())
            c.m_replyCursor = c.m_replies[c.m_replies.size() - 1].m_commentId;
    }
    return c;
}

} // namespace

//======================== 请求 ========================

std::string ThreadCodec::BuildListPosts(int64_t groupId, int sort, const std::string& cursor, int limit)
{
    Json::Value v(Json::objectValue);
    v["groupid"] = IdToString(groupId);
    v["sort"] = sort;
    v["cursor"] = cursor;
    v["limit"] = limit;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildPostId(int64_t postId)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildPublishPost(int64_t groupId, const std::string& clientPostId, const std::wstring& title,
                                          const std::wstring& text, const std::vector<ThreadImage>& images,
                                          const std::vector<int64_t>& mentions, const std::vector<ThreadAtSpan>& at)
{
    Json::Value v(Json::objectValue);
    v["groupid"] = IdToString(groupId);
    v["clientpostid"] = clientPostId;
    v["title"] = WideToUtf8(title);
    v["text"] = WideToUtf8(text);
    v["images"] = ImagesToJson(images);
    WriteMentionFields(v, mentions, at);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildEditPost(int64_t postId, const std::wstring& title, const std::wstring& text,
                                       const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                                       const std::vector<ThreadAtSpan>& at)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["title"] = WideToUtf8(title);
    v["text"] = WideToUtf8(text);
    v["images"] = ImagesToJson(images);
    WriteMentionFields(v, mentions, at);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildPin(int64_t postId, bool pin)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["pin"] = pin ? 1 : 0;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildListComments(int64_t postId, int64_t cursor, int limit)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["cursor"] = IdToString(cursor);
    v["limit"] = limit;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildListReplies(int64_t commentId, int64_t cursor, int limit)
{
    Json::Value v(Json::objectValue);
    v["commentid"] = IdToString(commentId);
    v["cursor"] = IdToString(cursor);
    v["limit"] = limit;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildAddComment(int64_t postId, int64_t parentId, const std::string& clientCommentId,
                                         const std::wstring& text, const std::vector<int64_t>& mentions,
                                         const std::vector<ThreadAtSpan>& at)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["parentid"] = IdToString(parentId);
    v["clientcommentid"] = clientCommentId;
    v["text"] = WideToUtf8(text);
    WriteMentionFields(v, mentions, at);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildCommentId(int64_t commentId)
{
    Json::Value v(Json::objectValue);
    v["commentid"] = IdToString(commentId);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildReact(int targetType, int64_t targetId, int reaction, bool on)
{
    Json::Value v(Json::objectValue);
    v["targettype"] = targetType;
    v["targetid"] = IdToString(targetId);
    v["reaction"] = reaction;
    v["on"] = on ? 1 : 0;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildGetReactors(int targetType, int64_t targetId, int reaction, int limit)
{
    Json::Value v(Json::objectValue);
    v["targettype"] = targetType;
    v["targetid"] = IdToString(targetId);
    v["reaction"] = reaction;
    v["limit"] = limit;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildWatch(int64_t groupId, bool watch)
{
    Json::Value v(Json::objectValue);
    v["groupid"] = IdToString(groupId);
    v["watch"] = watch ? 1 : 0;
    return WriteCompact(v);
}

std::string ThreadCodec::BuildAddAddendum(int64_t postId, const std::string& clientAddId, const std::wstring& text,
                                          const std::vector<ThreadImage>& images, const std::vector<int64_t>& mentions,
                                          const std::vector<ThreadAtSpan>& at)
{
    Json::Value v(Json::objectValue);
    v["postid"] = IdToString(postId);
    v["clientaddid"] = clientAddId;
    v["text"] = WideToUtf8(text);
    v["images"] = ImagesToJson(images);
    WriteMentionFields(v, mentions, at);
    return WriteCompact(v);
}

std::string ThreadCodec::BuildAddendumId(int64_t addId)
{
    Json::Value v(Json::objectValue);
    v["addid"] = IdToString(addId);
    return WriteCompact(v);
}

//======================== 应答 ========================

bool ThreadCodec::ParseCode(const std::string& json, int& code)
{
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    return true;
}

bool ThreadCodec::ParseListPosts(const std::string& json, int& code, std::vector<ThreadPost>& posts,
                                 std::string& nextCursor, bool& hasMore)
{
    posts.clear();
    nextCursor.clear();
    hasMore = false;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    if (code != thread_error_ok)
        return true;
    const Json::Value& arr = root["posts"];
    for (Json::ArrayIndex i = 0; arr.isArray() && i < arr.size(); ++i)
    {
        if (arr[i].isObject())
            posts.push_back(PostFromJson(arr[i]));
    }
    nextCursor = GetString(root, "nextcursor");
    hasMore = GetInt(root, "hasmore", 0) != 0;
    return true;
}

bool ThreadCodec::ParsePostResponse(const std::string& json, int& code, ThreadPost& post, bool& hasPost)
{
    post = ThreadPost();
    hasPost = false;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    if (code == thread_error_ok && root.isMember("post") && root["post"].isObject())
    {
        post = PostFromJson(root["post"]);
        hasPost = post.m_postId > 0;
    }
    return true;
}

bool ThreadCodec::ParseCommentsResponse(const std::string& json, int& code, int64_t& ownerId,
                                        std::vector<ThreadComment>& comments, int64_t& nextCursor, bool& hasMore)
{
    comments.clear();
    ownerId = 0;
    nextCursor = 0;
    hasMore = false;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    if (code != thread_error_ok)
        return true;
    ownerId = GetId(root, "ownerid");
    const Json::Value& arr = root["comments"];
    for (Json::ArrayIndex i = 0; arr.isArray() && i < arr.size(); ++i)
    {
        if (arr[i].isObject())
            comments.push_back(CommentFromJson(arr[i]));
    }
    nextCursor = GetId(root, "nextcursor");
    hasMore = GetInt(root, "hasmore", 0) != 0;
    return true;
}

bool ThreadCodec::ParseCommentResponse(const std::string& json, int& code, ThreadComment& comment, bool& hasComment)
{
    comment = ThreadComment();
    hasComment = false;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    if (code == thread_error_ok && root.isMember("comment") && root["comment"].isObject())
    {
        comment = CommentFromJson(root["comment"]);
        hasComment = comment.m_commentId > 0;
    }
    return true;
}

bool ThreadCodec::ParseReactResponse(const std::string& json, int& code, int& targetType, int64_t& targetId,
                                     std::vector<ThreadReaction>& reactions)
{
    reactions.clear();
    targetType = 0;
    targetId = 0;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    targetType = GetInt(root, "targettype", 0);
    targetId = GetId(root, "targetid");
    if (code == thread_error_ok)
        reactions = ReactionsFromJson(root["reactions"]);
    return true;
}

bool ThreadCodec::ParseReactorsResponse(const std::string& json, int& code, int& targetType, int64_t& targetId,
                                        int& reaction, std::vector<int64_t>& users, int& total)
{
    users.clear();
    targetType = 0;
    targetId = 0;
    reaction = 0;
    total = 0;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    targetType = GetInt(root, "targettype", 0);
    targetId = GetId(root, "targetid");
    reaction = GetInt(root, "reaction", 0);
    if (code == thread_error_ok)
    {
        users = IdsFromJson(root["users"]);
        total = GetInt(root, "total", static_cast<int>(users.size()));
    }
    return true;
}

bool ThreadCodec::ParseAddendumResponse(const std::string& json, int& code, ThreadAddendum& addendum,
                                        bool& hasAddendum)
{
    addendum = ThreadAddendum();
    hasAddendum = false;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    if (code == thread_error_ok && root.isMember("addendum") && root["addendum"].isObject())
    {
        addendum = AddendumFromJson(root["addendum"]);
        hasAddendum = addendum.m_addId > 0;
    }
    return true;
}

bool ThreadCodec::ParseIdResponse(const std::string& json, const char* key, int& code, int64_t& id)
{
    id = 0;
    Json::Value root;
    code = thread_error_server_internal;
    if (!ParseObject(json, root))
        return false;
    code = GetCode(root);
    id = GetId(root, key);
    return true;
}

//======================== 推送 ========================

bool ThreadCodec::ParseChangedNotify(const std::string& json, ThreadChange& change)
{
    change = ThreadChange();
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    change.m_groupId = GetId(root, "groupid");
    if (change.m_groupId <= 0)
        return false;
    change.m_changeType = GetInt(root, "changetype", 0);
    change.m_operator = GetId(root, "operator");
    change.m_postId = GetId(root, "postid");
    if (root.isMember("post") && root["post"].isObject())
    {
        change.m_post = PostFromJson(root["post"]);
        change.m_hasPost = change.m_post.m_postId > 0;
    }
    change.m_commentId = GetId(root, "commentid");
    if (root.isMember("comment") && root["comment"].isObject())
    {
        change.m_comment = CommentFromJson(root["comment"]);
        change.m_hasComment = change.m_comment.m_commentId > 0;
    }
    change.m_addId = GetId(root, "addid");
    if (root.isMember("addendum") && root["addendum"].isObject())
    {
        change.m_addendum = AddendumFromJson(root["addendum"]);
        change.m_hasAddendum = change.m_addendum.m_addId > 0;
    }
    change.m_commentCount = GetInt(root, "commentcount", -1);
    change.m_activeTime = GetInt64(root, "activetime", 0);
    change.m_targetType = GetInt(root, "targettype", 0);
    change.m_targetId = GetId(root, "targetid");
    change.m_reaction = GetInt(root, "reaction", 0);
    change.m_on = GetInt(root, "on", 0) != 0;
    change.m_reactions = ReactionsFromJson(root["reactions"]);
    return true;
}

} // namespace threadgroup
