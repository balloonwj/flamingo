#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
chatserver 主流程端到端冒烟测试（走真实 TCP 协议，不是单元测试）。

balloonwj@qq.com   2026-08-01

用途：验证「注册 → 登录 → 加好友 → 单聊 → 会话/消息同步 → 建群 → 群聊」这条主链路
在指定的数据库后端上真的能跑通。写它的直接动机是 SQLite 本地模式改造 —— 单元测试
只覆盖到驱动层，各业务 Manager 那 200 多条 SQL 究竟能不能在 SQLite 上正确执行，
必须把服务端拉起来真跑一遍才算数（见 docs/SQLite本地数据库模式方案.md 第 10 节）。

用法：
    1) 按需把 etc/chatserver.conf 的 dbtype 改成 sqlite（或保持 mysql 也能跑本脚本）
    2) 启动 chatserver，并把它的标准输出重定向到一个日志文件
    3) python tools/e2e_sqlite_smoke.py --log <该日志文件> [--db <sqlite 库文件>]

为什么需要 --log：注册要短信验证码，而短信下发是桩实现，验证码只写进服务端日志
（见 ChatSession::onSendSmsCode）。脚本从日志里把它捞出来，这也是开发期的既定用法。

--db 可选：给了就在跑完后直接查库，核对数据确实落盘了，而不只是应答说成功。

退出码 0 表示全部通过，非 0 表示有断言失败。
"""
import argparse
import json
import os
import re
import socket
import struct
import sys
import time
import zlib

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

#---- 帧格式常量，与 net/FrameCodec 及客户端保持一致 ----
FRAME_MAGIC = 0xF1A3
PROTOCOL_V2 = 0x02
FRAME_HEADER_LEN = 24
#帧头 flags 的最低位表示包体是否经过 zlib 压缩
FRAME_FLAG_COMPRESSED = 0x01

#---- 命令字，取自 chatserversrc/Protocol.h ----
CMD_LOGIN = 1003
CMD_REGISTER = 1101
CMD_SEND_SMS_CODE = 1102
CMD_GET_CONTACTS = 1201
CMD_SET_FRIEND_REMARK = 1209
CMD_CREATE_GROUP = 1301
CMD_SYNC_CONVERSATIONS = 1501
CMD_SYNC_MESSAGES = 1502
CMD_SEND_MESSAGE = 1503
CMD_MESSAGE_NOTIFY = 1504
CMD_MARK_READ = 1506

#会话类型：1 单聊，2 群聊（见 Protocol.h 的 conversation_single / conversation_group）
CTYPE_SINGLE = 1
CTYPE_GROUP = 2
#消息内容类型：1 为文本
MSGTYPE_TEXT = 1

#等待一帧应答的最长时间，单位秒
RECV_TIMEOUT_SECONDS = 8
#从服务端日志里捞验证码的最长等待时间，单位秒。日志是异步线程落盘的，需要一点余量
SMS_CODE_WAIT_SECONDS = 10


class SmokeError(Exception):
    """断言失败或协议异常。统一抛这个，主流程捕获后打印并以非 0 退出。"""


def crc32_ieee(data):
    """复刻 FrameCodec::crc32：CRC-32/IEEE，初值 0xFFFFFFFF、最终取反。"""
    crc = 0xFFFFFFFF
    for b in bytearray(data):
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF


def encode_frame(cmd, seq, body_bytes):
    hdr = bytearray(FRAME_HEADER_LEN)
    struct.pack_into(">H", hdr, 0, FRAME_MAGIC)
    hdr[2] = PROTOCOL_V2
    hdr[3] = 0
    struct.pack_into(">H", hdr, 4, cmd)
    struct.pack_into(">H", hdr, 6, 0)
    struct.pack_into(">I", hdr, 8, seq)
    struct.pack_into(">I", hdr, 12, len(body_bytes))
    struct.pack_into(">I", hdr, 16, len(body_bytes))
    struct.pack_into(">I", hdr, 20, crc32_ieee(bytes(hdr[:20])))
    return bytes(hdr) + body_bytes


class Session(object):
    """一条客户端连接。负责收发帧，并把推送帧缓存起来供后续按命令字取用。"""

    def __init__(self, host, port, tag):
        self.tag = tag
        self.seq = 0
        self.userid = 0
        self.sock = socket.create_connection((host, port), timeout=RECV_TIMEOUT_SECONDS)
        #服务端主动推送的帧（新消息通知、好友申请通知等）会夹在应答之间到达，
        #先缓存下来，等调用方按命令字来取
        self.pending = []

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise SmokeError("%s: 连接被服务端关闭（已收 %d/%d 字节）" % (self.tag, len(buf), n))
            buf += chunk
        return buf

    def _read_frame(self):
        hdr = self._recv_exact(FRAME_HEADER_LEN)
        magic = struct.unpack_from(">H", hdr, 0)[0]
        if magic != FRAME_MAGIC:
            raise SmokeError("%s: 帧头 magic 不对：0x%04X" % (self.tag, magic))

        flags = hdr[3]
        cmd = struct.unpack_from(">H", hdr, 4)[0]
        bodylen = struct.unpack_from(">I", hdr, 12)[0]
        body = self._recv_exact(bodylen) if bodylen > 0 else b""
        if flags & FRAME_FLAG_COMPRESSED:
            body = zlib.decompress(body)

        obj = {}
        if body:
            try:
                obj = json.loads(body.decode("utf-8"))
            except ValueError:
                obj = {"__raw__": repr(body)}
        return cmd, obj

    def send(self, cmd, body_obj):
        self.seq += 1
        body = json.dumps(body_obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(encode_frame(cmd, self.seq, body))

    def wait(self, cmd, timeout=RECV_TIMEOUT_SECONDS):
        """等一帧指定命令字的包；期间收到的其它帧进缓存。"""
        for i, (c, o) in enumerate(self.pending):
            if c == cmd:
                del self.pending[i]
                return o

        deadline = time.time() + timeout
        while time.time() < deadline:
            self.sock.settimeout(max(0.2, deadline - time.time()))
            try:
                c, o = self._read_frame()
            except socket.timeout:
                break
            if c == cmd:
                return o
            self.pending.append((c, o))

        raise SmokeError("%s: 等待 cmd=%d 超时，其间收到 %s"
                         % (self.tag, cmd, [c for c, _ in self.pending]))

    def request(self, cmd, body_obj):
        """发一个请求并等同命令字的应答。"""
        self.send(cmd, body_obj)
        return self.wait(cmd)


def check(cond, message):
    if not cond:
        raise SmokeError(message)


def check_ok(resp, what):
    """应答里 code 为 0 才算成功。"""
    check(isinstance(resp, dict), "%s: 应答不是 JSON 对象：%r" % (what, resp))
    check(resp.get("code") == 0, "%s: 失败，应答=%s" % (what, json.dumps(resp, ensure_ascii=False)))
    return resp


def sha256_hex(text):
    import hashlib
    return hashlib.sha256(text.encode("utf-8")).hexdigest().lower()


def read_sms_code(log_path, phone, since_offset):
    """从服务端日志里捞出刚下发给 phone 的验证码。

    短信下发是桩实现，验证码只写日志（见 ChatSession::onSendSmsCode），
    开发期本来就是这么取的。since_offset 之前的内容不看，避免捞到上一次的旧码。
    """
    pattern = re.compile(r"sms code requested, phone: %s, scene: \d+, code: (\d+)" % re.escape(phone))
    deadline = time.time() + SMS_CODE_WAIT_SECONDS
    while time.time() < deadline:
        if os.path.exists(log_path):
            with open(log_path, "rb") as f:
                f.seek(since_offset)
                text = f.read().decode("utf-8", "ignore")
            found = pattern.findall(text)
            if found:
                return found[-1]
        time.sleep(0.3)

    raise SmokeError("在日志 %s 里没找到手机号 %s 的验证码" % (log_path, phone))


def log_size(log_path):
    return os.path.getsize(log_path) if os.path.exists(log_path) else 0


def resolve_company(db_path, explicit):
    """定出注册时要用的公司全称。

    企业版的 cmd_register **必须**带 company，且要与 t_enterprise.f_name 完全一致
    （见 chatserversrc/RegisterProtocol.h）—— 服务端据它把新账号挂进该企业的根部门。
    账号有没有企业归属很关键：通讯录、在线状态广播、离线推送全都按企业范围投递，
    无企业归属的账号收不到任何关系链通知，用它做端到端回归等于什么都验不到。

      db_path  SQLite 库路径；没显式指定公司名时从这里取第一家企业。
      explicit 命令行显式给的公司全称；给了就直接用。
    返回：公司全称。定不出来时直接终止并说明原因。
    """
    if explicit:
        return explicit
    check(bool(db_path),
          "注册需要公司全称：请用 --company 指定，或用 --db 指定库文件以便自动取一家企业")
    import sqlite3
    con = sqlite3.connect(db_path)
    try:
        cur = con.cursor()
        cur.execute("SELECT f_name FROM t_enterprise ORDER BY f_enterprise_id LIMIT 1")
        row = cur.fetchone()
        check(row is not None and row[0],
              "库里一家企业都没有，无法注册测试账号：请先建企业或用 --company 指定")
        return row[0]
    finally:
        con.close()


def register_account(host, port, log_path, username, nickname, phone, password, company):
    """注册一个账号：先要验证码，再提交注册。用完即断开。

    company 是必填项，不是可选装饰 —— 缺了服务端 parseRegisterRequest 直接返回
    false，注册会被拒。取值见 resolve_company。
    """
    s = Session(host, port, "register-" + username)
    try:
        offset = log_size(log_path)
        check_ok(s.request(CMD_SEND_SMS_CODE, {"phone": phone, "scene": 1}),
                 "下发验证码(%s)" % phone)
        code = read_sms_code(log_path, phone, offset)

        resp = s.request(CMD_REGISTER, {
            "username": username,
            "nickname": nickname,
            "password": sha256_hex(password),
            "phone": phone,
            "smscode": code,
            "company": company,
        })
        check_ok(resp, "注册账号(%s)" % username)
        return code
    finally:
        s.close()


def login(host, port, account, password, tag):
    s = Session(host, port, tag)
    resp = s.request(CMD_LOGIN, {
        "clientversion": "3.2.1",
        "account": account,
        "password": sha256_hex(password),
        "clienttype": 1,
        "presence": 1,
        "deviceid": "E2E-" + tag,
        "devicename": "e2e-smoke",
    })
    check_ok(resp, "登录(%s)" % account)
    s.userid = int(resp["userid"])
    return s


def verify_database(db_path, expect):
    """直接查库核对数据确实落盘了 —— 应答说成功不等于真的写进去了。"""
    import sqlite3
    con = sqlite3.connect(db_path)
    try:
        cur = con.cursor()
        for sql, expected, what in expect:
            cur.execute(sql)
            actual = cur.fetchone()[0]
            check(actual == expected, "库内核对失败：%s，期望 %s 实际 %s（SQL: %s）"
                  % (what, expected, actual, sql))
            print("      %-34s = %s" % (what, actual))
    finally:
        con.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20000)
    parser.add_argument("--log", required=True, help="chatserver 标准输出的日志文件，用于取短信验证码")
    parser.add_argument("--db", default="", help="SQLite 库文件路径；给了就在跑完后查库核对，"
                                                 "同时用来自动取一家企业作注册用的公司名")
    parser.add_argument("--company", default="", help="注册时用的公司全称，须与 t_enterprise.f_name "
                                                      "完全一致；不给则从 --db 里自动取第一家")
    args = parser.parse_args()

    alice_pwd = "alice@1234"
    bob_pwd = "bob@12345"
    sessions = []

    try:
        #两个账号必须落进<u>同一家</u>企业：通讯录、在线状态广播、离线推送都是按企业范围
        #投递的，不同企业（或没有企业）就互相看不见，后面几步全都验不到东西。
        company = resolve_company(args.db, args.company)
        print("[1/9] 注册两个账号（公司：%s）" % company)
        register_account(args.host, args.port, args.log, "alice001", "爱丽丝", "13900000001", alice_pwd, company)
        register_account(args.host, args.port, args.log, "bob00001", "鲍勃", "13900000002", bob_pwd, company)
        print("      ok")

        print("[2/9] 两个账号分别登录")
        alice = login(args.host, args.port, "alice001", alice_pwd, "alice")
        sessions.append(alice)
        bob = login(args.host, args.port, "bob00001", bob_pwd, "bob")
        sessions.append(bob)
        print("      alice userid=%d, bob userid=%d" % (alice.userid, bob.userid))

        #好友关系子系统已于 2026-08-05 在企业版中删除（1203~1208 等协议号作废），
        #这一步改成验证接替它的联系人备注名（1209 → t_contact_remark）。
        #备注名里刻意带一个单引号，顺带把 SQL 转义这条路也走一遍。
        print("[3/9] alice 给 bob 设联系人备注名")
        remark_text = "鲍勃 O'Brien"
        resp = check_ok(alice.request(CMD_SET_FRIEND_REMARK, {
            "userid": str(bob.userid), "remark": remark_text}),
            "设置联系人备注名")
        check(str(resp.get("userid")) == str(bob.userid),
              "备注名应答未原样回带 userid：%s" % json.dumps(resp, ensure_ascii=False))
        print("      remark=%s" % remark_text)

        print("[4/9] alice 拉取通讯录，并核对上一步的备注名能读回来")
        contacts = check_ok(alice.request(CMD_GET_CONTACTS, {}), "拉取通讯录")
        entries = contacts.get("contacts", [])
        ids = [str(c.get("userid")) for c in entries]
        #企业版的通讯录就是企业组织架构（协议规范见 docs/协议设计-v2.md 的
        #cmd_get_contacts 一节：contacts 是「部门成员与服务号」的资料池）。两个账号
        #注册时挂进了同一家企业的根部门，所以 alice 一定能在通讯录里看到 bob。
        check(str(bob.userid) in ids,
              "通讯录里没有 bob（二人应属同一企业）：实际 userid 列表=%s" % ids)

        #这一条才是备注名功能的<u>读回路径</u>验证：上一步设的备注，必须能随通讯录
        #原样带回来。备注不走推送，客户端重登后就是靠这个字段恢复显示的 ——
        #以前这里因为账号没有企业归属、通讯录恒为空，等于设完从来没验证过能不能读回。
        bob_entry = next(c for c in entries if str(c.get("userid")) == str(bob.userid))
        check(bob_entry.get("remark") == remark_text,
              "通讯录里 bob 的 remark 与设置的不一致：期望 %r 实际 %r"
              % (remark_text, bob_entry.get("remark")))
        print("      bob 在通讯录里，remark=%s（含单引号，转义与落库都正确）"
              % bob_entry.get("remark"))

        #反向再确认一次单向性：备注是"alice 给 bob 起的名字"，不该出现在 bob 那一侧。
        bob_contacts = check_ok(bob.request(CMD_GET_CONTACTS, {}), "bob 拉取通讯录")
        alice_entry = next((c for c in bob_contacts.get("contacts", [])
                            if str(c.get("userid")) == str(alice.userid)), None)
        check(alice_entry is not None, "bob 的通讯录里没有 alice")
        check(not alice_entry.get("remark"),
              "备注名不该是双向的：bob 看 alice 竟然也有备注 %r" % alice_entry.get("remark"))
        print("      bob 看 alice 无备注 —— 备注是单向的，符合预期")
        print("      contacts=%d, groups=%d" % (len(ids), len(contacts.get("groups", []))))

        print("[5/9] alice 给 bob 发单聊消息，bob 应收到推送")
        text = "端到端冒烟：单聊内容含单引号 ' 与反斜杠 \\ 与中文"
        resp = check_ok(alice.request(CMD_SEND_MESSAGE, {
            "clientmsgid": "e2e-single-0001",
            "ctype": CTYPE_SINGLE,
            "cid": str(bob.userid),
            "msgtype": MSGTYPE_TEXT,
            "content": {"text": text},
        }), "发送单聊消息")
        single_msgid = int(resp["msgid"])
        notify = bob.wait(CMD_MESSAGE_NOTIFY)
        check(notify.get("content", {}).get("text") == text,
              "bob 收到的消息内容与发送的不一致：%s" % json.dumps(notify, ensure_ascii=False))
        print("      msgid=%d，内容原样送达（含引号/反斜杠/中文）" % single_msgid)

        print("[6/9] alice 同步会话列表与消息记录")
        convs = check_ok(alice.request(CMD_SYNC_CONVERSATIONS, {}), "同步会话列表")
        cids = [str(c.get("cid")) for c in convs.get("conversations", [])]
        check(str(bob.userid) in cids, "会话列表里没有与 bob 的会话，实际=%s" % cids)

        msgs = check_ok(alice.request(CMD_SYNC_MESSAGES, {
            "ctype": CTYPE_SINGLE, "cid": str(bob.userid), "beforemsgid": "0", "limit": 20}),
            "同步消息记录")
        got = [str(m.get("msgid")) for m in msgs.get("messages", [])]
        check(str(single_msgid) in got, "消息记录里没有刚发的那条，实际=%s" % got)

        #这条断言分量最重：内容是从数据库读回来的，一来一回完整经过了
        #escapeString 转义 → 落库 → 查询 → 反序列化。转义规则若按 MySQL 那套写，
        #单引号会让 SQL 提前截断、反斜杠会多出一个，这里立刻就能看出来
        stored = None
        for msg in msgs.get("messages", []):
            if str(msg.get("msgid")) == str(single_msgid):
                stored = msg.get("content", {}).get("text")
        check(stored == text, "从库里读回的消息内容与发送的不一致：%r vs %r" % (stored, text))
        print("      会话数=%d，拉回消息数=%d，内容与发送时逐字一致" % (len(cids), len(got)))

        print("[7/9] alice 上报已读位点")
        check_ok(alice.request(CMD_MARK_READ, {
            "ctype": CTYPE_SINGLE, "cid": str(bob.userid), "readmsgid": str(single_msgid)}),
            "上报已读位点")
        print("      ok")

        print("[8/9] alice 建群并拉 bob 入群，然后发一条群消息")
        resp = check_ok(alice.request(CMD_CREATE_GROUP, {
            "groupname": "端到端测试群", "memberids": [str(bob.userid)]}), "创建群聊")
        groupid = int(resp["groupid"])
        resp = check_ok(alice.request(CMD_SEND_MESSAGE, {
            "clientmsgid": "e2e-group-0001",
            "ctype": CTYPE_GROUP,
            "cid": str(groupid),
            "msgtype": MSGTYPE_TEXT,
            "content": {"text": "群里的第一条消息"},
        }), "发送群聊消息")
        group_msgid = int(resp["msgid"])
        notify = bob.wait(CMD_MESSAGE_NOTIFY)
        check(str(notify.get("cid")) == str(groupid),
              "bob 收到的群消息 cid 不对：%s" % json.dumps(notify, ensure_ascii=False))
        print("      groupid=%d, msgid=%d" % (groupid, group_msgid))

        print("[9/9] 直接查库核对数据确实落盘")
        if args.db:
            verify_database(args.db, [
                ("SELECT COUNT(*) FROM t_user", 2, "t_user 行数"),
                ("SELECT COUNT(*) FROM t_chatmsg", 2, "t_chatmsg 行数"),
                ("SELECT COUNT(*) FROM t_group", 1, "t_group 行数"),
                ("SELECT COUNT(*) FROM t_group_member", 2, "t_group_member 行数"),
                ("SELECT COUNT(*) FROM t_contact_remark", 1, "t_contact_remark 行数"),
                #备注名里的单引号必须原样落库：转义时翻倍的那个应当已被 SQLite 消费掉
                ("SELECT COUNT(*) FROM t_contact_remark WHERE f_remark LIKE '%O''Brien'", 1,
                 "备注名里的单引号原样落库"),
                ("SELECT COUNT(*) FROM t_conversation WHERE f_readmsgid > 0", 1,
                 "t_conversation 已读位点行数"),
                #f_content 里存的是 jsoncpp 序列化结果，中文会被转成 \uXXXX，
                #所以不能拿中文字面量去 LIKE。这里只核对转义相关的两个字节：
                #单引号必须原样只有一个（转义时翻倍的那个应当已被 SQLite 消费掉），
                #反斜杠必须只有 JSON 自身那一层（按 MySQL 规则转义会多出一个）
                ("SELECT COUNT(*) FROM t_chatmsg WHERE f_content LIKE '%\\u53f7 '' %'", 1,
                 "单引号原样落库（未被转义符污染）"),
                ("SELECT COUNT(*) FROM t_chatmsg WHERE f_content LIKE '%\\u6760 \\\\ %'", 1,
                 "反斜杠只保留 JSON 自身那一层"),
            ])
        else:
            print("      跳过（未指定 --db）")

        print()
        print("全部通过")
        return 0

    except SmokeError as e:
        print()
        print("失败：%s" % e)
        return 1
    finally:
        for s in sessions:
            s.close()


if __name__ == "__main__":
    sys.exit(main())
