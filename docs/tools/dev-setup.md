# 推荐开发环境

> 当前状态:全部已装。CodeGraph/OpenCLI 装在 `C:\Users\wumu2\.local\nodejs\node-v24.19.0-win-x64\node_modules\` 下,因 TRAE 沙箱拦了 .cmd shim 创建,通过 `.skills/codegraph.bat` / `.skills/opencli.bat` 包装脚本调用。

## 1. CodeGraph — 代码地图

仓库索引工具。一键定位函数定义、调用链、上下游关系。

```bash
.skills\codegraph.bat status          # 查索引状态(节点/边/语言分布)
.skills\codegraph.bat index --force   # 强制重建索引
.skills\codegraph.bat explore "I2S_Init OR RS485_Init"   # 查调用链
.skills\codegraph.bat callers HAL_UART_Transmit         # 谁调了这个函数
```

> 安装后 AI 优先用 CodeGraph 定位代码,而非 grep 全项目搜索,省 Token、更精准。
> SoundDog 当前索引:146 文件 / 2628 函数 / 369 struct(C 语言主)。

## 2. Ponytail — 懒惰模式

**未装,且当前环境用不上**。Ponytail 是 Claude Code 插件(`/ponytail full`),在 DSH/TRAE 这边调不起来。
其"防过度设计"的意图已被本项目 [.claude/rules.md](../../.claude/rules.md) R8 覆盖:三层复用优先(HAL 标准库 → 现有 BSP → grep 调用链),无复用才编写最小代码。

## 3. OpenCLI — 数据查询 & 浏览器驱动

CLI 工具,查网站数据、驱动浏览器调试。

```bash
.skills\opencli.bat list              # 列出所有可用适配器
.skills\opencli.bat hackernews top --limit 5   # 查 HN 热门
.skills\opencli.bat browser           # 驱动浏览器调试 Web 界面/API
```

> 查 STM32 数据手册也可用 TRAE 内置 WebFetch;OpenCLI 主要用于网页结构化抓取和浏览器自动化场景。

---

## 全链路

```
CodeGraph 定位代码 → R8 三层复用约束 → WebFetch/OpenCLI 查数据 → ci_local.sh 验证 → OpenOCD+GDB 硬件调试
```
