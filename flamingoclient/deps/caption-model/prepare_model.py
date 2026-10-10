# -*- coding: utf-8 -*-
"""
准备语音字幕的识别模型（docs/语音字幕方案.md 4.2、14.3），供 FlamingoCaptionModel 工程编进模型资源 DLL。

模型：sherpa-onnx 的中英带标点流式模型 sherpa-onnx-x-asr-480ms-streaming-zipformer-transducer-zh-en-punct-int8-2026-06-05
（Apache-2.0，来自 https://github.com/Gilgamesh-J/X-ASR，经 sherpa-onnx 发布）。模型文件超过 GitHub 单文件 100 MB 的上限，
不进版本库，由本脚本下载到本目录的 download/、转换后输出到 out/（两个目录都在 .gitignore 里）。

步骤：
  1. 下载模型压缩包并解压（或用 --source-dir 指定已解压的目录，跳过下载）；
  2. 按固定的 SHA256 校验 encoder / decoder / joiner / tokens 四个文件；
  3. 用 onnxruntime 把 encoder 转成 ORT 格式（预先做 EXTENDED 级图优化），加载时间约减少一半（方案 14.3）。
     onnxruntime 的版本须与 sherpa-onnx 链接的 1.28 系列一致：pip install onnxruntime==1.28.0；
  4. 输出 out/encoder.int8.ort、decoder.onnx、joiner.int8.onnx、tokens.txt 与 model_info.txt。

用法：
  python flamingoclient/deps/caption-model/prepare_model.py
  python flamingoclient/deps/caption-model/prepare_model.py --source-dir <已解压的模型目录>

balloonwj@qq.com   2026-10-06
"""
import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL_NAME = 'sherpa-onnx-x-asr-480ms-streaming-zipformer-transducer-zh-en-punct-int8-2026-06-05'
MODEL_URL = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/%s.tar.bz2' % MODEL_NAME
# 四个文件的 SHA256 与字节数（2026-10-05 下载的版本）
EXPECTED = {
    'encoder.int8.onnx': ('908596dcc137a73b95be908ca55e88caa1b3dbbe8027c171615f4b0609c5eb1e', 155278641),
    'decoder.onnx': ('a1cbc9eac2d5e3fb6617a218c67ad6daaa7f4e0fd225f08b2c22ab0413c8c257', 11309084),
    'joiner.int8.onnx': ('aedb7fa697b2ab43f20499826fff7c997eea7d67db77be97769aeeeb726e63b3', 2581422),
    'tokens.txt': ('b818a60878b9aae978cbb8ad594acbd403d76d1af2e31ef4197c84e2dbdba27c', 58806),
}
# 转换用的 onnxruntime 版本前缀：须与 sherpa-onnx v1.13.8 链接的 onnxruntime 1.28.2 同一系列
ORT_VERSION_PREFIX = '1.28.'
# 读文件算 SHA256 时每次读的字节数
READ_CHUNK = 1 << 20


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            buf = f.read(READ_CHUNK)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest()


def download_and_extract():
    dl = os.path.join(HERE, 'download')
    os.makedirs(dl, exist_ok=True)
    target = os.path.join(dl, MODEL_NAME)
    if os.path.isdir(target):
        return target
    archive = os.path.join(dl, MODEL_NAME + '.tar.bz2')
    if not os.path.exists(archive):
        print('downloading', MODEL_URL)
        tmp = archive + '.part'
        urllib.request.urlretrieve(MODEL_URL, tmp)
        os.replace(tmp, archive)
    print('extracting', archive)
    with tarfile.open(archive, 'r:bz2') as t:
        t.extractall(dl)
    return target


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source-dir', help='已解压的模型目录（含 encoder.int8.onnx 等四个文件），给出时不下载')
    args = ap.parse_args()
    src = args.source_dir or download_and_extract()

    for name, (digest, size) in EXPECTED.items():
        p = os.path.join(src, name)
        if not os.path.exists(p) or os.path.getsize(p) != size or sha256_of(p) != digest:
            sys.exit('model file missing or changed: %s' % p)
    print('checksums ok')

    try:
        import onnxruntime as ort
    except ImportError:
        sys.exit('onnxruntime not installed: pip install onnxruntime==1.28.0')
    if not ort.__version__.startswith(ORT_VERSION_PREFIX):
        sys.exit('onnxruntime %s found, need %s*: pip install onnxruntime==1.28.0' % (ort.__version__, ORT_VERSION_PREFIX))

    out = os.path.join(HERE, 'out')
    os.makedirs(out, exist_ok=True)
    ort_path = os.path.join(out, 'encoder.int8.ort')
    so = ort.SessionOptions()
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_EXTENDED
    so.optimized_model_filepath = ort_path
    so.add_session_config_entry('session.save_model_format', 'ORT')
    ort.InferenceSession(os.path.join(src, 'encoder.int8.onnx'), so, providers=['CPUExecutionProvider'])
    meta = ort.InferenceSession(ort_path, providers=['CPUExecutionProvider']).get_modelmeta().custom_metadata_map
    if meta.get('model_type') != 'zipformer2':
        sys.exit('unexpected model_type in converted encoder: %r' % meta.get('model_type'))
    for name in ('decoder.onnx', 'joiner.int8.onnx', 'tokens.txt'):
        shutil.copyfile(os.path.join(src, name), os.path.join(out, name))
    with open(os.path.join(out, 'model_info.txt'), 'w', encoding='utf-8') as f:
        f.write('%s\nonnxruntime %s\n' % (MODEL_NAME, ort.__version__))
    print('done:', out)


if __name__ == '__main__':
    main()
