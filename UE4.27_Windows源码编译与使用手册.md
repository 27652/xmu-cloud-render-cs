# UE4.27 Windows 源码编译、项目打包与 Pixel Streaming 使用手册

> 适用版本：Unreal Engine 4.27.2 源码版、Windows 10 64 位、Visual Studio 2019。  
> 示例工作区：`D:\UeWorkspace`；示例项目：`Testdemo`。请按实际情况替换路径、项目名和 IP。

本文说明 UE4.27.2 源码获取、Windows 编译、项目打包和 Pixel Streaming 的基本用法。UE4.27 与 UE5 的目录、脚本和参数存在差异，不要混用两个版本的文档。

---

## 1. 目录与组件

| 组件 | 它做什么 | 本文示例位置 |
| --- | --- | --- |
| UE4.27 源码 | 引擎本体；编译后得到 `UE4Editor.exe` | `D:\UeWorkspace\UnrealEngine` |
| UE 项目 | 你的场景、蓝图、C++代码和资源 | `D:\UeWorkspace\Project\Testdemo` |
| Visual Studio | 在 Windows 上编译 UE 和项目 C++ 代码 | Visual Studio 2019 |
| 打包程序 | 无需编辑器即可运行的项目成品 | `...\Packaged_SourceUE_Windows\WindowsNoEditor` |
| Pixel Streaming 插件 | 抓取 UE 画面和音频并进行实时编码 | 在项目插件中启用 |
| Signalling Web Server（Cirrus） | 提供浏览器页面，帮助 UE 与浏览器建立 WebRTC 连接 | `SignallingWebServer` |
| STUN/TURN | 处理跨公网、NAT、复杂防火墙下的连接 | 局域网首次测试可不使用 |

- **编译引擎**：把 UE 源码编译成可用的编辑器和工具。
- **编译项目**：把项目中的 C++ 模块编译成 DLL 或 EXE。
- **Cook（烘焙）**：把材质、贴图、地图等资源转换为目标平台格式。
- **Package（打包）**：把项目代码和 Cook 后的资源整理成可以分发运行的目录。
- **Pixel Streaming**：把 UE 画面和音频编码后通过 WebRTC 发送到浏览器。

---

## 2. 环境准备

### 2.1 操作系统与磁盘

源码编译建议使用：

- Windows 10 64 位；
- 16 GB 以上内存，建议 32 GB；
- 多核 CPU；
- SSD 上预留约 150 GB 可用空间；
- 支持 DirectX 11/12 的显卡；
- 可正常访问 GitHub 的网络。

工作区使用较短的英文路径，例如 `D:\UeWorkspace`，避免空格、中文和过长路径。

### 2.2 Pixel Streaming 显卡要求

UE4.27 Pixel Streaming 需要可用的硬件视频编码器：

- NVIDIA：显卡和驱动支持 NVENC；
- AMD：显卡和驱动支持 AMF。

出现 `No compatible GPU found` 或编码器加载失败时，检查显卡型号、驱动和硬件编码能力。

### 2.3 安装 Visual Studio 2019

使用 Visual Studio 2019 Community。打开 Visual Studio Installer，勾选：

- 工作负载：**使用 C++ 的游戏开发（Game development with C++）**；
- C++ 编译工具；
- Windows 10 SDK，版本 `10.0.18362` 或更高；
- C++ 分析工具；
- C++ AddressSanitizer（可选）。

### 2.4 安装 Git

安装 Git for Windows，并在命令提示符中确认：

```cmd
git --version
```

## 3. 建立目录

本文使用以下结构：

```text
D:\UeWorkspace\
├── UnrealEngine\
│   └── Engine\Binaries\Win64\UE4Editor.exe
├── Project\Testdemo\
│   ├── Testdemo.uproject
│   └── Packaged_SourceUE_Windows\
│       └── WindowsNoEditor\Testdemo.exe
├── PixelStreamingInfrastructure-UE4.27\   # 可选的独立基础设施仓库
└── logs\
    ├── manual_local_ue\
    └── manual_pixel_streaming\
```

创建基础目录：

```cmd
mkdir "D:\UeWorkspace\Project"
mkdir "D:\UeWorkspace\logs\manual_local_ue"
mkdir "D:\UeWorkspace\logs\manual_pixel_streaming"
```

---

## 4. 获取 UE4.27.2 源码和项目

### 4.1 取得源码权限

UE 源码仓库不是普通公开仓库。需要：

1. 准备 Epic Games 账号和 GitHub 账号；
2. 在 Epic 账号的“连接/应用与账户”中关联 GitHub；
3. 按 Epic/GitHub 提示完成组织邀请或授权；
4. 登录 GitHub 后确认可以打开 `https://github.com/EpicGames/UnrealEngine`。

如果浏览器看到 404，最常见原因不是仓库不存在，而是当前 GitHub 账号没有获得权限、邀请未接受，或 Git 使用了另一个账号的凭据。

### 4.2 克隆源码

在命令提示符中执行：

```cmd
cd /d D:\UeWorkspace
git clone --branch 4.27.2-release --single-branch --depth 1 ^
  https://github.com/EpicGames/UnrealEngine.git UnrealEngine

git clone --branch UE4.27 --single-branch --depth 1 ^
  https://github.com/EpicGames/PixelStreamingInfrastructure.git ^
  PixelStreamingInfrastructure-UE4.27
```

第一条命令下载 UE4.27.2 源码，第二条命令下载与 UE4.27 对应的 Pixel Streaming Infrastructure。两个仓库都固定版本，不使用 `master`。

检查当前版本：

```cmd
cd /d D:\UeWorkspace\UnrealEngine
git branch --show-current
git log -1 --oneline
```

### 4.3 放入项目

将项目复制或克隆到：

```text
D:\UeWorkspace\Project\Testdemo\Testdemo.uproject
```

初次接触时先确认以下内容：

- `.uproject` 文件存在；
- 项目版本确实面向 UE4.27；
- 项目所需第三方插件已经放入 `Plugins`；
- C++ 项目通常有 `Source\Testdemo.Target.cs` 和 `Source\TestdemoEditor.Target.cs`；
- 蓝图项目可以没有 `Source`，但某些插件会让它在打包时仍需要 C++ 工具链。

不要一开始就删除项目的 `Saved`、`Intermediate` 或 `DerivedDataCache`。这些目录可在明确需要“彻底清理构建”时再处理，其中 `Saved` 可能包含日志、配置、存档和性能数据。

---

## 5. 编译 UE4.27 Editor

### 5.1 下载依赖并生成解决方案

在 UE 根目录依次执行：

```cmd
cd /d D:\UeWorkspace\UnrealEngine
Setup.bat
GenerateProjectFiles.bat
```

- `Setup.bat` 下载引擎所需的二进制依赖、安装前置组件并设置文件关联；首次运行时间较长。
- `GenerateProjectFiles.bat` 生成 `UE4.sln`。同步新源码、增删模块或删除 `Intermediate` 后，应重新运行它。
- Windows SmartScreen 如果拦截批处理文件，应先确认文件来自官方仓库，再选择允许运行。

执行完后确认：

```cmd
if exist "D:\UeWorkspace\UnrealEngine\UE4.sln" (echo UE4.sln exists) else (echo UE4.sln not found)
```

显示 `UE4.sln exists` 表示解决方案已生成。

### 5.2 方式一：用 Visual Studio 编译

5.2 和 5.3 是两种编译方式，选择一种即可，不需要重复执行。第一次手工编译建议使用 Visual Studio；需要写脚本或自动构建时使用 5.3 的命令行方式。

1. 双击 `D:\UeWorkspace\UnrealEngine\UE4.sln`。
2. 等待 Visual Studio 完成解决方案加载。
3. 顶部“解决方案配置”选择 `Development Editor`。
4. “解决方案平台”选择 `Win64`。
5. 在解决方案资源管理器中找到 `UE4`，右键选择“生成”。
6. 等待输出窗口显示 Build succeeded/生成成功。

第一次编译耗时取决于 CPU、磁盘、内存和并行编译能力，实际可能明显超过官方文档中的示例时间。编译期间 CPU 和内存占用较高属于正常现象。

### 5.3 方式二：用命令行编译

该命令与 5.2 构建的是同一个 `UE4Editor Win64 Development` 目标：

```cmd
cd /d D:\UeWorkspace\UnrealEngine
Engine\Build\BatchFiles\Build.bat ^
  UE4Editor Win64 Development ^
  -WaitMutex
```

### 5.4 验证并启动编辑器

验证编辑器：

```cmd
set "EDITOR=D:\UeWorkspace\UnrealEngine\Engine\Binaries\Win64\UE4Editor.exe"
if exist "%EDITOR%" (echo UE4Editor.exe exists) else (echo UE4Editor.exe not found)
```

确认文件存在后启动编辑器：

```cmd
"D:\UeWorkspace\UnrealEngine\Engine\Binaries\Win64\UE4Editor.exe"
```

也可以直接打开项目：

```cmd
"D:\UeWorkspace\UnrealEngine\Engine\Binaries\Win64\UE4Editor.exe" ^
  "D:\UeWorkspace\Project\Testdemo\Testdemo.uproject" -log
```

首次启动可能需要较长时间生成 Derived Data Cache 和编译 Shader。此时不要因为界面短时间无响应就强制结束进程，先观察日志和任务管理器中的 CPU/磁盘活动。

---

## 6. 关联项目与源码引擎

如果双击 `.uproject` 时打开了错误的引擎版本：

1. 右键 `Testdemo.uproject`；
2. 选择 **Switch Unreal Engine version / 切换虚幻引擎版本**；
3. 选择刚编译的源码引擎；
4. 重新生成项目文件。

也可以从引擎侧生成项目解决方案：

```cmd
cd /d D:\UeWorkspace\UnrealEngine
GenerateProjectFiles.bat ^
  -project="D:\UeWorkspace\Project\Testdemo\Testdemo.uproject" ^
  -game -engine
```

然后打开项目生成的 `.sln`，对日常编辑器开发选择：

| 目标 | 配置 | 用途 |
| --- | --- | --- |
| `TestdemoEditor` | Development Editor / Win64 | 在 UE Editor 中开发和调试 |
| `Testdemo` | Development / Win64 | 独立游戏程序，需配合 Cook 后内容 |
| `Testdemo` | Shipping / Win64 | 发布版本；调试和统计能力更少 |

编译项目 Editor 目标的命令：

```cmd
"D:\UeWorkspace\UnrealEngine\Engine\Build\BatchFiles\Build.bat" ^
  TestdemoEditor Win64 Development ^
  -Project="D:\UeWorkspace\Project\Testdemo\Testdemo.uproject" ^
  -WaitMutex -NoHotReload
```

如果项目是纯蓝图项目且没有 `TestdemoEditor.Target.cs`，不要生造目标名；直接用编辑器打开并通过“文件 > 打包项目”完成打包即可。

---

## 7. 打包 Windows 项目

### 7.1 检查项目设置

用源码版 `UE4Editor.exe` 打开项目后，至少检查：

- **Edit > Project Settings > Maps & Modes**：默认 Game Map 是否正确；
- **Project Settings > Packaging**：Build Configuration 首测使用 Development；
- 如果没有 Cook 到所需地图，在 **List of Maps to Include in a Packaged Build** 中显式加入地图；
- 插件是否启用且支持 Win64；
- 项目可以在编辑器中用 Standalone Game 正常运行。

### 7.2 通过编辑器打包

1. 菜单选择 **File > Package Project > Windows (64-bit)**；
2. 输出目录选择：

```text
D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows
```

3. 等待右下角任务完成；失败时点击 Output Log 或打开项目 `Saved\Logs` 查看错误。

成功后通常得到：

```text
D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe
```

### 7.3 通过 UAT 打包

适合固定流程或自动化：

```cmd
set "UE_ROOT=D:\UeWorkspace\UnrealEngine"
set "PROJECT=D:\UeWorkspace\Project\Testdemo\Testdemo.uproject"
set "ARCHIVE=D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows"

"%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="%PROJECT%" ^
  -noP4 ^
  -platform=Win64 ^
  -clientconfig=Development ^
  -build -cook -stage -pak -archive ^
  -archivedirectory="%ARCHIVE%" ^
  -utf8output
```

查找实际 EXE：

```cmd
dir /s /b "D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\Testdemo.exe"
```

---

## 8. 本地运行测试

先确认打包程序本身可运行，再接入 Pixel Streaming。

### 8.1 窗口运行

```cmd
set "GAME=D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe"
"%GAME%" ^
  -windowed -ResX=1920 -ResY=1080 ^
  -log ^
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

应检查：

- 程序能进入正确地图；
- 键盘鼠标能操作；
- 画面分辨率与帧率大致符合预期；
- 没有插件缺失、资源缺失或致命错误。

### 8.2 离屏运行

```cmd
set "GAME=D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe"
"%GAME%" ^
  -RenderOffscreen -ForceRes ^
  -ResX=1920 -ResY=1080 ^
  -log ^
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

`-RenderOffscreen` 不等于 Pixel Streaming，它只是不显示本地窗口。官方建议推流时使用它，避免 UE 窗口被最小化后视频和输入停止；需要在服务器本地观察窗口时则先不要加。

### 8.3 启动参数

启动参数跟在项目 EXE 后面，可以按需要组合。例如以窗口模式、1280×720、30 FPS 启动：

```cmd
"D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe" ^
  -windowed -ResX=1280 -ResY=720 ^
  -log ^
  -ExecCmds="t.MaxFPS 30,r.VSync 0,stat fps"
```

| 参数 | 作用 |
| --- | --- |
| `-windowed` | 窗口运行 |
| `-RenderOffscreen` | 不显示本地窗口，常用于推流服务器 |
| `-ForceRes` | 强制使用 `-ResX/-ResY` 指定的分辨率 |
| `-ResX` / `-ResY` | 设置启动分辨率 |
| `-AudioMixer` | 启用软件音频混合，Pixel Streaming 推送音频时使用 |
| `-log` | 打开日志窗口 |
| `-ExecCmds="..."` | 启动后执行控制台命令，多个命令用逗号分隔 |

更多参数见 Epic 的 [UE4.27 Command-Line Arguments](https://dev.epicgames.com/documentation/unreal-engine/command-line-arguments?application_version=4.27)。Pixel Streaming 参数见 [UE4.27 Pixel Streaming Reference](https://dev.epicgames.com/documentation/unreal-engine/pixel-streaming-reference?application_version=4.27)。

---

## 9. 启用 Pixel Streaming

1. 在 UE Editor 中打开项目。
2. 选择 **Edit > Plugins**。
3. 在 Graphics 分类中找到 **Pixel Streaming**。
4. 勾选 Enabled，确认并重启编辑器。
5. 再次打开项目，先使用 **Standalone Game** 验证；正式使用时重新打包 Windows 版本。

Pixel Streaming 插件在 UE4.27 中只支持打包应用或编辑器的 Standalone Game，不要用普通 PIE 视口作为最终推流验证。

推送应用音频时，启动参数中加入 `-AudioMixer`。

---

## 10. 启动 Signalling Web Server

### 10.1 获取 UE4.27 Infrastructure

Pixel Streaming Infrastructure 必须与 UE 版本对应。UE4.27 使用官方仓库的 `UE4.27` 分支，不要使用 `master`：

如果已在 4.2 节下载，可跳过下面的命令。

```cmd
cd /d D:\UeWorkspace
git clone --branch UE4.27 --single-branch --depth 1 ^
  https://github.com/EpicGames/PixelStreamingInfrastructure.git ^
  PixelStreamingInfrastructure-UE4.27
```

下载后使用：

```text
D:\UeWorkspace\PixelStreamingInfrastructure-UE4.27\SignallingWebServer
```

### 10.2 安装依赖

该分支自带 Windows 版 Node。首次使用时运行 `setup.bat`，脚本会准备 Node、npm 依赖和 CoTURN：

```cmd
cd /d D:\UeWorkspace\PixelStreamingInfrastructure-UE4.27\SignallingWebServer
call platform_scripts\cmd\setup.bat
platform_scripts\cmd\node\node.exe --version
```

安装完成后，目录中应存在 `platform_scripts\cmd\node\node.exe`。

### 10.3 配置 Cirrus

编辑下面的文件：

```text
D:\UeWorkspace\PixelStreamingInfrastructure-UE4.27\SignallingWebServer\config.json
```

本手册使用以下端口：

| 用途 | 示例端口 | 谁连接它 |
| --- | ---: | --- |
| 浏览器 HTTP | 18088 | 浏览器访问 `http://服务器IP:18088/` |
| UE Streamer | 18777 | UE 连接 `ws://127.0.0.1:18777` |

`config.json` 示例：

```json
{
  "UseFrontend": false,
  "UseMatchmaker": false,
  "UseHTTPS": false,
  "UseAuthentication": false,
  "LogToFile": true,
  "LogVerbose": true,
  "HomepageFile": "player.html",
  "AdditionalRoutes": {},
  "EnableWebserver": true,
  "MatchmakerAddress": "",
  "MatchmakerPort": "9999",
  "PublicIp": "127.0.0.1",
  "HttpPort": 18088,
  "HttpsPort": 443,
  "StreamerPort": 18777,
  "MaxPlayerCount": -1,
  "peerConnectionOptions": "{\"iceServers\":[]}"
}
```

仅本机访问时 `PublicIp` 使用 `127.0.0.1`；局域网访问时改成 Windows 主机的局域网 IPv4 地址。

### 10.4 启动 Cirrus

直接使用 UE4.27 分支自带的 Node 启动 `cirrus.js`：

```cmd
cd /d D:\UeWorkspace\PixelStreamingInfrastructure-UE4.27\SignallingWebServer
platform_scripts\cmd\node\node.exe cirrus.js --configFile=config.json
```

该窗口需要保持运行。看到类似下面的输出表示 Cirrus 已启动：

```text
WebSocket listening to Streamer connections on :18777
Http listening on *: 18088
```

另开一个 cmd 窗口检查端口和网页服务：

```cmd
netstat -ano | findstr "LISTENING" | findstr ":18088 :18777"
curl.exe -I http://127.0.0.1:18088/
```

---

## 11. 启动 UE 推流

先启动第 10 节的 Cirrus，并保持其窗口运行。然后打开新的 cmd 窗口启动 UE。

### 11.1 完整示例

下面的命令与第 10 节的 `StreamerPort=18777` 对应，使用 1920×1080、60 FPS，并采集 18,000 帧 CSV：

```cmd
cd /d D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor
set "GAME=D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe"

"%GAME%" ^
  -RenderOffscreen ^
  -AudioMixer ^
  -ForceRes ^
  -ResX=1920 -ResY=1080 ^
  -PixelStreamingURL="ws://127.0.0.1:18777" ^
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit" ^
  -csvCaptureFrames=18000 ^
  -csvRepeat=0 ^
  -csvCategories="Basic,Engine,Renderer,RHI" ^
  -csvMetadata="RunId=manual_pixel_streaming,Mode=windows" ^
  -saveddirsuffix=manual_pixel_streaming ^
  -log
```

如果只想先看画面，可使用最小命令：

```cmd
"D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe" ^
  -AudioMixer -RenderOffscreen -ForceRes ^
  -ResX=1920 -ResY=1080 ^
  -PixelStreamingURL="ws://127.0.0.1:18777" ^
  -log
```

### 11.2 参数说明

| 参数 | 作用 |
| --- | --- |
| `-RenderOffscreen` | 不显示本地窗口，避免窗口最小化后停流 |
| `-AudioMixer` | 让 Pixel Streaming 获取应用音频 |
| `-ForceRes` | 强制使用指定分辨率 |
| `-ResX` / `-ResY` | 设置渲染分辨率 |
| `-PixelStreamingURL` | UE 连接 Cirrus 的 WebSocket 地址；端口必须是 `StreamerPort` |
| `-ExecCmds` | 启动后执行控制台命令；示例中限制 60 FPS、关闭 VSync 并显示统计信息 |
| `-csvCaptureFrames=18000` | 采集 18,000 帧；60 FPS 时约 5 分钟 |
| `-csvCategories` | 指定 CSV 统计类别 |
| `-csvMetadata` | 给本次采集写入标识信息 |
| `-saveddirsuffix` | 将本次 `Saved` 数据写入独立目录 |
| `-log` | 打开 UE 日志窗口 |

也可以把连接地址拆成 `-PixelStreamingIP=127.0.0.1 -PixelStreamingPort=18777` 两个参数。

`-PixelStreamingURL` 与 `-PixelStreamingIP/-PixelStreamingPort` 二选一，不要同时使用。

完整参数见 [UE4.27 Pixel Streaming Reference](https://dev.epicgames.com/documentation/unreal-engine/pixel-streaming-reference?application_version=4.27) 和 [UE4.27 Command-Line Arguments](https://dev.epicgames.com/documentation/unreal-engine/command-line-arguments?application_version=4.27)。

UE 连接的是 **StreamerPort 18777**，浏览器访问的是 **HttpPort 18088**。

信令服务器窗口出现 `Streamer connected`，表示 UE 已经连接；这仍不代表浏览器端的 WebRTC 媒体一定已经成功建立。

---

## 12. 浏览器访问与局域网使用

### 12.1 本机访问

按第 10 节的配置访问：

```text
http://127.0.0.1:18088/
```

打开页面后按页面提示点击连接或播放。默认页面会把键盘、鼠标和触摸输入发送回 UE。

### 12.2 局域网访问

查看 Windows 主机 IP：

```cmd
ipconfig
```

找到当前网卡的 IPv4 地址，例如 `192.168.1.10`。其他设备访问：

```text
http://192.168.1.10:18088/
```

如果本机可用、局域网不可用，先检查 Windows 防火墙。只开放实际使用的端口，并尽量把规则限制在专用网络或可信网段。管理员 cmd 示例：

```cmd
netsh advfirewall firewall add rule ^
  name="UE4.27 Pixel Streaming HTTP 18088" ^
  dir=in action=allow protocol=TCP localport=18088 profile=private

netsh advfirewall firewall add rule ^
  name="UE4.27 Pixel Streaming Streamer 18777" ^
  dir=in action=allow protocol=TCP localport=18777 profile=private
```

如果 UE 和 Signalling Server 在同一台机器，Streamer 端口通常只需要本机访问；不要为了省事把所有端口永久开放到所有网络配置文件。

---

## 13. 公网、NAT、STUN/TURN 与 HTTPS

局域网跑通后再处理公网部署。浏览器与 UE 在不同网络、经过 NAT、公司防火墙或云安全组时，通常需要 STUN/TURN。

- STUN 帮助端点发现外部可见地址；
- TURN 在端点无法直连时中继媒体；
- `peerConnectionOptions` 把 ICE 服务器信息交给浏览器和 UE；
- HTTPS/WSS 用于安全页面和安全 WebSocket，生产公网不应长期使用明文 HTTP 与弱口令。

UE4.27 官方 Hosting and Networking Guide 说明，Infrastructure 中包含适用于对应版本的 CoTURN 启动脚本。不同 UE4.27 快照的脚本名和外层路径可能有差异，应使用当前分支自带脚本及 README，不要从 UE5 主分支复制脚本。

在 `config.json` 中，`peerConnectionOptions` 是“字符串中的 JSON”。示意如下，必须替换服务器地址和凭据：

```json
{
  "peerConnectionOptions": "{\"iceServers\":[{\"urls\":[\"turn:203.0.113.10:3478?transport=udp\",\"turn:203.0.113.10:3478?transport=tcp\"],\"username\":\"replace_user\",\"credential\":\"replace_password\"}]}"
}
```

- 示例 IP `203.0.113.10` 是文档保留地址，不能直接使用；
- TURN 端口还要在云安全组、主机防火墙和 NAT 映射中开放；
- TURN 进程存在不代表媒体正在中继；应在 Chrome 的 `chrome://webrtc-internals` 中查看 selected candidate pair；
- Epic 提供的 Signalling Server 是参考实现，不是开箱即用的完整生产平台。生产环境还需认证、HTTPS、访问控制、日志保护、容量管理和进程守护。

---

## 14. 日志与性能数据

### 14.1 UE 日志

常见位置：

```text
项目目录\Saved\Logs\
WindowsNoEditor\Testdemo\Saved\Logs\
```

查找最新日志：

```cmd
dir /s /b /o-d "D:\UeWorkspace\Project\Testdemo\*.log"
```

排障时搜索关键词：

```cmd
findstr /I /C:"Error:" /C:"Fatal" /C:"PixelStreaming" /C:"WebRTC" ^
  /C:"NVENC" /C:"AMF" /C:"Streamer" "D:\实际路径\Testdemo.log"
```

### 14.2 CSV Profiler

以 60 FPS 采集 18,000 帧，约 5 分钟：

```cmd
set "GAME=D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\WindowsNoEditor\Testdemo.exe"
"%GAME%" ^
  -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 ^
  -PixelStreamingURL="ws://127.0.0.1:18777" ^
  -csvCaptureFrames=18000 ^
  -csvCategories="Basic,Engine,Renderer,RHI" ^
  -csvMetadata="RunId=manual_pixel_streaming,Mode=windows" ^
  -log
```

结束后查找 CSV：

```cmd
dir /s /b "D:\UeWorkspace\Project\Testdemo\Packaged_SourceUE_Windows\*.csv" ^
  | findstr /I "\Profiling\CSV\"
```

应让 UE 正常退出并完成文件封口后再复制 CSV。强制结束进程可能导致最后一段数据不完整。

### 14.3 GPU 采样

NVIDIA 驱动安装后可使用：

```cmd
nvidia-smi
```

采样示例，按 `Ctrl+C` 停止：

```cmd
mkdir "D:\UeWorkspace\logs\manual_pixel_streaming\raw"
nvidia-smi ^
  --query-gpu=timestamp,utilization.gpu,utilization.memory,memory.used,power.draw ^
  --format=csv -l 1 ^
  > "D:\UeWorkspace\logs\manual_pixel_streaming\raw\gpu.csv"
```

同时可以用任务管理器的 GPU 页面、资源监视器和浏览器 Pixel Streaming 页面的 Show Stats。要区分：

- UE Render FPS；
- 编码 FPS；
- 浏览器接收/解码 FPS；
- 网络丢包、往返时延和码率。

它们不是同一个指标。

---

## 15. 停止程序

1. 先在 UE 程序中正常退出，或关闭其日志窗口；
2. 等待 CSV 和日志写入完成；
3. 在 Signalling Server 窗口按 `Ctrl+C`；
4. 最后停止 GPU 采样窗口。

如果程序无响应，先查 PID，再只停止确认过的目标：

```cmd
tasklist /FI "IMAGENAME eq Testdemo.exe" /V
```

确认后再执行：

```cmd
taskkill /PID 12345
```

将 `12345` 替换为上一步确认的 PID。

不要使用模糊的批量结束命令，以免关闭其他项目实例或其他用户的进程。

---

## 16. 排障

| 现象 | 第一检查项 | 下一步 |
| --- | --- | --- |
| `Setup.bat` 下载失败 | GitHub权限、网络、磁盘空间 | 重新登录正确GitHub账号，检查代理；不要跳过Setup |
| 没有 `UE4.sln` | `GenerateProjectFiles.bat` 输出 | 检查VS C++工作负载和Windows SDK |
| UE4 编译失败 | VS 输出中第一个真正的 Error | 不要只看最后的“Build failed”汇总 |
| `.uproject` 提示版本不匹配 | 项目 EngineAssociation | 切换到本源码引擎并重新生成项目文件 |
| 编辑器能运行但打包失败 | `Saved\Logs`、缺失插件/地图/SDK | 先用Development打包并处理第一个错误 |
| 打包 EXE 双击无反应 | 加 `-log` 启动 | 查 `WindowsNoEditor\项目名\Saved\Logs` |
| Signalling Server 启动失败 | Node依赖、`config.json`、端口占用 | 重新运行4.27分支的`setup.bat`，再检查监听端口 |
| 网页打不开 | HTTP端口、Windows防火墙、IP | 本机先访问`127.0.0.1` |
| 网页能开但没有画面 | UE进程、插件、Streamer端口、编码器 | 查是否出现`Streamer connected`和NVENC/AMF错误 |
| 本机可用、局域网不可用 | 防火墙规则、网卡IPv4、网络配置文件 | 用另一设备访问主机IPv4和正确HttpPort |
| 局域网可用、公网不可用 | NAT、安全组、STUN/TURN、HTTPS | 查看`webrtc-internals`的ICE candidate pair |
| 有画面但没有声音 | 是否加入`-AudioMixer` | 检查浏览器静音、应用音频设备与日志 |
| 窗口最小化后停流 | 是否使用窗口模式 | 服务器运行加`-RenderOffscreen -ForceRes` |
| 帧率低或卡顿 | Render/Encode/Receive FPS分别观察 | 再看GPU、编码器、网络、分辨率和码率 |
| 没有CSV | capture参数、正常退出、实际Saved路径 | 递归查找`Profiling\CSV` |

从本机、不推流的默认设置开始检查，再逐步增加 Pixel Streaming、局域网、TURN 和公网配置。一次只修改一个变量。

---

## 17. 官方参考资料

- [Downloading Unreal Engine Source Code](https://dev.epicgames.com/documentation/en-us/unreal-engine/downloading-unreal-engine-source-code?application_version=4.27)
- [Building Unreal Engine from Source](https://dev.epicgames.com/documentation/en-us/unreal-engine/building-unreal-engine-from-source?application_version=4.27)
- [Setting Up Visual Studio for Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-for-unreal-engine?application_version=4.27)
- [Hardware and Software Specifications](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications?application_version=4.27)
- [Compiling Game Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/compiling-game-projects?application_version=4.27)
- [Packaging Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-projects?application_version=4.27)
- [Pixel Streaming Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/pixel-streaming-overview?application_version=4.27)
- [Getting Started with Pixel Streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/getting-started-with-pixel-streaming?application_version=4.27)
- [Pixel Streaming Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/pixel-streaming-reference?application_version=4.27)
- [Hosting and Networking Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/hosting-and-networking-guide?application_version=4.27)
- [EpicGames/PixelStreamingInfrastructure](https://github.com/EpicGames/PixelStreamingInfrastructure)
