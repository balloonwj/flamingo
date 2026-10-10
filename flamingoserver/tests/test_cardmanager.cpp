/**
 *  群互动卡片互动存储管理器单元测试（Suite KM），test_cardmanager.cpp
 *
 *  覆盖 CardManager 对 t_card_interaction 的写入、覆盖、删除与按卡片读取：
 *  修改本人那条后名次保持不变（2026-10-02 之前修改会让这一条排到末尾）、重复参与只保留一行、撤回后再参与
 *  排到末尾、不同卡片互不干扰、含特殊字符的文本原样读回。对应方案见
 *  docs/群接龙方案.md 第 7 节 1a 步骤。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— CardManager 经 DbFactory 取连接，
 *  而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdio.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/CardManager.h"
#include "../dbapi/DbConfig.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与其它套用的文件分开，避免互相干扰
static const char* const CARD_DB_PATH = "flamingo_sqlite_card_test.db";

//两张卡片所在的消息ID
static const int64_t kCardMsgA = 9007199254741011LL;
static const int64_t kCardMsgB = 9007199254741012LL;

//参与接龙的三个用户
static const int32_t kUserA = 100001;
static const int32_t kUserB = 100002;
static const int32_t kUserC = 100003;

//依次递增的互动时间，Unix毫秒；取值只需保证先后顺序
static const int64_t kTime1 = 1000;
static const int64_t kTime2 = 2000;
static const int64_t kTime3 = 3000;
static const int64_t kTime4 = 4000;
static const int64_t kTime5 = 5000;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeCardDbFiles()
{
    std::string basePath(CARD_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始。
static void resetCardDb()
{
    removeCardDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(CARD_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, CARD_DB_PATH);
}

TEST_CASE("KM1 修改本人那条后名次不变")
{
    resetCardDb();
    CardManager mgr;

    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "A", kTime1));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, "B", kTime2));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserC, "C", kTime3));

    //B 在 A、B、C 都参与之后修改自己那条
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, "B-edited", kTime4));

    std::vector<CardInteractionRecord> rows;
    REQUIRE(mgr.getCardInteractions(kCardMsgA, rows));
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].userid == kUserA);
    CHECK(rows[1].userid == kUserB);
    CHECK(rows[2].userid == kUserC);

    //内容是修改后的，时间仍是首次参与的时间
    CHECK(rows[1].data == "B-edited");
    CHECK(rows[1].createtime == kTime2);
}

TEST_CASE("KM2 重复参与只保留一行，内容为最后一次提交")
{
    resetCardDb();
    CardManager mgr;

    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "first", kTime1));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "second", kTime2));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "third", kTime3));

    std::vector<CardInteractionRecord> rows;
    REQUIRE(mgr.getCardInteractions(kCardMsgA, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].userid == kUserA);
    CHECK(rows[0].data == "third");
}

TEST_CASE("KM3 撤回本人那条后再参与，排到列表末尾")
{
    resetCardDb();
    CardManager mgr;

    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "A", kTime1));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, "B", kTime2));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserC, "C", kTime3));

    //B 撤回后重新参与：这是一行新的记录，时间取重新参与的时刻
    REQUIRE(mgr.removeInteraction(kCardMsgA, kUserB));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, "B-again", kTime5));

    std::vector<CardInteractionRecord> rows;
    REQUIRE(mgr.getCardInteractions(kCardMsgA, rows));
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].userid == kUserA);
    CHECK(rows[1].userid == kUserC);
    CHECK(rows[2].userid == kUserB);
    CHECK(rows[2].data == "B-again");
    CHECK(rows[2].createtime == kTime5);
}

TEST_CASE("KM4 不同卡片的互动互不干扰")
{
    resetCardDb();
    CardManager mgr;

    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "A on card A", kTime1));
    REQUIRE(mgr.upsertInteraction(kCardMsgB, kUserC, "C on card B", kTime2));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, "B on card A", kTime3));

    std::vector<CardInteractionRecord> rowsA;
    REQUIRE(mgr.getCardInteractions(kCardMsgA, rowsA));
    REQUIRE(rowsA.size() == 2);
    CHECK(rowsA[0].userid == kUserA);
    CHECK(rowsA[1].userid == kUserB);

    std::vector<CardInteractionRecord> rowsB;
    REQUIRE(mgr.getCardInteractions(kCardMsgB, rowsB));
    REQUIRE(rowsB.size() == 1);
    CHECK(rowsB[0].userid == kUserC);
    CHECK(rowsB[0].data == "C on card B");
}

TEST_CASE("KM5 含单引号、反斜杠与中文的文本写入后原样读回")
{
    resetCardDb();
    CardManager mgr;

    //单引号与反斜杠必须经 escapeString 转义，否则 SQL 语句会被截断或报错
    const std::string text = u8"it's a \\ test 张三 本人+1";
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, text, kTime1));

    //覆盖写入走的是冲突更新分支，同样要能原样保存
    const std::string edited = u8"it''s \\\\ 改过 'quoted'";
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, text, kTime2));
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserB, edited, kTime3));

    std::vector<CardInteractionRecord> rows;
    REQUIRE(mgr.getCardInteractions(kCardMsgA, rows));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].data == text);
    CHECK(rows[1].data == edited);
}

TEST_CASE("KM6 getInteraction 取某人对某卡片的那一条互动")
{
    resetCardDb();
    CardManager mgr;

    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "A", kTime1));
    //修改后再取：内容是新的，时间仍是首次参与的时间
    REQUIRE(mgr.upsertInteraction(kCardMsgA, kUserA, "A-edited", kTime2));

    //有记录
    CardInteractionRecord rec;
    bool found = false;
    REQUIRE(mgr.getInteraction(kCardMsgA, kUserA, rec, found));
    CHECK(found);
    CHECK(rec.userid == kUserA);
    CHECK(rec.data == "A-edited");
    CHECK(rec.createtime == kTime1);

    //没有记录：本人没参与过、或参与的是另一张卡片。数据库访问本身成功，返回 true
    CardInteractionRecord untouched;
    untouched.data = "untouched";
    found = true;
    CHECK(mgr.getInteraction(kCardMsgA, kUserB, untouched, found));
    CHECK_FALSE(found);
    CHECK(untouched.data == "untouched");

    found = true;
    CHECK(mgr.getInteraction(kCardMsgB, kUserA, untouched, found));
    CHECK_FALSE(found);

    //撤回后再取：没有记录
    REQUIRE(mgr.removeInteraction(kCardMsgA, kUserA));
    found = true;
    CHECK(mgr.getInteraction(kCardMsgA, kUserA, untouched, found));
    CHECK_FALSE(found);

}

TEST_CASE("KM7 closeCard / getCardState 提前结束接龙的写入与读取")
{
    resetCardDb();
    CardManager mgr;

    //还没结束：读不到结束记录，数据库访问本身成功
    CardStateRecord st;
    bool found = true;
    REQUIRE(mgr.getCardState(kCardMsgA, st, found));
    CHECK_FALSE(found);
    CHECK_FALSE(st.closed);

    //第一次结束：确实写入
    bool inserted = false;
    REQUIRE(mgr.closeCard(kCardMsgA, kUserB, kTime3, inserted));
    CHECK(inserted);
    REQUIRE(mgr.getCardState(kCardMsgA, st, found));
    CHECK(found);
    CHECK(st.closed);
    CHECK(st.closer == kUserB);
    CHECK(st.closetime == kTime3);

    //第二次结束（模拟另一位管理员几乎同时点了结束）：不写入，原操作者与时间保持不变
    inserted = true;
    REQUIRE(mgr.closeCard(kCardMsgA, kUserC, kTime4, inserted));
    CHECK_FALSE(inserted);
    CardStateRecord again;
    REQUIRE(mgr.getCardState(kCardMsgA, again, found));
    CHECK(found);
    CHECK(again.closer == kUserB);
    CHECK(again.closetime == kTime3);

    //别的卡片读不到这条结束记录
    CardStateRecord other;
    found = true;
    REQUIRE(mgr.getCardState(kCardMsgB, other, found));
    CHECK_FALSE(found);

    //本套最后一条用例，清掉临时库文件
    removeCardDbFiles();
}
