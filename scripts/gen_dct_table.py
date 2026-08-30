# gen_dct_table.py — A3-03 DCT-II 表生成器（2026-08-30）
# 算法逐字对齐 CMSIS-DSP v1.14.4 官方 Scripts/mfccdata.py 的 dctMatrix()（R27 预研核实，
# GitHub raw v1.14.4 全文拉取），参数按本 FEAT §1.4 规格：
#   dctOutputs=13（取前 13 维）/ melFilters=32（A3-02 输出维度）
# 官方公式：result[i,j] = cos(i·π·(j+0.5)/N) · sqrt(2/N)
#   注：官方对 k=0 行不加 1/sqrt(2) 特殊化（"almost orthogonal" HTK 口径，
#   维护者 Issue #54 实测回复确认与 TensorFlow 一致）
# 产物：dct_coefs[13*32=416]（行主序，供 mfcc_dct.c const 数组）
# 用法：python scripts/gen_dct_table.py [--c]   （--c 输出 C 数组格式）

import sys
import numpy as np

NB_DCT_OUT = 13   # k = 0..12（事实包：取前 13 维）
NB_MEL     = 32   # N = 32（A3-02 Mel 维度）


def dct_matrix(nb_dct_out, nb_mel):
    """复刻官方 dctMatrix()：sqrt(2/N) 统一因子，无 k=0 特殊化"""
    result = np.zeros((nb_dct_out, nb_mel))
    s = (np.linspace(1, nb_mel, nb_mel) - 0.5) / nb_mel
    for i in range(nb_dct_out):
        result[i, :] = np.cos(i * np.pi * s) * np.sqrt(2.0 / nb_mel)
    return result


def main():
    mat = dct_matrix(NB_DCT_OUT, NB_MEL)
    total = NB_DCT_OUT * NB_MEL

    print(f"===== DCT-II 表（K={NB_DCT_OUT} N={NB_MEL}）=====")
    print(f"系数个数 = {total}")
    print(f"flash 预算：{total}×4B = {total * 4 / 1024:.2f} KB const")
    print(f"归一化因子 sqrt(2/N) = sqrt(2/{NB_MEL}) = {np.sqrt(2.0 / NB_MEL):.9f}")

    # 边界核查（表错即对不上）
    print("\n===== 边界核查 =====")
    print(f"k=0 行：全系数 = sqrt(2/N) = {mat[0, 0]:.9f}（cos(0)=1，常数行）")
    print(f"k=0 行首尾系数差（应≈0，cos 严格为 1）：{mat[0, 0] - mat[0, -1]:.2e}")
    print(f"k=1 行 j=0：{mat[1, 0]:.9f}（cos(π·0.5/32)·0.25 手算对照）")
    hand = np.cos(1 * np.pi * 0.5 / 32) * np.sqrt(2.0 / 32)
    print(f"  手算对照值：{hand:.9f}  diff = {abs(mat[1, 0] - hand):.2e}")
    # 正交性核查：官方口径 "almost orthogonal"（TF 文档原话，Issue #54 维护者确认）：
    # 非对角元 ≈ 0；对角元 = 1，唯 k=0 行范数²=2（不加 1/sqrt(2) 特殊化所致）
    gram = mat @ mat.T
    off_max = np.abs(gram - np.diag(np.diag(gram))).max()
    diag = np.diag(gram)
    print(f"非对角元最大绝对值（应≈0）：{off_max:.2e}")
    print(f"对角元（官方口径应为 2,1,1,...）：{[round(float(v), 6) for v in diag]}")
    ok = (off_max < 1e-12) and (abs(diag[0] - 2.0) < 1e-9) \
         and all(abs(d - 1.0) < 1e-9 for d in diag[1:])
    print(f"正交性判定（almost orthogonal 口径）：{'PASS' if ok else 'FAIL'}")

    if "--c" in sys.argv:
        flat = mat.reshape(total)
        print("\n===== C 数组 =====")
        print(f"static const float32_t dct_coefs[{total}] = {{")
        for i in range(0, total, 8):
            row = ", ".join(f"{v:.9f}f" for v in flat[i:i + 8])
            print(f"  {row},")
        print("};")


if __name__ == "__main__":
    main()
