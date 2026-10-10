/**
 *  帖子群 DLL 发帖窗口的单元测试（标签 [threaddll]，源码编进 FlamingoUnit）：
 *    - TGD26 发帖内容模型：图片上传状态、重试与删除，迟到的结果；@ 名单去重与上限；
 *            提交判定（上传中、上传失败、其余按 CheckCompose）；编辑预填与是否改动；多选文件对话框结果的拆分；
 *    - TGD27 群成员名单（docs/帖子群方案.md 14.2 第 5a 步）：去重与丢弃非法项、显示名、选人浮层的顺序（去掉本人、
 *            群主与管理员在前）、清空后回到「还没拉到」；
 *    - TGD31 群成员面板的位置（14.3 第 5f 步）：贴着客户区右边、从头部之下到底部；客户区比面板窄、比头部矮时的处理。
 *    - TGD33 @所有人（§15 第 6d 步）：权限、@ 列表第一项、重扫与编辑时取回、请求带 mentionall、解析「0」片段、底色。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Core/ThreadTypes.h"
#include "../Core/ThreadRules.h"
#include "../Core/ThreadComposeModel.h"
#include "../Core/ThreadMembers.h"
#include "../Core/ThreadMention.h"   // MentionPickList / RescanAtSpans / AtSpansToDisplay：TGD33
#include "../Core/ThreadCodec.h"     // 请求组包与应答解析：TGD33

#include <set>

using namespace threadgroup;

namespace {

//测试用的请求号与用户
const uint32_t kReqA = 11;
const uint32_t kReqB = 12;
const uint32_t kReqC = 13;
const uint32_t kReqRetry = 21;
const int64_t kUserA = 9001;
const int64_t kUserB = 9002;
const uint32_t kFillReqBase = 100;      //填满图片时的请求号起点
const uint32_t kExtraReq = 200;         //图片满后再加的那张的请求号
const int64_t kFillUserBase = 1000;     //填满 @ 名单时的用户 ID 起点
const int64_t kExtraUser = 5000;        //名单满后再加的那个人
const int kImageWidth = 800;            //测试图片的宽（像素）
const int kImageHeight = 600;           //测试图片的高（像素）
const int64_t kImageBytes = 1024;       //测试图片的字节数

//做一张服务端图片
ThreadImage MakeImage(const char* fileId)
{
    ThreadImage image;
    image.m_fileId = fileId;
    image.m_md5 = std::string("md5-") + fileId;
    image.m_width = kImageWidth;
    image.m_height = kImageHeight;
    image.m_size = kImageBytes;
    return image;
}

} // namespace

TEST_CASE("TGD26 发帖内容模型：图片状态、@ 名单、提交判定与是否改动", "[threaddll]")
{
    SECTION("图片：上传中 → 成功 / 失败，重试，删除后迟到的结果不认")
    {
        CThreadComposeModel model(compose_post);
        CHECK(model.RoomForImages() == kMaxImages);
        CHECK_FALSE(model.AddUploading(0, L"C:\\a.png"));
        REQUIRE(model.AddUploading(kReqA, L"C:\\a.png"));
        REQUIRE(model.AddUploading(kReqB, L"C:\\b.png"));
        REQUIRE(model.AddUploading(kReqC, L"C:\\c.png"));
        CHECK(model.ImageCount() == 3);
        CHECK(model.UploadingCount() == 3);
        CHECK(model.HasRequest(kReqB));

        //成功：换成服务端信息；宿主给了缓存路径就用缓存路径
        CHECK(model.OnUploaded(kReqA, true, MakeImage("fa"), L"D:\\cache\\fa.png"));
        CHECK(model.Images()[0].m_state == compose_image_done);
        CHECK(model.Images()[0].m_localPath == L"D:\\cache\\fa.png");
        CHECK(model.Images()[0].m_requestId == 0);
        //同一个结果再来一次不再认
        CHECK_FALSE(model.OnUploaded(kReqA, true, MakeImage("fa"), L""));

        //失败：保留本机路径，可以重试
        CHECK(model.OnUploaded(kReqB, false, ThreadImage(), L""));
        CHECK(model.Images()[1].m_state == compose_image_failed);
        CHECK(model.Images()[1].m_localPath == L"C:\\b.png");
        CHECK(model.FailedCount() == 1);
        //成功但没有 fileid 按失败处理
        ThreadImage noId;
        CHECK(model.OnUploaded(kReqC, true, noId, L""));
        CHECK(model.Images()[2].m_state == compose_image_failed);

        CHECK_FALSE(model.RetryUpload(0, kReqRetry));   //已成功的不能重试
        CHECK_FALSE(model.RetryUpload(1, 0));
        REQUIRE(model.RetryUpload(1, kReqRetry));
        CHECK(model.Images()[1].m_state == compose_image_uploading);
        CHECK(model.OnUploaded(kReqRetry, true, MakeImage("fb"), L""));
        CHECK(model.Images()[1].m_localPath == L"C:\\b.png");   //宿主没给缓存路径时保留原路径

        //删除上传中的图片后，迟到的上传结果不属于本模型
        REQUIRE(model.RetryUpload(2, kReqC));
        REQUIRE(model.RemoveImage(2));
        CHECK_FALSE(model.RemoveImage(5));
        CHECK_FALSE(model.HasRequest(kReqC));
        CHECK_FALSE(model.OnUploaded(kReqC, true, MakeImage("fc"), L""));

        const std::vector<ThreadImage> uploaded = model.UploadedImages();
        REQUIRE(uploaded.size() == 2);
        CHECK(uploaded[0].m_fileId == "fa");
        CHECK(uploaded[1].m_fileId == "fb");
    }

    SECTION("图片上限：满 9 张后不能再加")
    {
        CThreadComposeModel model(compose_addendum);
        for (int i = 0; i < kMaxImages; ++i)
            REQUIRE(model.AddUploading(kFillReqBase + static_cast<uint32_t>(i), L"x.png"));
        CHECK(model.RoomForImages() == 0);
        CHECK_FALSE(model.AddUploading(kExtraReq, L"y.png"));
        CHECK_FALSE(model.AddExisting(MakeImage("z"), 0));
        CHECK(model.ImageCount() == kMaxImages);
    }

    SECTION("@ 名单：去重、上限、删除")
    {
        CThreadComposeModel model(compose_post);
        CHECK(model.AddMention(kUserA));
        CHECK_FALSE(model.AddMention(kUserA));
        //负数不是合法的 userid；0 是「@所有人」（§15，见 TGD33），记在名单里、同样去重
        CHECK_FALSE(model.AddMention(-1));
        CHECK(model.AddMention(kThreadAtAllUserId));
        CHECK_FALSE(model.AddMention(kThreadAtAllUserId));
        CHECK(model.RemoveMention(kThreadAtAllUserId));
        CHECK(model.AddMention(kUserB));
        REQUIRE(model.Mentions().size() == 2);
        CHECK(model.Mentions()[0] == kUserA);
        CHECK(model.RemoveMention(kUserA));
        CHECK_FALSE(model.RemoveMention(kUserA));
        CHECK_FALSE(model.HasMention(kUserA));
        CHECK(model.HasMention(kUserB));

        CThreadComposeModel full(compose_post);
        for (int i = 0; i < kMaxMentions; ++i)
            REQUIRE(full.AddMention(kFillUserBase + i));
        CHECK_FALSE(full.AddMention(kExtraUser));
    }

    SECTION("提交判定：上传中、上传失败优先，其余按 CheckCompose")
    {
        CThreadComposeModel model(compose_post);
        CHECK(model.CheckSubmit(L"", L"") == compose_empty);
        CHECK(model.CheckSubmit(L"", L"hello") == compose_ok);
        CHECK(model.CheckSubmit(std::wstring(kTitleMaxChars + 1, L'a'), L"x") == compose_title_too_long);

        REQUIRE(model.AddUploading(kReqA, L"a.png"));
        CHECK(model.CheckSubmit(L"", L"hello") == compose_images_uploading);
        REQUIRE(model.OnUploaded(kReqA, false, ThreadImage(), L""));
        CHECK(model.CheckSubmit(L"", L"hello") == compose_images_failed);
        REQUIRE(model.RetryUpload(0, kReqB));
        REQUIRE(model.OnUploaded(kReqB, true, MakeImage("fa"), L""));
        //只有图片、没有文字也可以发
        CHECK(model.CheckSubmit(L"", L"") == compose_ok);

        //追加补充不能带标题，正文上限 2000
        CThreadComposeModel addendum(compose_addendum);
        CHECK(addendum.CheckSubmit(L"t", L"x") == compose_not_allowed);
        CHECK(addendum.CheckSubmit(L"", std::wstring(kAddendumTextMaxChars + 1, L'a')) == compose_text_too_long);
        CHECK(addendum.CheckSubmit(L"", std::wstring(kAddendumTextMaxChars, L'a')) == compose_ok);
    }

    SECTION("多选文件对话框的结果：单个文件、同一目录下多个文件、根目录")
    {
        const wchar_t single[] = L"C:\\pics\\a.png\0";
        std::vector<std::wstring> files = SplitFileSelection(single, sizeof(single) / sizeof(single[0]));
        REQUIRE(files.size() == 1);
        CHECK(files[0] == L"C:\\pics\\a.png");

        const wchar_t multi[] = L"C:\\pics\0a.png\0b.jpg\0";
        files = SplitFileSelection(multi, sizeof(multi) / sizeof(multi[0]));
        REQUIRE(files.size() == 2);
        CHECK(files[0] == L"C:\\pics\\a.png");
        CHECK(files[1] == L"C:\\pics\\b.jpg");

        const wchar_t root[] = L"D:\\\0x.png\0y.png\0";
        files = SplitFileSelection(root, sizeof(root) / sizeof(root[0]));
        REQUIRE(files.size() == 2);
        CHECK(files[0] == L"D:\\x.png");

        const wchar_t empty[] = L"\0";
        CHECK(SplitFileSelection(empty, sizeof(empty) / sizeof(empty[0])).empty());
        CHECK(SplitFileSelection(NULL, 0).empty());
    }

    SECTION("编辑预填与是否改动")
    {
        CThreadComposeModel model(compose_post);
        REQUIRE(model.AddExisting(MakeImage("old1"), kReqA));
        REQUIRE(model.AddExisting(MakeImage("old2"), 0));
        REQUIRE(model.AddMention(kUserA));
        model.MarkClean(L"title", L"text");
        CHECK_FALSE(model.IsDirty(L"title", L"text"));
        //原有图片已在服务端，可以直接提交
        CHECK(model.CheckSubmit(L"title", L"text") == compose_ok);
        CHECK(model.UploadedImages().size() == 2);

        //缩略图下载到了只改本机路径，不算改动
        CHECK(model.OnDownloaded(kReqA, true, L"D:\\cache\\old1.png"));
        CHECK(model.Images()[0].m_localPath == L"D:\\cache\\old1.png");
        CHECK_FALSE(model.OnDownloaded(kReqA, true, L"D:\\other.png"));
        CHECK_FALSE(model.IsDirty(L"title", L"text"));

        CHECK(model.IsDirty(L"title2", L"text"));
        CHECK(model.IsDirty(L"title", L"text2"));

        REQUIRE(model.RemoveMention(kUserA));
        CHECK(model.IsDirty(L"title", L"text"));
        REQUIRE(model.AddMention(kUserA));
        CHECK_FALSE(model.IsDirty(L"title", L"text"));

        REQUIRE(model.AddUploading(kReqB, L"new.png"));
        CHECK(model.IsDirty(L"title", L"text"));
        REQUIRE(model.RemoveImage(2));
        CHECK_FALSE(model.IsDirty(L"title", L"text"));
        REQUIRE(model.RemoveImage(0));
        CHECK(model.IsDirty(L"title", L"text"));
    }
}

namespace {

//名单里的一个人
ThreadMember MakeMember(int64_t userId, int role, const wchar_t* name)
{
    ThreadMember m;
    m.m_userId = userId;
    m.m_role = role;
    m.m_name = name;
    return m;
}

} // namespace

TEST_CASE("TGD27 群成员名单：去重、显示名与选人浮层的顺序", "[threaddll]")
{
    const int64_t kSelf = 7001;
    const int64_t kOwner = 7002;
    const int64_t kAdmin = 7003;
    const int64_t kMemberA = 7004;
    const int64_t kMemberB = 7005;
    const int kUnknownRole = 9;   //不认识的角色值：按普通成员排

    CThreadMemberBook book;
    CHECK_FALSE(book.IsLoaded());
    CHECK(book.Count() == 0);
    CHECK(book.NameOf(kOwner).empty());

    std::vector<ThreadMember> list;
    list.push_back(MakeMember(kMemberA, thread_member_role_member, L"甲"));
    list.push_back(MakeMember(kSelf, thread_member_role_member, L"我"));
    list.push_back(MakeMember(kAdmin, thread_member_role_admin, L"管理员乙"));
    list.push_back(MakeMember(kMemberA, thread_member_role_member, L"甲的重复项"));   //重复：保留第一次
    list.push_back(MakeMember(0, thread_member_role_member, L"非法"));               //ID 不大于 0：丢弃
    list.push_back(MakeMember(kOwner, thread_member_role_owner, L"群主丙"));
    list.push_back(MakeMember(kMemberB, kUnknownRole, L"丁"));
    book.Set(list);
    CHECK(book.IsLoaded());
    CHECK(book.Count() == 5);

    SECTION("显示名取宿主给的名字（群名片或昵称），不在名单里时为空")
    {
        CHECK(book.NameOf(kMemberA) == L"甲");
        CHECK(book.NameOf(kOwner) == L"群主丙");
        CHECK(book.NameOf(9999).empty());
    }

    SECTION("选人浮层：去掉本人，群主、管理员在前，同一角色保持原顺序")
    {
        const std::vector<ThreadMember> order = book.PickOrder(kSelf);
        REQUIRE(order.size() == 4);
        CHECK(order[0].m_userId == kOwner);
        CHECK(order[1].m_userId == kAdmin);
        CHECK(order[2].m_userId == kMemberA);
        CHECK(order[3].m_userId == kMemberB);
        //传 0 时不去掉任何人
        CHECK(book.PickOrder(0).size() == 5);
    }

    SECTION("清空后回到「还没拉到」；拉到空名单也算拉到了")
    {
        book.Clear();
        CHECK_FALSE(book.IsLoaded());
        CHECK(book.Count() == 0);
        book.Set(std::vector<ThreadMember>());
        CHECK(book.IsLoaded());
        CHECK(book.PickOrder(kSelf).empty());
    }
}

TEST_CASE("TGD31 群成员面板的位置：贴右、头部之下到底部", "[threaddll]")
{
    const int kPanelWidth = 300;    //面板设计宽
    const int kTop = 90;            //标题栏加头部的高

    SECTION("客户区足够大：宽为设计宽，右边贴着客户区右边")
    {
        const int kClientW = 1100;
        const int kClientH = 760;
        const ThreadPanelRect rc = ComputeMemberPanelRect(kClientW, kClientH, kTop, kPanelWidth);
        CHECK(rc.m_right == kClientW);
        CHECK(rc.m_left == kClientW - kPanelWidth);
        CHECK(rc.m_top == kTop);
        CHECK(rc.m_bottom == kClientH);
    }

    SECTION("客户区比面板窄：面板占满客户区宽度")
    {
        const int kClientW = 200;
        const int kClientH = 500;
        const ThreadPanelRect rc = ComputeMemberPanelRect(kClientW, kClientH, kTop, kPanelWidth);
        CHECK(rc.m_left == 0);
        CHECK(rc.m_right == kClientW);
    }

    SECTION("客户区比头部还矮：高度为 0，不出现负高度")
    {
        const int kClientW = 800;
        const int kClientH = 60;
        const ThreadPanelRect rc = ComputeMemberPanelRect(kClientW, kClientH, kTop, kPanelWidth);
        CHECK(rc.m_top == kTop);
        CHECK(rc.m_bottom == kTop);
    }

    SECTION("客户区为空（窗口还没建好或最小化）：矩形为空，不出现负值")
    {
        const ThreadPanelRect rc = ComputeMemberPanelRect(0, 0, kTop, kPanelWidth);
        CHECK(rc.m_left == 0);
        CHECK(rc.m_right == 0);
        CHECK(rc.m_bottom >= rc.m_top);
    }
}

TEST_CASE("TGD33 @所有人：权限、列表第一项、重扫、请求带 mentionall、解析与底色", "[threaddll]")
{
    const int64_t kSelf = 4101;    //本人
    const int64_t kZhang = 4103;   //张三

    SECTION("权限：群主、管理员恒可；普通成员只在群设置为「所有人」时可以；不是成员不可")
    {
        CHECK(CanMentionAll(thread_role_owner, thread_at_all_owner_admin));
        CHECK(CanMentionAll(thread_role_admin, thread_at_all_owner_admin));
        CHECK_FALSE(CanMentionAll(thread_role_member, thread_at_all_owner_admin));
        CHECK(CanMentionAll(thread_role_member, thread_at_all_everyone));
        CHECK_FALSE(CanMentionAll(thread_role_none, thread_at_all_everyone));
    }

    SECTION("@ 列表：有权限时第一项是「所有人」，其余顺序不变；没有权限时没有这一项")
    {
        std::vector<ThreadMember> order;
        ThreadMember zhang;
        zhang.m_userId = kZhang;
        zhang.m_name = L"张三";
        order.push_back(zhang);
        const std::vector<ThreadMember> withAll = MentionPickList(order, true);
        REQUIRE(withAll.size() == 2);
        CHECK(withAll[0].m_userId == kThreadAtAllUserId);
        CHECK(withAll[0].m_name == L"所有人");
        CHECK(withAll[1].m_userId == kZhang);
        const std::vector<ThreadMember> without = MentionPickList(order, false);
        REQUIRE(without.size() == 1);
        CHECK(without[0].m_userId == kZhang);
        //插入正文的名字与普通群相同，不随界面语言变化
        CHECK(AtAllName() == L"所有人");
    }

    //「@所有人」10 字节从 0 开始（UTF-8），「 和 」之后「@张三」从 15 开始
    const std::wstring wire = L"@所有人 和 @张三 看一下";
    std::vector<ThreadAtCandidate> candidates;
    ThreadAtCandidate all;
    all.m_userId = kThreadAtAllUserId;
    all.m_name = AtAllName();
    candidates.push_back(all);
    ThreadAtCandidate zhangCand;
    zhangCand.m_userId = kZhang;
    zhangCand.m_name = L"张三";
    candidates.push_back(zhangCand);

    SECTION("选中后插入「@所有人 」：按最终正文重扫得到 uid 为 0 的片段；编辑时从原片段取回候选")
    {
        const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, candidates);
        REQUIRE(at.size() == 2);
        CHECK(at[0].m_userId == kThreadAtAllUserId);
        CHECK(at[0].m_off == 0);
        CHECK(at[0].m_len == 10);
        CHECK(at[1].m_userId == kZhang);
        CHECK(at[1].m_off == 15);
        CHECK(HasAtAll(at));
        const std::vector<ThreadAtCandidate> back = CandidatesFromSpans(wire, at);
        REQUIRE(back.size() == 2);
        CHECK(back[0].m_userId == kThreadAtAllUserId);
        CHECK(back[0].m_name == L"所有人");
        //把「@所有人」删掉之后重扫不再有它
        CHECK_FALSE(HasAtAll(RescanAtSpans(L"和 @张三 看一下", candidates)));
    }

    SECTION("请求：片段里有「@所有人」时带 mentionall，mentions 里不出现 0；没有时不带")
    {
        const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, candidates);
        std::vector<int64_t> mentions;
        mentions.push_back(kThreadAtAllUserId);
        mentions.push_back(kZhang);
        const std::string comment = ThreadCodec::BuildAddComment(100, 0, "tc-1", wire, mentions, at);
        CHECK(comment.find("\"mentionall\":true") != std::string::npos);
        CHECK(comment.find("\"mentions\":[\"4103\"]") != std::string::npos);
        CHECK(comment.find("\"uid\":\"0\"") != std::string::npos);
        const std::string post = ThreadCodec::BuildPublishPost(6, "c-1", L"", wire, std::vector<ThreadImage>(),
                                                               mentions, at);
        CHECK(post.find("\"mentionall\":true") != std::string::npos);
        CHECK(ThreadCodec::BuildEditPost(5, L"", wire, std::vector<ThreadImage>(), mentions, at)
                  .find("\"mentionall\":true") != std::string::npos);
        CHECK(ThreadCodec::BuildAddAddendum(5, "a-1", wire, std::vector<ThreadImage>(), mentions, at)
                  .find("\"mentionall\":true") != std::string::npos);
        //只 @ 了张三：不带 mentionall
        std::vector<ThreadAtSpan> onlyZhang(1, at[1]);
        const std::string plain =
            ThreadCodec::BuildAddComment(100, 0, "tc-2", wire, std::vector<int64_t>(1, kZhang), onlyZhang);
        CHECK(plain.find("mentionall") == std::string::npos);
    }

    SECTION("解析：uid 明确为 \"0\" 的片段保留为「@所有人」，写错的 uid 丢弃")
    {
        const std::string postJson =
            "{\"code\":0,\"msg\":\"ok\",\"post\":{\"postid\":\"5\",\"groupid\":\"6\",\"author\":\"7\","
            "\"text\":\"x\",\"mentionall\":true,\"at\":[{\"uid\":\"0\",\"off\":0,\"len\":10},"
            "{\"uid\":\"x1\",\"off\":11,\"len\":3},{\"uid\":\"4103\",\"off\":15,\"len\":7}]}}";
        int code = -1;
        ThreadPost post;
        bool hasPost = false;
        REQUIRE(ThreadCodec::ParsePostResponse(postJson, code, post, hasPost));
        REQUIRE(post.m_at.size() == 2);
        CHECK(post.m_at[0].m_userId == kThreadAtAllUserId);
        CHECK(post.m_at[1].m_userId == kZhang);
    }

    SECTION("插入前补空格：光标前一个字不是空白时补一个，否则「hello@所有人」重扫不出片段（第 6h 步联调发现）")
    {
        //正文开头、空格 / 制表符 / 回车 / 换行之后：不补
        CHECK_FALSE(NeedsSpaceBeforeAt(0, L'\0'));
        CHECK_FALSE(NeedsSpaceBeforeAt(5, L' '));
        CHECK_FALSE(NeedsSpaceBeforeAt(5, L'\t'));
        CHECK_FALSE(NeedsSpaceBeforeAt(5, L'\r'));
        CHECK_FALSE(NeedsSpaceBeforeAt(5, L'\n'));
        //文字、表情图（U+FFFC）、全角空格之后：补（重扫只认 ASCII 空白）
        CHECK(NeedsSpaceBeforeAt(5, L'o'));
        CHECK(NeedsSpaceBeforeAt(5, static_cast<wchar_t>(0xFFFC)));
        CHECK(NeedsSpaceBeforeAt(5, static_cast<wchar_t>(0x3000)));
        //补了空格之后重扫认得出，没补时认不出
        CHECK(HasAtAll(RescanAtSpans(L"hello @所有人 ", candidates)));
        CHECK_FALSE(HasAtAll(RescanAtSpans(L"hello@所有人 ", candidates)));
    }

    SECTION("底色：「@所有人」与「@本人」用同一种配色（与普通群相同），@张三 按普通 @ 配色")
    {
        const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, candidates);
        const std::vector<ThreadAtDisplay> disp = AtSpansToDisplay(wire, at, kSelf, std::set<int>(), 0);
        REQUIRE(disp.size() == 2);
        CHECK(disp[0].m_start == 0);
        CHECK(disp[0].m_end == 4);
        CHECK(disp[0].m_isMe);
        CHECK_FALSE(disp[1].m_isMe);
    }
}
