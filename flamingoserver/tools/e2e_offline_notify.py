# -*- coding: utf-8 -*-
"""
下线通知的回归测试（针对 ChatServer::onDisconnected 自我死锁崩溃）
balloonwj@qq.com   2026-08-03

背景：修复前，只要「下线用户至少有一个同事在线」，chatserver 就会必崩。
原因是 ChatServer::onDisconnected 在持有 m_sessionMutex 期间调用
ChatSession::sendUserStatusChangeMsg，后者内部又调 getUserClientTypeByUserId
再加一次同一把非递归锁，直接抛 resource_deadlock_would_occur 并终止进程。

本脚本就是把那个触发条件原样跑一遍，逐项验证：
  1. 两人都在线时，一方正常断开，服务端不崩；
  2. 在线的那一方能收到对方的 cmd_presence_changed_notify（presence=0）；
  3. 换成 RST 异常断开（客户端进程被杀 / 网线拔掉那种），服务端同样不崩；
  4. 反方向再来一次（换另一个人断开），确认不是单向侥幸；
  5. 全程结束后服务端仍能正常应答新请求。

复用 e2e_sqlite_smoke.py 的协议实现（帧编解码、注册、登录等），只在这里
补充下线场景需要的判定。

【2026-08-05 企业版改造后的变化】

原先第 2 步是「双方互加好友」，靠好友关系把两人关联起来。企业版已删除好友关系
子系统（加好友 1203 / 处理申请 1205 等全部移除），该步骤连同它引用的
e2e.CMD_ADD_FRIEND_REQUEST 等常量一并删掉 —— 死锁的触发条件是 onDisconnected
走到广播分支，与两人之间有没有关系链无关，双方都登录着即可。

关联改由**企业归属**承担：两个账号注册进同一家企业，离线推送就走
ChatServer::onDisconnected 里的 broadcastToEnterprise 投到对方。这比原先靠好友
关系更贴近生产 —— 企业版真实用户之间本来就是靠企业范围互相看到在线状态的。
注意该广播明确跳过 enterpriseId == 0 的账号（平台超管、群账号），所以注册时
**必须**带上公司全称，见 e2e_sqlite_smoke.resolve_company。

用法：
    python tools/e2e_offline_notify.py --port 20100 --log 路径/chatserver.out
"""
import argparse
import importlib.util
import json
import os
import socket
import struct
import sys
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))

# 复用冒烟脚本里的协议实现，避免同一套帧格式写两遍慢慢走样
_spec = importlib.util.spec_from_file_location(
    "e2e_sqlite_smoke", os.path.join(HERE, "e2e_sqlite_smoke.py"))
e2e = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(e2e)

# 推送：某联系人在线状态发生变更（见 chatserversrc/Protocol.h 的 cmd_presence_changed_notify）
CMD_PRESENCE_CHANGED_NOTIFY = 1109

# presence 字段里表示「离线」的取值（见 ProfileProtocol::buildPresenceChangedNotify 调用方）
PRESENCE_OFFLINE = 0

# 等待下线推送的超时（秒）。下线推送是服务端在断开处理里同步发出的，正常都在毫秒级；
# 给到 5 秒是留足调试版本在慢盘上的余量。
NOTIFY_TIMEOUT_SECONDS = 5

# 断开之后、判定服务端存活之前的等待（秒）。给服务端处理完断开事件的时间。
SETTLE_SECONDS = 1.0


class RegressionError(Exception):
    """回归测试失败"""
    pass


def check(cond, message):
    if not cond:
        raise RegressionError(message)


def server_alive(host, port):
    """服务端是否还能正常干活。

    只判断端口能否连上是不够的：进程 abort 之后在调试版本上会弹窗卡住，端口往往
    还挂着监听。所以这里补发一次真实请求，能收到应答才算活着。

    返回：(是否存活, 说明文字)
    """
    try:
        s = e2e.Session(host, port, "probe")
    except Exception as exc:
        return False, u"连接失败：%s" % exc

    try:
        s.send(e2e.CMD_SEND_SMS_CODE, {"phone": "13900000000", "scene": 1})
        s.sock.settimeout(NOTIFY_TIMEOUT_SECONDS)
        cmd, obj = s._read_frame()
        return obj.get("code") == 0, u"应答 code=%s" % obj.get("code")
    except Exception as exc:
        return False, u"收不到应答：%s" % exc
    finally:
        s.close()


def expect_offline_notify(watcher, offline_userid):
    """在 watcher 这条会话上等对方的离线推送，校验 userid 与 presence。

    watcher        观察方的会话（保持在线的那一个）
    offline_userid 期望在推送里出现的下线用户 ID

    返回：收到的推送 JSON 对象。
    """
    notify = watcher.wait(CMD_PRESENCE_CHANGED_NOTIFY, timeout=NOTIFY_TIMEOUT_SECONDS)
    check(str(notify.get("userid")) == str(offline_userid),
          u"离线推送里的 userid 不对：期望 %s，实际 %s"
          % (offline_userid, json.dumps(notify, ensure_ascii=False)))
    check(notify.get("presence") == PRESENCE_OFFLINE,
          u"离线推送里的 presence 不是 0：%s" % json.dumps(notify, ensure_ascii=False))
    return notify


def abrupt_close(session):
    """把连接改成发 RST 后直接关闭，模拟客户端进程被杀 / 断网这类异常掉线。

    正常 close 走的是 FIN，服务端从 handleClose 进 onDisconnected；RST 走的是
    handleError，路径不同，所以两种都要测。
    """
    session.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack("ii", 1, 0))
    session.close()


def main():
    parser = argparse.ArgumentParser(
        description=u"下线通知回归测试（onDisconnected 死锁崩溃）")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20100)
    parser.add_argument("--log", required=True,
                        help=u"chatserver 标准输出文件，用于抓注册短信验证码")
    parser.add_argument("--suffix", default="01",
                        help=u"账号后缀，重复跑时换一个，避免撞用户名")
    parser.add_argument("--db", default="",
                        help=u"SQLite 库文件路径，用来自动取一家企业作注册用的公司名")
    parser.add_argument("--company", default="",
                        help=u"注册时用的公司全称，须与 t_enterprise.f_name 完全一致；"
                             u"不给则从 --db 里自动取第一家")
    args = parser.parse_args()

    alice_name = "off_a%s" % args.suffix
    bob_name = "off_b%s" % args.suffix
    alice_pwd = "alice@1234"
    bob_pwd = "bob@12345"
    alice_phone = "1390000%s01" % args.suffix
    bob_phone = "1390000%s02" % args.suffix

    # 两个账号必须落进<u>同一家</u>企业：离线推送走 broadcastToEnterprise 按企业范围
    # 投递，不同企业（或没有企业归属）就互相收不到，第 2 项断言无从验起。
    company = e2e.resolve_company(args.db, args.company)
    print(u"[1/6] 注册两个账号（公司：%s）" % company)
    e2e.register_account(args.host, args.port, args.log,
                         alice_name, u"爱丽丝", alice_phone, alice_pwd, company)
    e2e.register_account(args.host, args.port, args.log,
                         bob_name, u"鲍勃", bob_phone, bob_pwd, company)
    print(u"      ok")

    print(u"[2/6] 双方登录")
    # 企业版删除好友关系后，这里不再需要"互加好友"这一步：死锁的触发条件是
    # onDisconnected 走到广播分支，与两人之间有没有关系链无关，双方都在线即可；
    # 而"对方能收到离线推送"这一条由上面的同企业注册来保证。
    alice = e2e.login(args.host, args.port, alice_name, alice_pwd, "alice")
    bob = e2e.login(args.host, args.port, bob_name, bob_pwd, "bob")
    print(u"      alice userid=%d, bob userid=%d, 均已登录同一企业"
          % (alice.userid, bob.userid))

    print(u"[3/6] bob 正常断开（FIN），alice 在线 —— 这就是原先必崩的场景")
    bob.close()
    notify = expect_offline_notify(alice, bob.userid)
    print(u"      alice 收到离线推送: %s" % json.dumps(notify, ensure_ascii=False))
    time.sleep(SETTLE_SECONDS)
    ok, detail = server_alive(args.host, args.port)
    check(ok, u"bob 正常断开后服务端已不工作（%s）" % detail)
    print(u"      服务端存活（%s）" % detail)

    print(u"[4/6] bob 重新登录后 RST 异常断开，alice 仍在线")
    bob = e2e.login(args.host, args.port, bob_name, bob_pwd, "bob2")
    # 上线也会给好友推一条 presence 通知，先收掉，免得跟后面的离线推送混起来
    try:
        alice.wait(CMD_PRESENCE_CHANGED_NOTIFY, timeout=NOTIFY_TIMEOUT_SECONDS)
    except Exception:
        # 上线推送目前只按企业广播，非企业用户可能收不到；收不到不影响本用例
        pass
    abrupt_close(bob)
    notify = expect_offline_notify(alice, bob.userid)
    print(u"      alice 收到离线推送: %s" % json.dumps(notify, ensure_ascii=False))
    time.sleep(SETTLE_SECONDS)
    ok, detail = server_alive(args.host, args.port)
    check(ok, u"bob 异常断开后服务端已不工作（%s）" % detail)
    print(u"      服务端存活（%s）" % detail)

    print(u"[5/6] 反方向：bob 在线，alice 断开")
    bob = e2e.login(args.host, args.port, bob_name, bob_pwd, "bob3")
    alice_userid = alice.userid
    alice.close()
    notify = expect_offline_notify(bob, alice_userid)
    print(u"      bob 收到离线推送: %s" % json.dumps(notify, ensure_ascii=False))
    time.sleep(SETTLE_SECONDS)
    ok, detail = server_alive(args.host, args.port)
    check(ok, u"alice 断开后服务端已不工作（%s）" % detail)
    print(u"      服务端存活（%s）" % detail)

    print(u"[6/6] 收尾：最后一条连接也断开，服务端仍应正常")
    bob.close()
    time.sleep(SETTLE_SECONDS)
    ok, detail = server_alive(args.host, args.port)
    check(ok, u"全部断开后服务端已不工作（%s）" % detail)
    print(u"      服务端存活（%s）" % detail)

    print()
    print(u"全部通过")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RegressionError, e2e.SmokeError) as exc:
        print()
        print(u"失败: %s" % exc)
        sys.exit(1)
