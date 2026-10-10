# -*- coding: utf-8 -*-
"""
生成音视频呼叫提示音（flamingoclient/Debug/Sound 与 flamingoclient/Release/Sound 下的 call_*.wav）。balloonwj@qq.com 2026-10-05

五个文件：
  call_incoming.wav   来电铃声（循环播放）
  call_outgoing.wav   呼出等待对方接听的回铃（循环播放）
  call_connected.wav  接通（一次）
  call_ended.wav      挂断 / 通话结束（一次）
  call_busy.wav       对方忙、拒绝、无人接听、不在线（一次）
一对一通话与群通话共用，播放方式见 Source/UI/BalloonMain/NotificationSound。

脚本里有三套音色：A_mallet（柔和木琴）、B_electronic（清亮电子）、C_classic（经典电话铃）。2026-10-05 用户试听后选定
A_mallet，默认只生成这一套，同时写进 Debug/Sound 与 Release/Sound（2026-10-09 起两个运行目录各有一份入库的资源，原先
写进已删除的 Bin/Sound）；要再比较几套时用 --all 生成到别的目录，每套另带一个依次播放五个声音的 preview_all.wav。

格式：单声道、16 位、44100 Hz PCM；峰值 -3 dBFS；每个音符都有起音与衰减包络，整段首尾 5 毫秒淡入淡出，
循环播放（SND_LOOP）时接缝处不出爆音。依赖 numpy。

  python flamingoclient/tools/gen_call_sounds.py              把 A_mallet 写进 flamingoclient/Debug/Sound 与 Release/Sound
  python flamingoclient/tools/gen_call_sounds.py --all <目录>  三套都生成到 <目录>/<套名>/，各带 preview_all.wav
"""
import os
import sys
import wave

import numpy as np

RATE = 44100
PEAK = 10 ** (-3.0 / 20)          # 峰值 -3 dBFS
EDGE_FADE_S = 0.005               # 整段首尾淡入淡出时长（秒）
# 默认输出目录：两个运行目录 flamingoclient/Debug/Sound 与 flamingoclient/Release/Sound（本脚本在 flamingoclient/tools/ 下），
# 两份内容相同，都入库
SOUND_DIRS = [os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", d, "Sound"))
              for d in ("Debug", "Release")]
# 选定的一套（2026-10-05 用户试听后选定）
CHOSEN_STYLE = "A_mallet"

# 十二平均律音高（Hz）
NOTE = {
    "A3": 220.00, "C4": 261.63, "E4": 329.63, "G4": 392.00, "A4": 440.00,
    "C5": 523.25, "D5": 587.33, "E5": 659.25, "G5": 783.99, "A5": 880.00,
    "C6": 1046.50, "E6": 1318.51,
}


def silence(seconds):
    return np.zeros(int(RATE * seconds))


def place(track, clip, start_s):
    """把 clip 叠加到 track 的 start_s 秒处，track 不够长时自动延长。"""
    start = int(RATE * start_s)
    end = start + len(clip)
    if end > len(track):
        track = np.concatenate([track, np.zeros(end - len(track))])
    track[start:end] += clip
    return track


def finish(track):
    """归一化到 -3 dBFS，首尾淡入淡出。"""
    peak = np.max(np.abs(track))
    if peak > 0:
        track = track / peak * PEAK
    n = int(RATE * EDGE_FADE_S)
    if len(track) > 2 * n:
        ramp = np.linspace(0.0, 1.0, n)
        track[:n] *= ramp
        track[-n:] *= ramp[::-1]
    return track


def write_wav(path, track):
    data = np.clip(track, -1.0, 1.0)
    pcm = (data * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


# ---------------------------------------------------------------- 音色

def mallet(freq, length=0.6, decay=0.22):
    """木琴 / 马林巴式的木质敲击音：基音加 4 倍、9.2 倍泛音，高泛音衰减更快，起音 3 毫秒。"""
    t = np.arange(int(RATE * length)) / RATE
    attack = np.minimum(t / 0.003, 1.0)
    tone = (1.00 * np.sin(2 * np.pi * freq * t) * np.exp(-t / decay)
            + 0.35 * np.sin(2 * np.pi * freq * 4.0 * t) * np.exp(-t / (decay * 0.30))
            + 0.12 * np.sin(2 * np.pi * freq * 9.2 * t) * np.exp(-t / (decay * 0.12)))
    # 截断处还有余音：末尾 30 毫秒淡出，免得每个音符结束时「咔」一声
    tail = np.ones_like(t)
    r = int(RATE * 0.03)
    tail[-r:] = np.linspace(1.0, 0.0, r)
    return tone * attack * tail


def beep(freq, length, attack=0.010, release=0.060, harmonic=0.25):
    """电子提示音：正弦加少量二次谐波，带起音与释音，中段平稳。"""
    t = np.arange(int(RATE * length)) / RATE
    env = np.ones_like(t)
    a = int(RATE * attack)
    r = int(RATE * release)
    if a > 0:
        env[:a] = np.linspace(0.0, 1.0, a)
    if r > 0:
        env[-r:] = np.minimum(env[-r:], np.linspace(1.0, 0.0, r))
    tone = np.sin(2 * np.pi * freq * t) + harmonic * np.sin(2 * np.pi * freq * 2 * t)
    return tone * env


def bell_trill(length, base=700.0, trill_hz=20.0):
    """老式电话机的电铃：几个不成谐波关系的分音（铃碗的振动模式），被约 20 Hz 的小锤反复敲击形成「叮铃铃」。"""
    t = np.arange(int(RATE * length)) / RATE
    partials = (1.00, 2.76, 5.40, 8.93)
    weights = (1.00, 0.55, 0.30, 0.15)
    tone = sum(w * np.sin(2 * np.pi * base * p * t) for p, w in zip(partials, weights))
    # 每次敲击后铃声快速衰减一点，形成颤动
    phase = (t * trill_hz) % 1.0
    strike = 0.55 + 0.45 * np.exp(-phase * 6.0)
    env = np.ones_like(t)
    r = int(RATE * 0.08)
    env[-r:] = np.linspace(1.0, 0.0, r)
    return tone * strike * env


def dual_tone(f1, f2, length, release=0.02):
    """两个频率叠加的持续音（电话网的回铃、忙音那一类）。"""
    t = np.arange(int(RATE * length)) / RATE
    env = np.ones_like(t)
    r = int(RATE * release)
    env[:r] = np.linspace(0.0, 1.0, r)
    env[-r:] = np.linspace(1.0, 0.0, r)
    tone = np.sin(2 * np.pi * f1 * t)
    if f2 > 0:
        tone = tone + np.sin(2 * np.pi * f2 * t)
    return tone * env


# ---------------------------------------------------------------- 三套声音

def style_mallet():
    s = {}
    # 来电：上行四音 C5 E5 G5 C6 奏两遍，之后留白 1 秒，整段约 2.6 秒一圈
    tr = silence(0)
    motif = ("C5", "E5", "G5", "C6")
    for rep in range(2):
        for i, n in enumerate(motif):
            tr = place(tr, mallet(NOTE[n]), rep * 0.80 + i * 0.16)
    s["call_incoming.wav"] = np.concatenate([tr, silence(2.6 - len(tr) / RATE)])
    # 回铃：G4、C5 两个轻音，之后留白，整段 3.5 秒一圈
    tr = silence(0)
    tr = place(tr, 0.7 * mallet(NOTE["G4"], 0.9, 0.30), 0.00)
    tr = place(tr, 0.7 * mallet(NOTE["C5"], 0.9, 0.30), 0.30)
    s["call_outgoing.wav"] = np.concatenate([tr, silence(3.5 - len(tr) / RATE)])
    # 接通：上行双音 G5 → C6
    tr = place(silence(0), mallet(NOTE["G5"], 0.5), 0.0)
    s["call_connected.wav"] = place(tr, mallet(NOTE["C6"], 0.5), 0.12)
    # 挂断：下行双音 C6 → G5
    tr = place(silence(0), mallet(NOTE["C6"], 0.5), 0.0)
    s["call_ended.wav"] = place(tr, mallet(NOTE["G5"], 0.5), 0.14)
    # 忙 / 无人接听：低音 A3 三连
    tr = silence(0)
    for i in range(3):
        tr = place(tr, mallet(NOTE["A3"], 0.45, 0.15), i * 0.26)
    s["call_busy.wav"] = tr
    return s


def style_electronic():
    s = {}
    # 来电：A5 / E6 交替四次（每音 0.11 秒），停 0.9 秒，整段约 1.9 秒一圈
    tr = silence(0)
    for i in range(8):
        f = NOTE["A5"] if i % 2 == 0 else NOTE["E6"]
        tr = place(tr, beep(f, 0.11, 0.005, 0.03), i * 0.125)
    s["call_incoming.wav"] = np.concatenate([tr, silence(1.9 - len(tr) / RATE)])
    # 回铃：E5 柔和长音 0.45 秒两次，之后留白，整段 3 秒一圈
    tr = silence(0)
    tr = place(tr, 0.6 * beep(NOTE["E5"], 0.45, 0.04, 0.12, 0.1), 0.0)
    tr = place(tr, 0.6 * beep(NOTE["E5"], 0.45, 0.04, 0.12, 0.1), 0.6)
    s["call_outgoing.wav"] = np.concatenate([tr, silence(3.0 - len(tr) / RATE)])
    # 接通：A5 → E6 上行
    tr = place(silence(0), beep(NOTE["A5"], 0.10, 0.005, 0.03), 0.0)
    s["call_connected.wav"] = place(tr, beep(NOTE["E6"], 0.16, 0.005, 0.08), 0.11)
    # 挂断：E6 → A5 下行
    tr = place(silence(0), beep(NOTE["E6"], 0.10, 0.005, 0.03), 0.0)
    s["call_ended.wav"] = place(tr, beep(NOTE["A5"], 0.18, 0.005, 0.10), 0.11)
    # 忙：480 Hz 短音三下
    tr = silence(0)
    for i in range(3):
        tr = place(tr, beep(480.0, 0.22, 0.01, 0.04, 0.15), i * 0.40)
    s["call_busy.wav"] = tr
    return s


def style_classic():
    s = {}
    # 来电：电铃「叮铃铃」响 1 秒、停 0.4 秒、再响 1 秒，之后停 2 秒，整段 4.4 秒一圈
    tr = silence(0)
    tr = place(tr, bell_trill(1.0), 0.0)
    tr = place(tr, bell_trill(1.0), 1.4)
    s["call_incoming.wav"] = np.concatenate([tr, silence(4.4 - len(tr) / RATE)])
    # 回铃：电话网的 450 Hz 长音，响 1 秒、停 4 秒
    tr = 0.5 * dual_tone(450.0, 0.0, 1.0)
    s["call_outgoing.wav"] = np.concatenate([tr, silence(4.0)])
    # 接通：一声短促的 1000 Hz「嘀」
    s["call_connected.wav"] = 0.6 * dual_tone(1000.0, 0.0, 0.18, 0.01)
    # 挂断：450 Hz 短音两下（电话网忙音节奏，比忙音少一下）
    tr = silence(0)
    for i in range(2):
        tr = place(tr, 0.6 * dual_tone(450.0, 0.0, 0.35), i * 0.70)
    s["call_ended.wav"] = tr
    # 忙：450 Hz 响 0.35 秒、停 0.35 秒，四下
    tr = silence(0)
    for i in range(4):
        tr = place(tr, 0.6 * dual_tone(450.0, 0.0, 0.35), i * 0.70)
    s["call_busy.wav"] = tr
    return s


ORDER = ("call_incoming.wav", "call_outgoing.wav", "call_connected.wav", "call_ended.wav", "call_busy.wav")
LOOPED = ("call_incoming.wav", "call_outgoing.wav")


# 全部三套的名字，--all 时依次生成
STYLES = ("A_mallet", "B_electronic", "C_classic")


def builders():
    return {"A_mallet": style_mallet, "B_electronic": style_electronic, "C_classic": style_classic}


def write_style(name, folder, with_preview):
    """生成一套声音写到 folder；with_preview 为 True 时另写 preview_all.wav。"""
    os.makedirs(folder, exist_ok=True)
    sounds = {k: finish(v) for k, v in builders()[name]().items()}
    preview = silence(0.3)
    for key in ORDER:
        write_wav(os.path.join(folder, key), sounds[key])
        print("  %-14s %-20s %.2f s" % (name, key, len(sounds[key]) / RATE))
        times = 2 if key in LOOPED else 1
        for _ in range(times):
            preview = np.concatenate([preview, sounds[key]])
        preview = np.concatenate([preview, silence(0.8)])
    if with_preview:
        write_wav(os.path.join(folder, "preview_all.wav"), preview)
        print("  %-14s %-20s %.2f s" % (name, "preview_all.wav", len(preview) / RATE))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--all":
        for name in STYLES:
            write_style(name, os.path.join(sys.argv[2], name), True)
        return
    for sound_dir in SOUND_DIRS:
        write_style(CHOSEN_STYLE, sound_dir, False)
        print("written to", sound_dir)


if __name__ == "__main__":
    main()
