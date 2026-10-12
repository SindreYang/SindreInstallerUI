# SindreInstallerUI

SindreInstallerUI 是一个面向 Windows 软件的通用安装器主题项目，目标是使用 **NSIS + Modern UI 2（MUI2）** 提供稳定、可复用的安装、升级和卸载流程。

项目当前处于 **开发中**。现阶段优先确定原生 NSIS/MUI2 的页面结构、资源规范和安装流程，暂不维护自定义 DLL 主题运行时。

`SindrePy` 只是原型中的示例产品名。最终接入其他软件时，应替换产品名称、版本号、图标、许可协议、安装目录和实际 payload。

## 目标特性

以下能力属于当前原生安装器的开发目标：

- **支持 2GB 以上安装内容**：大体积 payload 不塞入单个传统 NSIS 数据块，改用安装包旁的外置分卷或分块文件，安装前检查剩余空间，安装过程中按块校验和提取。
- **支持 uv Python 环境**：可检测或安装 uv，按锁定文件创建项目虚拟环境，并执行依赖同步；后续可提供在线、离线缓存和指定 Python 版本三种模式。
- **支持首次安装和升级**：升级时复用已有安装目录，保留用户配置、数据和快捷方式。
- **支持静默部署**：通过命令行参数执行无人值守安装、升级和卸载，适合批量部署和 CI。
- **支持可选组件**：组件可以设置为必选或可选，并显示说明、占用空间和安装状态。
- **支持失败回退**：复制、解压或依赖同步失败时记录错误并清理未完成的安装内容。

## 当前方向

当前只推进原生 NSIS/MUI2 方案：

- 使用 NSIS 负责安装器打包、文件复制、注册表和卸载器生成。
- 使用 MUI2 提供欢迎、许可、组件、安装目录、安装进度和完成页面。
- 使用同一套脚本支持首次安装、已有版本升级和静默安装。
- 将产品信息、许可文本、图标、组件和 payload 作为脚本或资源配置维护。
- 保持安装器可以脱离 SindrePy、DuiLib 和自定义 UI DLL 独立构建。

## 开发状态

| 模块 | 状态 | 说明 |
| --- | --- | --- |
| Full 原型 | 已归档 | 用于记录完整安装流程和页面布局 |
| Simple 原型 | 已归档 | 用于记录精简安装流程和页面布局 |
| 原生 MUI2 页面 | 开发中 | 正在根据原型整理最终 NSIS 页面和资源 |
| 安装流程 | 开发中 | 安装、升级、卸载和静默模式需要持续验证 |
| 组件管理 | 开发中 | 组件选择、必选组件和空间统计需要接入实际 payload |
| 图标和资源 | 开发中 | 当前资源仍会根据最终产品主题调整 |
| 自动化测试 | 开发中 | 需要继续完善真实安装器闭环和 Windows 环境测试 |

当前目录没有声明正式发布版本，生成的测试包只用于本地验证。

## 原型页面

原型页面保存在 `bak` 中，直接打开对应的 `index.html`：

- Full：[`bak/preview/ui_full/index.html`](bak/preview/ui_full/index.html)
- Simple：[`bak/preview/ui_sample/index.html`](bak/preview/ui_sample/index.html)

本机路径：

```text
F:\My_Github\SindreInstallerUI\bak\preview\ui_full\index.html
F:\My_Github\SindreInstallerUI\bak\preview\ui_sample\index.html
```

这些页面用于确认视觉结构和页面状态，不作为当前安装器的运行时依赖。

## 原型截图

截图由上面两个 `index.html` 生成，统一放在 `tmp/screenshots`，按主题和页面分组。

### Full

![Full 欢迎页](tmp/screenshots/full/01-welcome.png)

![Full 许可页](tmp/screenshots/full/02-license.png)

![Full 组件页](tmp/screenshots/full/03-components.png)

![Full 安装位置](tmp/screenshots/full/04-location.png)

![Full 安装进度](tmp/screenshots/full/05-progress.png)

![Full 安装完成](tmp/screenshots/full/06-finish.png)

![Full 错误回滚](tmp/screenshots/full/07-error.png)

### Simple

![Simple 欢迎页](tmp/screenshots/simple/01-welcome.png)

![Simple 安装中](tmp/screenshots/simple/02-installing.png)

![Simple 安装完成](tmp/screenshots/simple/03-complete.png)

![Simple 许可弹窗](tmp/screenshots/simple/04-license.png)

![Simple 安装选项](tmp/screenshots/simple/05-options.png)

## 目录说明

```text
SindreInstallerUI/
├─ bak/                    # 历史原型、自定义主题和旧源码
│  └─ preview/
│     ├─ ui_full/          # Full 原型 index.html
│     └─ ui_sample/        # Simple 原型 index.html
├─ third_party/nsis/       # NSIS 相关第三方文件
├─ tools/                  # 构建和验证工具
│  └─ nsis/                # NSIS 工具文件
├─ tmp/
│  └─ screenshots/         # README 使用的原型截图
└─ README.md
```

`bak` 中的内容只用于追溯和对照，不参与当前原生安装器运行。

## 原生安装器计划

### 1. 页面流程

标准安装流程按以下顺序实现：

```text
欢迎
  → 许可协议
  → 选择组件
  → 安装目录与快捷方式
  → 安装进度
  → 安装完成
```

同时提供：

- 检测已有安装并进入升级流程。
- 保留用户数据、配置和已有快捷方式。
- 安装失败时显示错误信息并清理已写入内容。
- 静默安装和静默卸载。
- 支持取消、返回和关闭窗口。

### 2. 组件模型

每个组件需要定义：

- 组件名称和说明。
- 是否必选。
- 安装文件或目录。
- 占用空间。
- 是否支持升级时保留。
- 组件被取消时需要清理的文件。

核心运行库等基础组件应设为必选，开发工具、SDK、示例和文档可以作为可选组件。

### 3. 产品配置

后续会把以下内容集中到产品配置区域：

- 产品名称和发布者。
- 版本号。
- 安装目录默认值。
- 安装器图标和卸载器图标。
- 许可协议文本。
- 开始菜单和桌面快捷方式。
- 是否注册 Windows 服务。
- 是否设置开机自启。
- 安装完成后是否启动应用。

### 4. 验证范围

每次调整页面或脚本后，需要至少验证：

1. 新安装能正确写入 payload。
2. 安装目录、快捷方式和注册表信息正确。
3. 再次运行安装包能识别已有版本。
4. 升级不会删除用户数据和配置。
5. 卸载后文件、快捷方式和注册表项清理完成。
6. 静默参数不会弹出交互页面。
7. 取消和异常路径不会留下半安装状态。

## 构建说明

当前仍在整理最终构建目录，暂不把构建命令视为稳定公共接口。NSIS 工具位于：

```text
F:\My_Github\SindreInstallerUI\third_party\nsis
F:\My_Github\SindreInstallerUI\tools\nsis
```

正式构建入口、产物目录和发布命名将在原生脚本目录确定后统一更新。

## 历史内容

`bak` 保存切换到原生 NSIS/MUI2 前的内容，包括：

- Full 和 Simple HTML 原型。
- 自定义 DLL 主题及其 XML、图片和字体资源。
- `SindreInstallerUI.dll` 插件源码。
- DuiLib、CMake、旧构建目录和历史发布产物。

这些内容保留用于视觉比对和实现追溯，不属于当前正在开发的原生安装器版本。
