/**
 *  取光标位置与逻辑按键，说明见 ShareCursorSampler.h。代码由 ShareHostStreamer 的 sampleCursor 挪来（第 16 期 16d），
 *  共享的推流线程与独立录屏的录制线程共用。
 *  balloonwj@qq.com   2026-10-04
 */
#include "ShareCursorSampler.h"

#include <stdio.h>    // fopen_s、fscanf_s（读模拟光标文件，Debug）
#include <stdlib.h>   // getenv_s（Debug）
#include "ShareMouseEffects.h"   // LogicalMouseButtons
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//GetAsyncKeyState 返回值里「当前按着」的位
const SHORT kKeyDownBit = static_cast<SHORT>(0x8000);
//Debug 版模拟光标：环境变量名、读文件的最短间隔（毫秒）、路径缓冲区长度
const char kFakeCursorEnv[] = "FLAMINGO_SHARE_FAKE_CURSOR_FILE";
const int64_t kFakeCursorPollMs = 50;
const size_t kFakeCursorPathLen = 520;
//文件里至少要有的项数（x、y），以及带上左键、右键时的项数
const int kFakeCursorMinFields = 2;
const int kFakeCursorLeftFields = 3;
const int kFakeCursorRightFields = 4;

} // namespace

ShareCursorSampler::ShareCursorSampler()
    : m_readMs(0)
    , m_fakeLeft(false)
    , m_fakeRight(false)
    , m_everRead(false)
{
    m_fakePt.x = 0;
    m_fakePt.y = 0;
#ifdef _DEBUG
    char path[kFakeCursorPathLen] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, path, sizeof(path), kFakeCursorEnv) == 0 && len > 1)
    {
        m_fakeFile = path;
        LOG_INFO("screenshare: fake cursor from %s.", path);
    }
#endif
}

bool ShareCursorSampler::IsFake() const
{
    return !m_fakeFile.empty();
}

void ShareCursorSampler::Sample(POINT& pt, bool& left, bool& right, int64_t nowMs)
{
    pt.x = 0;
    pt.y = 0;
    left = false;
    right = false;
    if (!m_fakeFile.empty())
    {
        //模拟光标（Debug 联调用）：文件内容为「x y 左键 右键」，后两项 1 表示按着；读不到时沿用上一次的值
        if (!m_everRead || nowMs - m_readMs >= kFakeCursorPollMs)
        {
            m_readMs = nowMs;
            FILE* fp = NULL;
            if (fopen_s(&fp, m_fakeFile.c_str(), "r") == 0 && fp != NULL)
            {
                int x = 0;
                int y = 0;
                int l = 0;
                int r = 0;
                const int n = fscanf_s(fp, "%d %d %d %d", &x, &y, &l, &r);
                fclose(fp);
                if (n >= kFakeCursorMinFields)
                {
                    m_fakePt.x = x;
                    m_fakePt.y = y;
                    m_fakeLeft = (n >= kFakeCursorLeftFields && l != 0);
                    m_fakeRight = (n >= kFakeCursorRightFields && r != 0);
                    m_everRead = true;
                }
            }
        }
        pt = m_fakePt;
        left = m_fakeLeft;
        right = m_fakeRight;
        return;
    }
    ::GetCursorPos(&pt);
    //GetAsyncKeyState 读的是物理按键，按系统的「切换主次按钮」设置换算成用户感知的左右键
    const bool physicalLeft = (::GetAsyncKeyState(VK_LBUTTON) & kKeyDownBit) != 0;
    const bool physicalRight = (::GetAsyncKeyState(VK_RBUTTON) & kKeyDownBit) != 0;
    LogicalMouseButtons(physicalLeft, physicalRight, ::GetSystemMetrics(SM_SWAPBUTTON) != 0, left, right);
}

} // namespace screenshare
