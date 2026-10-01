/**
 *  文件元数据存储管理器单元测试（Suite FSM），test_filestoremanager.cpp
 *
 *  覆盖 FileStoreManager::findCompletedByMd5 的秒传命中规则：只命中**同一场景**、
 *  且已上传完成的记录（FSM1～FSM5）；以及 markCompletedByUploader 的标记规则：只有
 *  上传者本人引用时才把记录标记为已完成，重复标记无害（FSM6～FSM8）。
 *
 *  背景：2026-09-30 一张聊天图片（场景 4）按 MD5 命中了同内容
 *  群文件（场景 2）的记录，服务端直接复用了那条记录的 fileid。下载时令牌场景与
 *  目标服务器都取自记录本身，接收方被指向 fileserver，而客户端收图固定连
 *  imgserver，imgserver 按场景拒绝，图片永远下载失败。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— FileStoreManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include <stdio.h>
#include <string>
#include "doctest.h"
#include "../chatserversrc/FileStoreManager.h"
#include "../chatserversrc/Protocol.h"          //file_scene
#include "../dbapi/DbConfig.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与别的套用的文件分开，避免互相干扰
static const char* const FSM_DB_PATH = "flamingo_sqlite_filestore_test.db";

//本套用例统一使用的记录创建时间（Unix 毫秒），取固定值使结果可复现
static const int64_t FSM_TEST_TIME = 1700000000000LL;

//样本文件：取自 2026-09-30 下载失败的那张图片的真实元数据
static const char* const FSM_MD5      = "e17efa5d322c5215671cc31bf668ac0e";
static const char* const FSM_NAME     = "agent2.png";
static const char* const FSM_EXT      = "png";
static const int64_t     FSM_SIZE     = 210616;    //字节
static const int64_t     FSM_UPLOADER = 5127;      //上传者 userid

//客户端上传可用的四种场景（升级包场景不经上传流程、不进 t_file，不在此列）
static const int FSM_UPLOAD_SCENES[] = {
    file_scene_chat_file,
    file_scene_group_file,
    file_scene_avatar,
    file_scene_image_msg,
};
static const size_t FSM_UPLOAD_SCENE_COUNT =
    sizeof(FSM_UPLOAD_SCENES) / sizeof(FSM_UPLOAD_SCENES[0]);

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeFsmDbFiles()
{
    std::string basePath(FSM_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//FileStoreManager 是单例、用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetFsmDb()
{
    removeFsmDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 补列 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(FSM_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, FSM_DB_PATH);
}

//按样本文件建一条指定场景的记录。
//  scene：用途场景，取值见 enum file_scene
//  completed：true 时顺带标记为已完成（可被秒传命中），false 时停留在待上传
//  返回：新记录的 fileid
static int64_t addFileRecord(int scene, bool completed)
{
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();
    int64_t fileid = mgr.createPendingFile(FSM_MD5, FSM_NAME, FSM_EXT, FSM_SIZE,
                                           scene, FSM_UPLOADER, FSM_TEST_TIME);
    REQUIRE(fileid > 0);
    if (completed)
        REQUIRE(mgr.markCompleted(fileid));
    return fileid;
}

TEST_CASE("FSM1 图片消息不命中同内容群文件的记录")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //2026-09-30 那次故障的原样复现：群文件先传完，随后同内容作为图片消息申请上传
    addFileRecord(file_scene_group_file, true);

    FileRecord hit;
    CHECK_FALSE(mgr.findCompletedByMd5(FSM_MD5, file_scene_image_msg, hit));
}

TEST_CASE("FSM2 同场景照常命中，回填的是那条记录")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    int64_t groupFileId = addFileRecord(file_scene_group_file, true);

    FileRecord hit;
    REQUIRE(mgr.findCompletedByMd5(FSM_MD5, file_scene_group_file, hit));
    CHECK(hit.fileid == groupFileId);
    CHECK(hit.scene == file_scene_group_file);
    CHECK(hit.md5 == FSM_MD5);
    CHECK(hit.size == FSM_SIZE);
}

TEST_CASE("FSM3 同内容有两条不同场景的记录时，各自只命中自己那条")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //修复上线后同一内容会有两条记录：旧的群文件记录，以及图片消息新建的记录
    int64_t groupFileId = addFileRecord(file_scene_group_file, true);
    int64_t imageMsgId  = addFileRecord(file_scene_image_msg, true);
    REQUIRE(groupFileId != imageMsgId);

    FileRecord hit;
    REQUIRE(mgr.findCompletedByMd5(FSM_MD5, file_scene_image_msg, hit));
    CHECK(hit.fileid == imageMsgId);
    CHECK(hit.scene == file_scene_image_msg);

    REQUIRE(mgr.findCompletedByMd5(FSM_MD5, file_scene_group_file, hit));
    CHECK(hit.fileid == groupFileId);
    CHECK(hit.scene == file_scene_group_file);
}

TEST_CASE("FSM4 四种上传场景两两交叉，只有场景相同时命中")
{
    //同一台服务器上的两种场景也不能互相命中：聊天文件若复用群文件的记录，会被
    //群文件的下载鉴权拦下；头像与图片消息之间同理按精确匹配处理，规则统一。
    for (size_t i = 0; i < FSM_UPLOAD_SCENE_COUNT; ++i)
    {
        const int storedScene = FSM_UPLOAD_SCENES[i];
        resetFsmDb();
        FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();
        int64_t storedId = addFileRecord(storedScene, true);

        for (size_t j = 0; j < FSM_UPLOAD_SCENE_COUNT; ++j)
        {
            const int queryScene = FSM_UPLOAD_SCENES[j];
            CAPTURE(storedScene);
            CAPTURE(queryScene);

            FileRecord hit;
            bool found = mgr.findCompletedByMd5(FSM_MD5, queryScene, hit);
            CHECK(found == (queryScene == storedScene));
            if (found)
                CHECK(hit.fileid == storedId);
        }
    }
}

TEST_CASE("FSM5 同场景但尚未传完的记录不命中")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //待上传的记录磁盘上还没有完整文件，命中它等于让接收方去下一个半截文件
    addFileRecord(file_scene_image_msg, false);

    FileRecord hit;
    CHECK_FALSE(mgr.findCompletedByMd5(FSM_MD5, file_scene_image_msg, hit));
}

//非上传者的 userid（与 FSM_UPLOADER 不同），模拟转发别人图片的用户
static const int64_t FSM_OTHER_USER = 5303;

TEST_CASE("FSM6 上传者本人引用后记录变为已完成，同内容同场景可秒传")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //发图流程：申请上传建出待上传记录 → 字节传完 → 上传者用该 fileid 发消息
    int64_t fileid = addFileRecord(file_scene_image_msg, false);
    REQUIRE(mgr.markCompletedByUploader(fileid, FSM_UPLOADER));

    FileRecord rec;
    REQUIRE(mgr.getFileById(fileid, rec));
    CHECK(rec.status == file_status_completed);

    FileRecord hit;
    REQUIRE(mgr.findCompletedByMd5(FSM_MD5, file_scene_image_msg, hit));
    CHECK(hit.fileid == fileid);
}

TEST_CASE("FSM7 非上传者引用不改动记录")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //别人拿这个 fileid 发消息（例如转发）：数据库访问本身成功，但记录不应被改动，
    //否则一份半途失败的上传也可能被别人标记为已完成
    int64_t fileid = addFileRecord(file_scene_image_msg, false);
    CHECK(mgr.markCompletedByUploader(fileid, FSM_OTHER_USER));

    FileRecord rec;
    REQUIRE(mgr.getFileById(fileid, rec));
    CHECK(rec.status == file_status_pending);

    FileRecord hit;
    CHECK_FALSE(mgr.findCompletedByMd5(FSM_MD5, file_scene_image_msg, hit));
}

TEST_CASE("FSM8 对已完成的记录再标记一次，仍为已完成且返回成功")
{
    resetFsmDb();
    FileStoreManager& mgr = Singleton<FileStoreManager>::Instance();

    //发送方重发、或同一张图被上传者本人再次引用时都会走到这里，不能报错
    int64_t fileid = addFileRecord(file_scene_image_msg, true);
    CHECK(mgr.markCompletedByUploader(fileid, FSM_UPLOADER));
    CHECK(mgr.markCompletedByUploader(fileid, FSM_UPLOADER));

    FileRecord rec;
    REQUIRE(mgr.getFileById(fileid, rec));
    CHECK(rec.status == file_status_completed);
}
