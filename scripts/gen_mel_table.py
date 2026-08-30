# gen_mel_table.py — A3-02 Mel 滤波器表生成器（2026-08-30）
# 算法逐字对齐 CMSIS-DSP v1.14.4 官方 Scripts/mfccdata.py 的 melFilterMatrix()（R27 预研核实），
# 参数按本 FEAT §1.4 规格：fftlength=512 / fmin=300 / fmax=8000 / melFilters=32 / fs=16000
# 产物：filtPos[32] / filtLen[32] / coefs[totalLen]（稀疏打包，供 mel.c const 数组）
# 用法：python scripts/gen_mel_table.py [--c]   （--c 输出 C 数组格式）

import sys
import numpy as np

FFTLEN = 512
FMIN = 300.0
FMAX = 8000.0
NB_FILTERS = 32
FS = 16000.0


def frequency_to_mel_space(freq):
    """官方公式：1127·ln(1+f/700)（与规格 2595·log10(1+f/700) 数学等价）"""
    return 1127.0 * np.log(1.0 + freq / 700.0)


def mel_filter_matrix(fmin, fmax, num_filters, fs, fft_size):
    """复刻官方 melFilterMatrix()：HTK 风格三角滤波 + 稀疏打包"""
    filters = np.zeros((num_filters, int(fft_size / 2 + 1)))
    zeros = np.zeros(int(fft_size // 2))
    fmin_mel = frequency_to_mel_space(fmin)
    fmax_mel = frequency_to_mel_space(fmax)
    mels = np.linspace(fmin_mel, fmax_mel, num=num_filters + 2)
    linearfreqs = np.linspace(0, fs / 2.0, int(fft_size // 2 + 1))
    spectrogrammels = frequency_to_mel_space(linearfreqs)[1:]
    filt_pos, filt_len = [], []
    total_len = 0
    packed_filters = []
    for n in range(num_filters):
        upper = (spectrogrammels - mels[n]) / (mels[n + 1] - mels[n])
        lower = (mels[n + 2] - spectrogrammels) / (mels[n + 2] - mels[n + 1])
        filters[n, :] = np.hstack([0, np.maximum(zeros, np.minimum(upper, lower))])
        nb = 0
        start_found = False
        end_pos = 0
        for sample in filters[n, :]:
            if not start_found and sample != 0.0:
                start_found = True
                start_pos = nb
            if start_found and sample == 0.0:
                end_pos = nb - 1
                break
            nb += 1
        filt_len.append(end_pos - start_pos + 1)
        total_len += end_pos - start_pos + 1
        filt_pos.append(start_pos)
        packed_filters += list(filters[n, start_pos:end_pos + 1])
    return filt_len, filt_pos, total_len, packed_filters, filters


def main():
    filt_len, filt_pos, total_len, packed, filters = mel_filter_matrix(
        FMIN, FMAX, NB_FILTERS, FS, FFTLEN)

    bin_hz = FS / FFTLEN  # 31.25 Hz/bin
    print(f"===== Mel 滤波器表（fft={FFTLEN} fmin={FMIN} fmax={FMAX} "
          f"N={NB_FILTERS} fs={FS}）=====")
    print(f"totalLen（coefs 数组长度）= {total_len}")
    print(f"RAM/flash 预算：filtPos 32×4B + filtLen 32×4B + coefs {total_len}×4B"
          f" = {(64 + total_len * 4) / 1024:.2f} KB const")

    # 边界核查（AC-01）：第 1 个滤波器覆盖起点、第 32 个终点、单调递增
    print("\n===== 边界核查（AC-01）=====")
    print(f"滤波器 0：filtPos={filt_pos[0]} (bin{filt_pos[0]}"
          f"={filt_pos[0] * bin_hz:.1f}Hz) filtLen={filt_len[0]}")
    end0 = filt_pos[0] + filt_len[0] - 1
    print(f"  → 三角峰 bin{end0} = {end0 * bin_hz:.1f}Hz")
    last = NB_FILTERS - 1
    print(f"滤波器 {last}：filtPos={filt_pos[last]} "
          f"({filt_pos[last] * bin_hz:.1f}Hz) filtLen={filt_len[last]}")
    endl = filt_pos[last] + filt_len[last] - 1
    print(f"  → 终点 bin{endl} = {endl * bin_hz:.1f}Hz "
          f"(bin 末 = {int(FFTLEN / 2)} = 8000Hz)")

    # 单调性核查
    mono = all(filt_pos[i] < filt_pos[i + 1] for i in range(NB_FILTERS - 1))
    print(f"filtPos 单调递增：{mono}")

    # 1kHz 落带预测（AC-02 设计预期）
    bin_1k = int(round(1000.0 / bin_hz))
    band_1k = None
    for i in range(NB_FILTERS):
        if filt_pos[i] <= bin_1k <= filt_pos[i] + filt_len[i] - 1:
            band_1k = i
            break
    print(f"\n1kHz → bin {bin_1k} → 预期峰值带 = {band_1k}"
          if band_1k is not None else "\n1kHz 未落入任何滤波器（异常！）")

    if "--c" in sys.argv:
        print("\n===== C 数组 =====")
        print(f"static const uint32_t mel_filt_pos[{NB_FILTERS}] = "
              f"{{{','.join(str(p) for p in filt_pos)}}};")
        print(f"static const uint32_t mel_filt_len[{NB_FILTERS}] = "
              f"{{{','.join(str(l) for l in filt_len)}}};")
        print(f"static const float32_t mel_coefs[{total_len}] = {{")
        for i in range(0, total_len, 8):
            row = ", ".join(f"{v:.9f}f" for v in packed[i:i + 8])
            print(f"  {row},")
        print("};")


if __name__ == "__main__":
    main()
