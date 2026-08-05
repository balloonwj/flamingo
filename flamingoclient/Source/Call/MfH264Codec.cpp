/**
 *  H.264 视频编解码（Media Foundation MFT）实现，MfH264Codec.cpp
 *  balloonwj@qq.com   2026-06-15
 *
 *  说明：MS 自带的 H.264 编码器 / 解码器 MFT 由调用方分配输出样本（不置
 *  MFT_OUTPUT_STREAM_PROVIDES_SAMPLES），故本实现统一自行分配输出缓冲；解码器在首个
 *  ProcessOutput 返回 MF_E_TRANSFORM_STREAM_CHANGE 时再据码流协商出 NV12 输出类型与尺寸。
 */
#include "MfH264Codec.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>

#include "../IULog.h"   //须在 windows.h 之后

#include <string.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace call {

//一秒的 100ns 计数（帧时长 = 此值 / fps）。
static const int64_t kHnsPerSecond = 10000000LL;

//给 MFT 设 MF_LOW_LATENCY 属性：让 MS 编/解码器不为帧重排序（B 帧 / DPB）缓冲多帧，
//实时通话下显著降低首帧 / 端到端延迟。须在配置类型 / 开始流之前设。失败不致命。
static void setMftLowLatency(IMFTransform* mft)
{
    IMFAttributes* attr = NULL;
    if (SUCCEEDED(mft->GetAttributes(&attr)) && attr != NULL)
    {
        attr->SetUINT32(MF_LOW_LATENCY, TRUE);
        attr->Release();
    }
}

//给编码器开低延迟模式并设码率（通过 ICodecAPI）。失败不致命。
static void setEncoderLowLatency(IMFTransform* mft, int bitrateBps)
{
    ICodecAPI* api = NULL;
    if (FAILED(mft->QueryInterface(IID_PPV_ARGS(&api))) || api == NULL)
        return;

    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = VARIANT_TRUE;
    api->SetValue(&CODECAPI_AVLowLatencyMode, &v);

    VARIANT vb;
    VariantInit(&vb);
    vb.vt = VT_UI4;
    vb.ulVal = static_cast<ULONG>(bitrateBps);
    api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &vb);

    api->Release();
}

//==== MfH264Encoder ====

struct MfH264Encoder::Impl
{
    IMFTransform* mft;
    int           width;
    int           height;
    int           fps;
    bool          ready;

    Impl()
        : mft(NULL)
        , width(0)
        , height(0)
        , fps(0)
        , ready(false)
    {
    }
};

MfH264Encoder::MfH264Encoder()
    : m_impl(new Impl())
{
}

MfH264Encoder::~MfH264Encoder()
{
    Close();
    delete m_impl;
}

bool MfH264Encoder::IsReady() const
{
    return m_impl->ready;
}

bool MfH264Encoder::Init(int width, int height, int fps, int bitrateBps)
{
    Close();
    if (width <= 0 || height <= 0 || fps <= 0)
        return false;

    HRESULT hr = ::CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&m_impl->mft));
    if (FAILED(hr) || m_impl->mft == NULL)
    {
        LOG_ERROR("MfH264Encoder: create encoder MFT failed, hr=0x%08x.", hr);
        return false;
    }

    setMftLowLatency(m_impl->mft);
    setEncoderLowLatency(m_impl->mft, bitrateBps);

    //编码器须先设输出类型（H.264），再设输入类型（NV12）
    IMFMediaType* outType = NULL;
    ::MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrateBps));
    ::MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps, 1);
    ::MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
    hr = m_impl->mft->SetOutputType(0, outType, 0);
    outType->Release();
    if (FAILED(hr))
    {
        LOG_ERROR("MfH264Encoder: SetOutputType(H264) failed, hr=0x%08x.", hr);
        Close();
        return false;
    }

    IMFMediaType* inType = NULL;
    ::MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    ::MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps, 1);
    ::MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = m_impl->mft->SetInputType(0, inType, 0);
    inType->Release();
    if (FAILED(hr))
    {
        LOG_ERROR("MfH264Encoder: SetInputType(NV12) failed, hr=0x%08x.", hr);
        Close();
        return false;
    }

    m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_impl->width = width;
    m_impl->height = height;
    m_impl->fps = fps;
    m_impl->ready = true;
    LOG_INFO("MfH264Encoder: ready, %dx%d @%dfps %dbps.", width, height, fps, bitrateBps);
    return true;
}

void MfH264Encoder::Close()
{
    if (m_impl->mft != NULL)
    {
        m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        m_impl->mft->Release();
        m_impl->mft = NULL;
    }
    m_impl->ready = false;
    m_impl->width = 0;
    m_impl->height = 0;
    m_impl->fps = 0;
}

void MfH264Encoder::EncodeNv12(const uint8_t* nv12, int stride, int64_t ptsHns, MfH264EncoderSink* sink)
{
    if (!m_impl->ready || nv12 == NULL || sink == NULL)
        return;

    int w = m_impl->width;
    int h = m_impl->height;
    int frameBytes = w * h * 3 / 2;   //NV12 = Y(w*h) + UV(w*h/2)

    //把 NV12 拷进连续 stride==w 的输入缓冲（来源 stride 可能 > w，则逐行拷）
    IMFMediaBuffer* inBuf = NULL;
    if (FAILED(::MFCreateMemoryBuffer(frameBytes, &inBuf)) || inBuf == NULL)
        return;
    BYTE* p = NULL;
    if (FAILED(inBuf->Lock(&p, NULL, NULL)))
    {
        inBuf->Release();
        return;
    }
    if (stride == w)
    {
        memcpy(p, nv12, frameBytes);
    }
    else
    {
        for (int y = 0; y < h; ++y)
            memcpy(p + y * w, nv12 + y * stride, w);
        BYTE* dstUV = p + w * h;
        const uint8_t* srcUV = nv12 + static_cast<size_t>(stride) * h;
        for (int y = 0; y < h / 2; ++y)
            memcpy(dstUV + y * w, srcUV + static_cast<size_t>(y) * stride, w);
    }
    inBuf->Unlock();
    inBuf->SetCurrentLength(frameBytes);

    IMFSample* inSample = NULL;
    ::MFCreateSample(&inSample);
    inSample->AddBuffer(inBuf);
    inBuf->Release();
    inSample->SetSampleTime(ptsHns);
    inSample->SetSampleDuration(kHnsPerSecond / m_impl->fps);

    HRESULT hr = m_impl->mft->ProcessInput(0, inSample, 0);
    inSample->Release();
    if (FAILED(hr))
    {
        LOG_ERROR("MfH264Encoder: ProcessInput failed, hr=0x%08x.", hr);
        return;
    }

    //抽出所有就绪的 H.264 包（调用方分配输出样本）
    MFT_OUTPUT_STREAM_INFO si;
    memset(&si, 0, sizeof(si));
    m_impl->mft->GetOutputStreamInfo(0, &si);
    DWORD outCap = (si.cbSize > 0) ? si.cbSize : static_cast<DWORD>(w * h);

    while (true)
    {
        IMFSample* outSample = NULL;
        IMFMediaBuffer* outBuf = NULL;
        ::MFCreateSample(&outSample);
        ::MFCreateMemoryBuffer(outCap, &outBuf);
        outSample->AddBuffer(outBuf);

        MFT_OUTPUT_DATA_BUFFER odb;
        memset(&odb, 0, sizeof(odb));
        odb.pSample = outSample;
        DWORD status = 0;
        hr = m_impl->mft->ProcessOutput(0, 1, &odb, &status);
        if (odb.pEvents != NULL)
            odb.pEvents->Release();

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            outBuf->Release();
            outSample->Release();
            break;
        }
        if (FAILED(hr))
        {
            LOG_ERROR("MfH264Encoder: ProcessOutput failed, hr=0x%08x.", hr);
            outBuf->Release();
            outSample->Release();
            break;
        }

        UINT32 cleanPoint = 0;
        bool keyframe = SUCCEEDED(outSample->GetUINT32(MFSampleExtension_CleanPoint, &cleanPoint)) && cleanPoint != 0;
        LONGLONG t = ptsHns;
        outSample->GetSampleTime(&t);

        IMFMediaBuffer* cont = NULL;
        if (SUCCEEDED(outSample->ConvertToContiguousBuffer(&cont)) && cont != NULL)
        {
            BYTE* op = NULL;
            DWORD ol = 0;
            if (SUCCEEDED(cont->Lock(&op, NULL, &ol)) && op != NULL && ol > 0)
            {
                sink->OnEncodedH264(op, ol, keyframe, static_cast<int64_t>(t));
                cont->Unlock();
            }
            cont->Release();
        }

        outBuf->Release();
        outSample->Release();
    }
}

//==== MfH264Decoder ====

struct MfH264Decoder::Impl
{
    IMFTransform* mft;
    int           width;       //输出 NV12 宽（首个 STREAM_CHANGE 后确定）
    int           height;
    int           stride;      //输出 NV12 行跨度
    DWORD         outCap;      //输出缓冲容量
    bool          outSet;      //是否已协商好 NV12 输出类型
    bool          ready;

    Impl()
        : mft(NULL)
        , width(0)
        , height(0)
        , stride(0)
        , outCap(0)
        , outSet(false)
        , ready(false)
    {
    }

    //在解码器当前可用输出类型里挑 NV12 并设上，回填尺寸 / 跨度 / 容量。成功返回 true。
    bool selectNv12Output();
};

MfH264Decoder::MfH264Decoder()
    : m_impl(new Impl())
{
}

MfH264Decoder::~MfH264Decoder()
{
    Close();
    delete m_impl;
}

bool MfH264Decoder::IsReady() const
{
    return m_impl->ready;
}

//在解码器当前可用输出类型里挑 NV12 并设上，回填尺寸 / 跨度 / 容量。成功返回 true。
bool MfH264Decoder::Impl::selectNv12Output()
{
    for (DWORD i = 0; ; ++i)
    {
        IMFMediaType* t = NULL;
        HRESULT hr = mft->GetOutputAvailableType(0, i, &t);
        if (hr == MF_E_NO_MORE_TYPES || FAILED(hr) || t == NULL)
            break;

        GUID subtype = GUID_NULL;
        t->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype == MFVideoFormat_NV12)
        {
            hr = mft->SetOutputType(0, t, 0);
            if (SUCCEEDED(hr))
            {
                UINT32 w = 0;
                UINT32 h = 0;
                ::MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
                width = static_cast<int>(w);
                height = static_cast<int>(h);
                UINT32 ds = 0;
                if (FAILED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &ds)) || static_cast<int>(ds) <= 0)
                    stride = width;
                else
                    stride = static_cast<int>(ds);

                MFT_OUTPUT_STREAM_INFO si;
                memset(&si, 0, sizeof(si));
                mft->GetOutputStreamInfo(0, &si);
                outCap = (si.cbSize > 0) ? si.cbSize
                                         : static_cast<DWORD>(width * height * 3 / 2);
                outSet = true;
                t->Release();
                LOG_INFO("MfH264Decoder: NV12 output %dx%d stride=%d.", width, height, stride);
                return true;
            }
        }
        t->Release();
    }
    return false;
}

bool MfH264Decoder::Init(int width, int height)
{
    Close();
    if (width <= 0 || height <= 0)
        return false;

    HRESULT hr = ::CoCreateInstance(CLSID_CMSH264DecoderMFT, NULL, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&m_impl->mft));
    if (FAILED(hr) || m_impl->mft == NULL)
    {
        LOG_ERROR("MfH264Decoder: create decoder MFT failed, hr=0x%08x.", hr);
        return false;
    }

    //低延迟：MFT 属性 MF_LOW_LATENCY（不为重排序缓冲多帧）+ ICodecAPI 低延迟模式
    setMftLowLatency(m_impl->mft);
    ICodecAPI* api = NULL;
    if (SUCCEEDED(m_impl->mft->QueryInterface(IID_PPV_ARGS(&api))) && api != NULL)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BOOL;
        v.boolVal = VARIANT_TRUE;
        api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
        api->Release();
    }

    //输入类型：H.264 + 帧尺寸 / 帧率（设了尺寸，解码器才能在 ProcessInput 前就给出带尺寸的
    //可用输出类型，从而在 Init 里就把输出类型设好，避免 ProcessOutput 返回 TYPE_NOT_SET）
    IMFMediaType* inType = NULL;
    ::MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    ::MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, 30, 1);
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = m_impl->mft->SetInputType(0, inType, 0);
    inType->Release();
    if (FAILED(hr))
    {
        LOG_ERROR("MfH264Decoder: SetInputType(H264) failed, hr=0x%08x.", hr);
        Close();
        return false;
    }

    //提前协商 NV12 输出类型（此后真实尺寸若不同，解码时经 STREAM_CHANGE 重协商）
    if (!m_impl->selectNv12Output())
    {
        LOG_ERROR("MfH264Decoder: select NV12 output in Init failed.");
        Close();
        return false;
    }

    m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    m_impl->ready = true;
    return true;
}

void MfH264Decoder::Close()
{
    if (m_impl->mft != NULL)
    {
        m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        m_impl->mft->Release();
        m_impl->mft = NULL;
    }
    m_impl->width = 0;
    m_impl->height = 0;
    m_impl->stride = 0;
    m_impl->outCap = 0;
    m_impl->outSet = false;
    m_impl->ready = false;
}

void MfH264Decoder::DecodePacket(const uint8_t* data, size_t len, int64_t ptsHns, MfH264DecoderSink* sink)
{
    if (!m_impl->ready || data == NULL || len == 0 || sink == NULL)
        return;

    IMFMediaBuffer* inBuf = NULL;
    if (FAILED(::MFCreateMemoryBuffer(static_cast<DWORD>(len), &inBuf)) || inBuf == NULL)
        return;
    BYTE* p = NULL;
    if (FAILED(inBuf->Lock(&p, NULL, NULL)))
    {
        inBuf->Release();
        return;
    }
    memcpy(p, data, len);
    inBuf->Unlock();
    inBuf->SetCurrentLength(static_cast<DWORD>(len));

    IMFSample* inSample = NULL;
    ::MFCreateSample(&inSample);
    inSample->AddBuffer(inBuf);
    inBuf->Release();
    inSample->SetSampleTime(ptsHns);

    HRESULT hr = m_impl->mft->ProcessInput(0, inSample, 0);
    inSample->Release();
    if (FAILED(hr) && hr != MF_E_NOTACCEPTING)
    {
        LOG_ERROR("MfH264Decoder: ProcessInput failed, hr=0x%08x.", hr);
        return;
    }

    //抽出所有就绪的 NV12 帧
    while (true)
    {
        //输出类型未定（还没碰到 STREAM_CHANGE）时先试协商一次；不行就等下一包
        IMFSample* outSample = NULL;
        IMFMediaBuffer* outBuf = NULL;
        if (m_impl->outSet)
        {
            ::MFCreateSample(&outSample);
            ::MFCreateMemoryBuffer(m_impl->outCap, &outBuf);
            outSample->AddBuffer(outBuf);
        }

        MFT_OUTPUT_DATA_BUFFER odb;
        memset(&odb, 0, sizeof(odb));
        odb.pSample = outSample;   //outSet 为假时为 NULL，仅用于探测 STREAM_CHANGE
        DWORD status = 0;
        hr = m_impl->mft->ProcessOutput(0, 1, &odb, &status);
        if (odb.pEvents != NULL)
            odb.pEvents->Release();

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            if (outBuf != NULL)
                outBuf->Release();
            if (outSample != NULL)
                outSample->Release();
            //据码流协商 NV12 输出类型与尺寸，然后继续抽帧
            if (!m_impl->selectNv12Output())
            {
                LOG_ERROR("MfH264Decoder: select NV12 output failed after stream change.");
                break;
            }
            continue;
        }
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            if (outBuf != NULL)
                outBuf->Release();
            if (outSample != NULL)
                outSample->Release();
            break;
        }
        if (FAILED(hr))
        {
            LOG_ERROR("MfH264Decoder: ProcessOutput failed, hr=0x%08x.", hr);
            if (outBuf != NULL)
                outBuf->Release();
            if (outSample != NULL)
                outSample->Release();
            break;
        }

        //拿到一帧 NV12
        if (odb.pSample != NULL)
        {
            LONGLONG t = ptsHns;
            odb.pSample->GetSampleTime(&t);
            IMFMediaBuffer* cont = NULL;
            if (SUCCEEDED(odb.pSample->ConvertToContiguousBuffer(&cont)) && cont != NULL)
            {
                BYTE* op = NULL;
                DWORD ol = 0;
                if (SUCCEEDED(cont->Lock(&op, NULL, &ol)) && op != NULL && ol > 0)
                {
                    sink->OnDecodedNv12(op, m_impl->width, m_impl->height, m_impl->stride,
                                        static_cast<int64_t>(t));
                    cont->Unlock();
                }
                cont->Release();
            }
        }

        if (outBuf != NULL)
            outBuf->Release();
        if (outSample != NULL)
            outSample->Release();
    }
}

} // namespace call
