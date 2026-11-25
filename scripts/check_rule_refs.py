#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_rule_refs.py —— 规则可执行性校验（SoundDog）

查三类悬空引用（对应 2026-11-26 审计发现的那一类问题）：
  A1 正文引用但未定义的规则号（定义 = rules.md 的 [Rx] ∪ rule-index.md 的表格行）
  A2 正文引用但不存在的章节 §N / 章节号重复
  A3 反引号里点名的文件路径不存在

设计约束（血泪）：
  * **匹配串一律从目标文本里取，不硬编码**（例：R19 原文是 `Core/` 不是 `firmware/Core`）
  * 默认 WARN，`--fail` 才返回非 0 —— 误报一次就会被 --no-verify 绕过，全盘失效
  * 路径解析按「仓库根 → 常见子目录」的顺序，跨仓路径去父目录找

用法：
    python scripts/check_rule_refs.py            # 只报告，恒返回 0
    python scripts/check_rule_refs.py --fail     # 有项目返回 1（供 pre-commit）
"""
from __future__ import annotations

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = os.path.join(ROOT, '.claude', 'rules.md')
INDEX = os.path.join(ROOT, 'docs', 'summary', 'rule-index.md')

PATH_RE = re.compile(r'`([^`\n]+\.(?:md|sh|ps1|py|c|h))`')
# 路径搜索顺序（相当于 include 路径）
SEARCH_DIRS = ['', 'docs', 'docs/summary', 'docs/features', 'docs/hardware',
               'docs/bugs', 'docs/tools', 'docs/handover', 'scripts',
               os.path.join('firmware', 'soundDog'), 'firmware']


def read(p: str) -> str:
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def resolve(rel: str):
    """返回 True/False/None（None = 不像仓库内路径）。"""
    rel = rel.strip()
    if not rel or len(rel) > 200:
        return None
    # 绝对路径 / http / 明确标注仓库外的（桌面\ 或含 Windows 分隔符的绝对位置）
    if rel.startswith(('http', 'C:', '/c/', '桌面\\', '桌面/')):
        return None
    if '桌面' in rel:          # 例：桌面\stm32F407ZGT6\...
        return None
    for d in SEARCH_DIRS:
        cand = os.path.join(ROOT, d, rel) if d else os.path.join(ROOT, rel)
        if os.path.exists(cand):
            return True
    # 跨仓：父目录下同名路径（如 ../SoundDog/docs/...）
    parent = os.path.dirname(ROOT)
    if os.path.exists(os.path.join(parent, rel)):
        return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--fail', action='store_true', help='有问题时返回 1（供 pre-commit）')
    args = ap.parse_args()

    if not os.path.exists(RULES):
        print(f'[FATAL] 找不到 {RULES}')
        return 2

    s = read(RULES)
    problems = []

    # ---------- A1 规则号 ----------
    defined = set(re.findall(r'\[R(\d+(?:\.\d+)?)\]', s))
    if os.path.exists(INDEX):
        # 外置索引里的表格行：| R33 | ... |
        idx = read(INDEX)
        defined |= set(re.findall(r'^\|\s*R(\d+(?:\.\d+)?)\s*\|', idx, re.M))
    refs = set(re.findall(r'R(\d+(?:\.\d+)?)', s))
    undef = sorted(refs - defined, key=float)
    print(f'[A1] 定义 {len(defined)} 条 / 正文引用 {len(refs)} 个号')
    print(f'     引用但未定义: {", ".join("R" + x for x in undef) if undef else "无"}')
    if undef:
        problems.append(f'A1 未定义规则: {", ".join("R" + x for x in undef)}')

    # ---------- A2 章节 ----------
    refsec = set(re.findall(r'§(\d+)', s))
    heads = re.findall(r'^##\s*§(\d+)', s, re.M)
    hs = set(heads)
    dups = [n for n in sorted(hs, key=int) if heads.count(n) > 1]
    miss = sorted(refsec - hs, key=int)
    print(f'[A2] 章节定义 {sorted(hs, key=int)} / 引用 {sorted(refsec, key=int)}')
    print(f'     引用但不存在: {", ".join("§" + x for x in miss) if miss else "无"}')
    print(f'     重复定义:     {", ".join("§" + x for x in dups) if dups else "无"}')
    if miss:
        problems.append(f'A2 不存在章节: {", ".join("§" + x for x in miss)}')
    if dups:
        problems.append(f'A2 章节重复: {", ".join("§" + x for x in dups)}')

    # ---------- A3 路径 ----------
    # ⚠️ 必须同时扫 rule-index.md：§7 外置后，索引表里的路径才是最容易烂的那批
    sources = [('rules.md', s)]
    if os.path.exists(INDEX):
        sources.append(('rule-index.md', read(INDEX)))
    bad, checked = [], 0
    for tag, text in sources:
        for m in PATH_RE.finditer(text):
            raw = m.group(1)
            r = resolve(raw)
            if r is None:
                continue
            checked += 1
            if r is False:
                bad.append(f'{raw}  ← 出自 {tag}')
    print(f'[A3] 路径引用 {checked} 个（含 rule-index.md）')
    if bad:
        for b in sorted(set(bad)):
            print(f'     ❌ 不存在: `{b}`')
            problems.append(f'A3 路径不存在: {b}')
    else:
        print('     全部存在')

    print()
    if problems:
        print(f'===== {len(problems)} 项问题 =====')
        for p in problems:
            print('  -', p)
        return 1 if args.fail else 0
    print('===== ✅ 全部可执行 =====')
    return 0


if __name__ == '__main__':
    sys.exit(main())
