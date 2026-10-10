/**
 *  识别引擎（模型）的加载与卸载，实现见 CaptionEngine.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionEngine.h"

#include <string.h>

#include "../CaptionApi.h"
#include "../../Source/Common/Log/IULog.h"
#include "CaptionModelRes.h"

#ifdef CAPTION_HAS_ENGINE
#include "sherpa-onnx/c-api/c-api.h"
#endif

namespace caption {

namespace {

#ifdef CAPTION_HAS_ENGINE
//登记内存文件时用的名字：只要与真实路径不冲突即可
const char kMemEncoder[] = "flamingo-caption-memory:encoder.int8.ort";
const char kMemDecoder[] = "flamingo-caption-memory:decoder.onnx";
const char kMemJoiner[] = "flamingo-caption-memory:joiner.int8.onnx";
//模型类型：与模型元数据里的 model_type 一致（prepare_model.py 转换时已核对）
const char kModelType[] = "zipformer2";
//特征参数：16 kHz、80 维 fbank，与模型训练时一致
const int kFeatureSampleRate = 16000;
const int kFeatureDim = 80;
//推理线程数：0g 实测多线程不能加速流式识别，反而成倍增加 CPU
const int kRecognizerThreads = 1;
//端点规则：没识别出字时静音 2.4 秒切；识别出字后静音 0.8 秒切；一句最长 20 秒
const float kEndpointRule1Sec = 2.4f;
const float kEndpointRule2Sec = 0.8f;
const float kEndpointRule3Sec = 20.0f;
//资源类型 RCDATA 的编号
const int kRtRcData = 10;

//取一个 RCDATA 资源；失败返回 NULL
const void* GetResource(HMODULE mod, int id, DWORD* size)
{
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(kRtRcData));
    if (res == NULL)
        return NULL;
    HGLOBAL h = LoadResource(mod, res);
    if (h == NULL)
        return NULL;
    *size = SizeofResource(mod, res);
    return LockResource(h);
}
#endif

int64_t NowMs()
{
    return static_cast<int64_t>(GetTickCount64());
}

} // namespace

CaptionEngine& CaptionEngine::Instance()
{
    static CaptionEngine instance;
    return instance;
}

bool CaptionEngine::HasEngine()
{
#ifdef CAPTION_HAS_ENGINE
    return true;
#else
    return false;
#endif
}

CaptionEngine::CaptionEngine()
    : m_state(HasEngine() ? caption_engine_idle : caption_engine_failed)
    , m_modelModule(NULL)
    , m_recognizer(NULL)
{
}

CaptionEngine::~CaptionEngine()
{
}

void CaptionEngine::SetModelPath(const std::wstring& modelDllPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_modelPath = modelDllPath;
}

void CaptionEngine::Preload()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state.load() != caption_engine_idle)
        return;
    if (m_loader.joinable())
        m_loader.join();
    m_state = caption_engine_loading;
    m_loader = std::thread(&CaptionEngine::LoadOnLoaderThread, this);
}

int CaptionEngine::State() const
{
    return m_state.load();
}

const void* CaptionEngine::Recognizer() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recognizer;
}

void CaptionEngine::LoadOnLoaderThread()
{
#ifdef CAPTION_HAS_ENGINE
    const int64_t t0 = NowMs();
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_modelPath;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (mod == NULL)
    {
        LOG_WARNING("caption: model dll not loaded (error %lu), captions disabled.", GetLastError());
        m_state = caption_engine_failed;
        return;
    }
    DWORD encSize = 0;
    DWORD decSize = 0;
    DWORD joinSize = 0;
    DWORD tokSize = 0;
    const void* enc = GetResource(mod, IDR_CAPTION_MODEL_ENCODER, &encSize);
    const void* dec = GetResource(mod, IDR_CAPTION_MODEL_DECODER, &decSize);
    const void* join = GetResource(mod, IDR_CAPTION_MODEL_JOINER, &joinSize);
    const void* tok = GetResource(mod, IDR_CAPTION_MODEL_TOKENS, &tokSize);
    if (enc == NULL || dec == NULL || join == NULL || tok == NULL || encSize == 0 || tokSize == 0)
    {
        LOG_WARNING("caption: model dll has no model resources (built without the model), captions disabled.");
        FreeLibrary(mod);
        m_state = caption_engine_failed;
        return;
    }
    DWORD infoSize = 0;
    const void* info = GetResource(mod, IDR_CAPTION_MODEL_INFO, &infoSize);
    std::string infoText = info != NULL ? std::string(static_cast<const char*>(info), infoSize) : std::string();
    for (size_t i = 0; i < infoText.size(); ++i)
    {
        if (infoText[i] == '\r' || infoText[i] == '\n')
            infoText[i] = ' ';
    }

    SherpaOnnxRegisterMemoryFile(kMemEncoder, enc, encSize);
    SherpaOnnxRegisterMemoryFile(kMemDecoder, dec, decSize);
    SherpaOnnxRegisterMemoryFile(kMemJoiner, join, joinSize);

    SherpaOnnxOnlineRecognizerConfig config;
    memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = kFeatureSampleRate;
    config.feat_config.feature_dim = kFeatureDim;
    config.model_config.transducer.encoder = kMemEncoder;
    config.model_config.transducer.decoder = kMemDecoder;
    config.model_config.transducer.joiner = kMemJoiner;
    config.model_config.tokens_buf = static_cast<const char*>(tok);
    config.model_config.tokens_buf_size = static_cast<int32_t>(tokSize);
    config.model_config.num_threads = kRecognizerThreads;
    config.model_config.provider = "cpu";
    config.model_config.model_type = kModelType;
    config.decoding_method = "greedy_search";
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = kEndpointRule1Sec;
    config.rule2_min_trailing_silence = kEndpointRule2Sec;
    config.rule3_min_utterance_length = kEndpointRule3Sec;
    const SherpaOnnxOnlineRecognizer* rec = SherpaOnnxCreateOnlineRecognizer(&config);
    if (rec == NULL)
    {
        LOG_ERROR("caption: failed to create the recognizer from %s, captions disabled.", infoText.c_str());
        SherpaOnnxUnregisterMemoryFile(kMemEncoder);
        SherpaOnnxUnregisterMemoryFile(kMemDecoder);
        SherpaOnnxUnregisterMemoryFile(kMemJoiner);
        FreeLibrary(mod);
        m_state = caption_engine_failed;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_modelModule = mod;
        m_recognizer = rec;
    }
    m_state = caption_engine_ready;
    LOG_INFO("caption: recognizer ready in %lld ms (%s).", NowMs() - t0, infoText.c_str());
#else
    m_state = caption_engine_failed;
#endif
}

void CaptionEngine::ReleaseLocked()
{
#ifdef CAPTION_HAS_ENGINE
    if (m_recognizer != NULL)
    {
        SherpaOnnxDestroyOnlineRecognizer(static_cast<const SherpaOnnxOnlineRecognizer*>(m_recognizer));
        m_recognizer = NULL;
        SherpaOnnxUnregisterMemoryFile(kMemEncoder);
        SherpaOnnxUnregisterMemoryFile(kMemDecoder);
        SherpaOnnxUnregisterMemoryFile(kMemJoiner);
    }
#endif
    if (m_modelModule != NULL)
    {
        FreeLibrary(m_modelModule);
        m_modelModule = NULL;
    }
}

void CaptionEngine::Unload()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state.load() != caption_engine_ready)
        return;
    ReleaseLocked();
    m_state = caption_engine_idle;
    LOG_INFO("caption: recognizer unloaded after idle.");
}

void CaptionEngine::Shutdown()
{
    if (m_loader.joinable())
        m_loader.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseLocked();
    if (m_state.load() != caption_engine_failed)
        m_state = caption_engine_idle;
}

} // namespace caption
