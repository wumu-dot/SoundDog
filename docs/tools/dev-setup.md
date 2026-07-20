# 推荐开发环境

新项目建议按顺序装这三样：

## 1. CodeGraph — 代码地图

仓库索引工具。一键定位函数定义、调用链、上下游关系。

```bash
codegraph init      # 项目根目录执行，生成 .codegraph/ 索引
codegraph explore "函数名 or 问题"   # 查调用链
codegraph node 文件名:行号           # 读源码
```

> 装好后 agent 优先用 CodeGraph 定位代码，而非 grep 全项目搜索，省 Token、更精准。

## 2. Ponytail — 懒惰模式

Claude Code 插件。强制最简方案：标准库优先、一行代码不改三行、不写没用到的抽象。

```bash
# Claude Code 内启用
/ponytail full
```

> 嵌入式项目代码量大、硬件约束多，Ponytail 防止 agent 过度设计。

## 3. OpenCLI — 数据查询 & 浏览器驱动

CLI 工具，查芯片手册、API 文档、驱动浏览器调试。

```bash
opencli "STM32F407 HSE clock configuration"
opencli browser    # 驱动浏览器调试 Web 界面/API
```

> 查数据手册、调试 ESP32 Web 配置页时不用切出终端。

---

## 全链路

```
CodeGraph 定位代码 → Ponytail 约束生成 → OpenCLI 查数据/调试 → ci_local.sh 验证 → OpenOCD+GDB 硬件调试
```
