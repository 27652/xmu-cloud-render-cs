# UE4.27 Linux 源码编译、项目打包与 Pixel Streaming 使用手册

> 适用版本：Unreal Engine 4.27.2、Linux x86_64、当前 `xmu-cloud-render-cs` 仓库。  
> 推荐发行版：Arch Linux；Ubuntu/Debian 也可以参考本文，但包名和 Node.js 安装方式可能不同。  
> 示例工作区：`/mnt/linux_data/git/xmu-cloud-render-cs`。请按实际路径、项目名和网卡地址替换命令中的变量。

本文是 `original-material/UE4.27_Windows源码编译与使用手册.md` 的 Linux 版本。Windows 的 Visual Studio、`.bat`、Win64 和 Windows 防火墙步骤已替换为 Linux 工具链、Shell 脚本、Linux 构建目标和 `systemd`/防火墙命令。

当前仓库已经包含：

```text
xmu-cloud-render-cs/
├── Commit.gitdeps.xml                         # 父目录中的修正版 UE 依赖清单
├── UE4.27/                                    # UE4.27.2 源码
├── PixelStreamingInfrastructure-UE4.27/      # UE4.27 对应的 Pixel Streaming 服务
├── original-material/                         # 原始 Windows 手册和资料
└── using-material/                             # 转换后的使用手册
```

当前仓库没有单独的业务 `.uproject` 项目；`UE4.27/Samples` 和 `UE4.27/Templates` 中的项目用于验证引擎。实际 Pixel Streaming 项目应放在 UE 源码目录之外，例如：

```text
/mnt/ue-workspace/
├── xmu-cloud-render-cs/                        # 本仓库
└── Project/Testdemo/                           # 外部 UE4.27 项目
    ├── Testdemo.uproject
    ├── Content/
    ├── Config/
    ├── Source/                                 # C++ 项目才有
    └── Saved/
```

---

## 1. 组件和 Linux 路径

| 组件 | 作用 | 当前仓库或示例位置 |
| --- | --- | --- |
| UE4.27 源码 | 引擎源码和构建工具 | `UE4.27/` |
| UE4Editor | Linux 编辑器 | `UE4.27/Engine/Binaries/Linux/UE4Editor` |
| UE 项目 | 场景、蓝图、C++ 代码和资源 | `/mnt/ue-workspace/Project/Testdemo/` |
| Pixel Streaming 插件 | 采集画面/音频并通过 WebRTC 推流 | UE4.27 引擎插件或项目插件 |
| Cirrus | Signalling Web Server，提供网页和 WebSocket 信令 | `PixelStreamingInfrastructure-UE4.27/SignallingWebServer/` |
| Coturn | STUN/TURN 服务，处理复杂网络中的 WebRTC 连接 | 系统 `coturn` 或 Infrastructure 脚本 |
| Node.js | 运行 Cirrus | Infrastructure 自带 Node 16 或系统 Node |

几个概念需要区分：

- **编译引擎**：把 UE 源码编译成 `UE4Editor`、`UE4Game` 和相关工具。
- **编译项目**：编译项目的 C++ 模块；纯蓝图项目通常不需要项目 C++ 编译目标。
- **Cook**：将材质、贴图、地图等资源转换为目标平台格式。
- **Package**：把代码和 Cook 后资源整理成可运行目录。
- **Pixel Streaming**：把 UE 画面和音频编码后通过 WebRTC 发送给浏览器。
- `StreamerPort` 是 UE 连接 Cirrus 的 WebSocket 端口；`HttpPort` 是浏览器打开网页的 HTTP 端口，两者不能混用。

---

## 2. 硬件、系统和磁盘要求

### 2.1 推荐硬件

- 64 位 Linux；
- 16 GB 内存起步，建议 32 GB 或更多；
- 多核 CPU；
- SSD 上至少预留 150 GB，完整依赖、编译中间文件、Shader 缓存和项目资源可能需要更多空间；
- 支持硬件编码的 GPU；
- 可访问 GitHub 和 Epic CDN 的网络。

首次编译会产生大量中间文件，`Engine/Intermediate`、`Engine/DerivedDataCache` 和项目 `Intermediate` 不应放在容量很小的分区。

### 2.2 GPU 和硬件编码

UE4.27 Pixel Streaming 需要可用的视频编码器：

- NVIDIA：驱动和 GPU 支持 NVENC；
- AMD：驱动和 GPU 支持 AMF；
- Linux 上应先确认 Vulkan/OpenGL 渲染和 GPU 驱动正常。

NVIDIA 可用下面的命令检查：

```bash
nvidia-smi
```

出现 `No compatible GPU found`、编码器加载失败或黑屏时，先排查 GPU 驱动、权限和远程会话环境，再排查 Pixel Streaming。

### 2.3 Arch Linux 基础工具

```bash
sudo pacman -Syu
sudo pacman -S --needed \
  base-devel git curl wget unzip \
  mono msbuild clang lld llvm \
  cmake ninja python \
  nodejs npm \
  gdb pkgconf \
  vulkan-icd-loader vulkan-tools
```

检查版本：

```bash
git --version
mono --version
msbuild --version
clang --version
node --version
npm --version
vulkaninfo --summary
```

UE4.27 自带历史 Mono 文件，但当前仓库中的 bundled Mono 可缺少可执行的 `mono-boehm`。本仓库已经修改 Linux `SetupMono.sh`：如果 bundled Mono 可执行文件不存在，就使用系统 Mono，并清除冲突的 `MONO_PATH`。因此 Arch Linux 上应优先使用系统 Mono。

### 2.4 文件句柄和进程限制

UE 编辑器和编译器会打开大量文件。先检查：

```bash
ulimit -n
```

临时提高当前 shell 的限制：

```bash
ulimit -n 65536
```

如果仍然遇到 `Too many open files`，为用户配置 `/etc/security/limits.conf` 或 systemd user limits，并重新登录后验证。不要只修改当前终端却在另一个服务进程中运行 UE。

---

## 3. 获取和检查当前仓库

如果已经有本仓库，可以跳过克隆步骤：

```bash
cd /mnt/linux_data/git
git clone git@github.com:27652/xmu-cloud-render-cs.git
cd xmu-cloud-render-cs
```

检查 UE 版本：

```bash
cat UE4.27/Engine/Build/Build.version
```

应包含：

```json
{
    "MajorVersion": 4,
    "MinorVersion": 27,
    "PatchVersion": 2,
    "CompatibleChangelist": 17155196,
    "BranchName": "++UE4+Release-4.27"
}
```

检查 Pixel Streaming 分支：

```bash
git -C PixelStreamingInfrastructure-UE4.27 branch --show-current 2>/dev/null || true
git -C PixelStreamingInfrastructure-UE4.27 log -1 --oneline
```

Infrastructure 的版本必须与 UE4.27 匹配，不要把 UE5 `master` 分支的 Cirrus 脚本混入本目录。

### 3.1 UE 源码权限

如果从 Epic 官方仓库重新获取源码，需要 Epic Games 账号与 GitHub 账号关联，并获得 `EpicGames/UnrealEngine` 的访问权限。可以使用 SSH：

```bash
git clone --branch 4.27.2-release --single-branch \
  git@github.com:EpicGames/UnrealEngine.git UE4.27
```

也可以使用 HTTPS：

```bash
git clone --branch 4.27.2-release --single-branch \
  https://github.com/EpicGames/UnrealEngine.git UE4.27
```

如果 GitHub 返回 404，先确认当前 Git 凭据对应的账号已经被 Epic 授权。Git 源码权限和二进制依赖 CDN 不是同一件事。

---

## 4. 下载 UE4.27 二进制依赖

### 4.1 当前仓库的修正版 manifest

Epic 的 `GitDependencies.exe` 会读取 `Engine/*/Build/*.gitdeps.xml`。本仓库的父目录还提供了修正版：

```text
/mnt/linux_data/git/xmu-cloud-render-cs/Commit.gitdeps.xml
```

它使用新的 dependency pack 集合（例如 `UnrealEngine-18310727`），而 UE4.27 原始 manifest 中的部分旧 pack 会返回 HTTP 403。`UE4.27/Setup.sh` 已处理这一差异：运行时会优先读取父目录的 `Commit.gitdeps.xml`，转换 CDN 地址为 HTTPS，并写入 `UE4.27/Engine/Build/Commit.gitdeps.xml`。

因此必须从当前仓库的 UE 目录执行：

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
./Setup.sh
```

如果使用的是单独复制出来的 UE 目录，没有父目录的修正版 manifest，先将 `Commit.gitdeps.xml` 放在 UE 目录的上一级，或手动把正确版本复制到 `Engine/Build/`。

### 4.2 Setup 输出和失败处理

正常输出会类似：

```text
Registering git hooks...
Using system mono: /usr/bin/mono
Checking dependencies...
Updating dependencies...
```

如果出现：

```text
Corlib not in sync with this runtime
```

确认系统 Mono 被选中：

```bash
command -v mono
mono --version
```

本仓库的 `SetupMono.sh` 应显示：

```text
Using system mono: /usr/bin/mono
```

如果出现 CDN 403，先确认实际 manifest 已切换：

```bash
grep -o 'BaseUrl="[^"]*"' Engine/Build/Commit.gitdeps.xml | head -1
grep -o 'UnrealEngine-[0-9A-Za-z_-]*' Engine/Build/Commit.gitdeps.xml \
  | sort -u | tail
grep -n '12245497' Engine/Build/Commit.gitdeps.xml
```

当前修正版应使用 `https://cdn.unrealengine.com/dependencies`，并且不应再出现旧的 `12245497` revision。如果仍然出现旧 revision，说明执行的不是当前仓库 `Setup.sh`，或父目录 manifest 不存在。

可以先做不下载的检查：

```bash
mono Engine/Binaries/DotNET/GitDependencies.exe --dry-run --threads=1
```

不要把 `remote.origin` 或 `remote.ue` 当成 binary dependency 镜像地址。Git remote 只负责 Git 对象；GitDependencies 使用 manifest 中的 CDN `BaseUrl` 和 `RemotePath`。

### 4.3 缓存

GitDependencies 默认会在父 Git 仓库的下面使用：

```text
.git/ue4-gitdeps/
```

如果团队中另一台机器已经成功下载了相同 manifest 的缓存，可以复制该目录到当前仓库的 `.git/ue4-gitdeps/`。缓存必须来自相同 UE 版本和相同 dependency manifest，不能只复制一部分并假设所有文件都可用。

---

## 5. 生成 Linux 项目文件

依赖下载完成后：

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
./GenerateProjectFiles.sh
```

该脚本会构建或更新 UnrealBuildTool，并生成 Makefile、CMake 文件和其他项目文件。成功后检查：

```bash
test -f Makefile && echo "Makefile exists"
test -f Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh && echo "Linux generator exists"
```

为外部项目生成项目文件：

```bash
./GenerateProjectFiles.sh \
  -project="/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -game -engine
```

如果提示缺少 `UnrealBuildTool.exe`、Mono 或依赖文件，先重新运行 `Setup.sh` 和上面的系统依赖检查。

---

## 6. 编译 UE4.27 Linux Editor

### 6.1 使用 Make

在 UE 根目录运行：

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
make
```

通常也可以显式构建常用目标：

```bash
make UE4Editor
make ShaderCompileWorker
make UnrealLightmass
make UnrealPak
```

并行编译：

```bash
make -j"$(nproc)"
```

如果内存较小，不要盲目使用全部 CPU 线程；可以降低并发：

```bash
make -j4
```

调试版本：

```bash
make UE4Editor-Linux-Debug
```

从头清理再编译：

```bash
make UE4Editor ARGS="-clean"
make -j"$(nproc)" UE4Editor
```

`-clean` 会显著增加构建时间。只有在目标文件、编译配置或中间文件确实不一致时使用。

### 6.2 使用 Build.sh

也可以通过 UnrealBuildTool 构建：

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
Engine/Build/BatchFiles/Linux/Build.sh \
  UE4Editor Linux Development \
  -WaitMutex -NoHotReload
```

针对外部 C++ 项目：

```bash
Engine/Build/BatchFiles/Linux/Build.sh \
  TestdemoEditor Linux Development \
  -Project="/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -WaitMutex -NoHotReload
```

目标名称必须来自项目的 Target.cs。纯蓝图项目没有 `TestdemoEditor.Target.cs` 时，不要人为创建或调用不存在的目标。

### 6.3 验证编辑器

```bash
test -x Engine/Binaries/Linux/UE4Editor && echo "UE4Editor exists"
./Engine/Binaries/Linux/UE4Editor -version
```

打开项目：

```bash
./Engine/Binaries/Linux/UE4Editor \
  "/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -log
```

也可以打开引擎自带模板，例如：

```bash
./Engine/Binaries/Linux/UE4Editor \
  Templates/TP_ThirdPerson/TP_ThirdPerson.uproject \
  -log
```

第一次启动会生成 Shader 缓存和 Derived Data Cache，CPU、磁盘和内存长时间占用是正常的。不要仅因窗口短时间无响应就强制杀进程，应先查看日志和磁盘活动。

---

## 7. Linux 项目编译与打包

### 7.1 项目检查

打开项目后检查：

- Maps & Modes 中的默认地图；
- Packaging 的 Build Configuration；
- 需要打包的地图是否加入 `List of Maps to Include in a Packaged Build`；
- Pixel Streaming 插件是否启用；
- 项目能否使用 Standalone Game 正常运行；
- 项目插件是否支持 Linux；
- C++ 项目是否有 Linux 目标所需的 Target.cs。

Linux 上不能直接使用 Windows 的 `.exe` 打包结果。目标目录通常是 `LinuxNoEditor`，并且项目应在 Linux 上重新 Cook/Package。

### 7.2 使用 RunUAT.sh

UE4.27 当前仓库包含：

```text
UE4.27/Engine/Build/BatchFiles/RunUAT.sh
```

典型 Linux 打包命令：

```bash
export UE_ROOT=/mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
export PROJECT=/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject
export ARCHIVE=/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux

"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PROJECT" \
  -noP4 \
  -platform=Linux \
  -clientconfig=Development \
  -build -cook -stage -pak -archive \
  -archivedirectory="$ARCHIVE" \
  -utf8output
```

查找结果：

```bash
find "$ARCHIVE" -type f -executable -name 'Testdemo' -print
find "$ARCHIVE" -maxdepth 3 -type f -name '*.uproject' -print
```

不同项目的 UAT 参数和第三方 SDK 要求可能不同。排错时先去掉 `-pak` 或使用 Development 配置，先解决第一个真正的编译/Cook 错误。

### 7.3 编辑器内打包

也可以在编辑器中选择：

```text
File -> Package Project -> Linux
```

输出目录建议放在项目之外或一个明确的归档目录，例如：

```text
/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux
```

---

## 8. 本地运行项目

先确认不接 Pixel Streaming 时项目自身可以运行。

### 8.1 窗口运行

```bash
GAME=/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux/LinuxNoEditor/Testdemo

"$GAME" \
  -windowed -ResX=1920 -ResY=1080 \
  -log \
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

如果项目二进制没有执行权限：

```bash
chmod +x "$GAME"
```

图形会话、DISPLAY/Wayland、GPU 驱动和音频设备必须可用。无桌面的服务器应使用离屏模式和正确的 Vulkan/渲染环境。

### 8.2 离屏运行

```bash
"$GAME" \
  -RenderOffscreen -ForceRes \
  -ResX=1920 -ResY=1080 \
  -log \
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

`-RenderOffscreen` 只是隐藏本地窗口，不代表已经启用 Pixel Streaming。服务器部署通常使用该参数，避免窗口最小化后暂停渲染。

### 8.3 常用启动参数

| 参数 | 作用 |
| --- | --- |
| `-windowed` | 窗口模式 |
| `-RenderOffscreen` | 离屏渲染，常用于服务器 |
| `-ForceRes` | 强制使用 `-ResX/-ResY` |
| `-ResX` / `-ResY` | 设置分辨率 |
| `-AudioMixer` | 启用 Pixel Streaming 所需的音频混合 |
| `-log` | 输出日志 |
| `-ExecCmds="..."` | 启动时执行控制台命令 |
| `-PixelStreamingURL=ws://...` | 连接 Cirrus 的 WebSocket |

---

## 9. 启用 Pixel Streaming

1. 用源码版 `UE4Editor` 打开项目；
2. 进入 `Edit -> Plugins`；
3. 启用 `Pixel Streaming`；
4. 重启编辑器；
5. 先用 `Standalone Game` 测试，不要把普通 PIE 视口当作最终推流结果；
6. 重新编译/打包 Linux 项目。

启动推流应用时加入 `-AudioMixer`。UE4.27 的 Pixel Streaming 插件和 UE5 版本参数不完全兼容，不要直接复制 UE5 手册命令。

---

## 10. Linux 下安装和启动 Cirrus

### 10.1 当前 Infrastructure 目录

```bash
export PS_ROOT=/mnt/linux_data/git/xmu-cloud-render-cs/PixelStreamingInfrastructure-UE4.27
export SIGNAL_ROOT="$PS_ROOT/SignallingWebServer"
cd "$SIGNAL_ROOT"
```

如果需要重新获取正确分支：

```bash
cd /mnt/ue-workspace
git clone --branch UE4.27 --single-branch --depth 1 \
  https://github.com/EpicGames/PixelStreamingInfrastructure.git \
  PixelStreamingInfrastructure-UE4.27
```

### 10.2 Node.js 依赖

Infrastructure 的 Linux `setup.sh` 默认面向使用 `apt-get` 的发行版，并会下载 Node 16.14.2。如果在 Arch Linux 上直接运行，它可能在安装 Coturn 阶段失败。可以先执行：

```bash
cd "$SIGNAL_ROOT"
platform_scripts/bash/setup.sh
```

如果脚本因 `apt-get` 或 Coturn 安装失败而停止，手动安装：

```bash
sudo pacman -S --needed nodejs npm coturn
npm install
```

如果项目需要使用 Infrastructure 自带 Node，而不是系统 Node：

```bash
cd "$SIGNAL_ROOT"
platform_scripts/bash/setup.sh
```

检查：

```bash
node --version
npm --version
test -x platform_scripts/bash/node/bin/node && \
  platform_scripts/bash/node/bin/node --version
```

项目脚本要求 Node 版本至少接近 16.x。若 Arch 当前 Node 版本太新导致依赖行为变化，使用项目自带 Node 或用版本管理器安装 Node 16。

### 10.3 配置 config.json

当前仓库的配置文件：

```text
PixelStreamingInfrastructure-UE4.27/SignallingWebServer/config.json
```

当前默认值包括：

```json
{
  "UseFrontend": false,
  "UseMatchmaker": false,
  "UseHTTPS": false,
  "UseAuthentication": false,
  "EnableWebserver": true,
  "PublicIp": "localhost",
  "HttpPort": 80,
  "HttpsPort": 443,
  "StreamerPort": 8888,
  "MaxPlayerCount": -1
}
```

建议本机开发时改成不需要 root 的高端口：

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
  "PublicIp": "127.0.0.1",
  "MatchmakerAddress": "",
  "MatchmakerPort": "9999",
  "HttpPort": 18088,
  "HttpsPort": 443,
  "StreamerPort": 18777,
  "MaxPlayerCount": -1
}
```

端口含义：

| 用途 | 示例端口 | 连接方 |
| --- | ---: | --- |
| 浏览器 HTTP | 18088 | 浏览器访问 `http://127.0.0.1:18088/` |
| UE Streamer | 18777 | UE 连接 `ws://127.0.0.1:18777` |
| HTTPS | 443 | 仅在配置证书并启用 HTTPS 后使用 |

`PublicIp` 在局域网测试时应改成运行 Cirrus 主机的局域网 IPv4；不要把 `localhost` 写成让其他机器可访问的地址。

### 10.4 启动 Cirrus

直接启动当前分支的 Cirrus：

```bash
cd "$SIGNAL_ROOT"
NO_SUDO=1 platform_scripts/bash/run_local.sh \
  --publicIp=127.0.0.1 \
  --httpPort=18088 \
  --streamerPort=18777
```

如果脚本参数版本不接受端口参数，直接使用配置文件和 Node：

```bash
cd "$SIGNAL_ROOT"
PATH="$SIGNAL_ROOT/platform_scripts/bash/node/bin:$PATH" \
  "$SIGNAL_ROOT/platform_scripts/bash/node/bin/node" \
  cirrus.js --configFile=config.json
```

保持该终端运行。另开终端检查：

```bash
ss -ltnp | grep -E ':18088|:18777'
curl -I http://127.0.0.1:18088/
```

成功时应看到 HTTP 服务监听，并能从浏览器打开网页。

---

## 11. 启动 Linux Pixel Streaming 应用

先启动 Cirrus，再启动已经打包的 Linux 应用。

```bash
GAME=/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux/LinuxNoEditor/Testdemo

"$GAME" \
  -RenderOffscreen \
  -AudioMixer \
  -ForceRes \
  -ResX=1920 -ResY=1080 \
  -PixelStreamingURL="ws://127.0.0.1:18777" \
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit" \
  -csvCaptureFrames=18000 \
  -csvRepeat=0 \
  -csvCategories="Basic,Engine,Renderer,RHI" \
  -csvMetadata="RunId=manual_pixel_streaming,Mode=linux" \
  -saveddirsuffix=manual_pixel_streaming \
  -log
```

只测试连接时可以使用最小命令：

```bash
"$GAME" \
  -RenderOffscreen -AudioMixer -ForceRes \
  -ResX=1920 -ResY=1080 \
  -PixelStreamingURL="ws://127.0.0.1:18777" \
  -log
```

UE 日志中应能看到连接 Cirrus 的信息，Cirrus 终端应出现类似 `Streamer connected` 的提示。UE 使用的是 `StreamerPort`，浏览器使用的是 `HttpPort`：

```text
UE -> ws://127.0.0.1:18777
Browser -> http://127.0.0.1:18088/
```

打开浏览器：

```text
http://127.0.0.1:18088/
```

浏览器页面能打开只说明 HTTP 服务正常；出现画面还需要 UE、Streamer WebSocket、WebRTC、编码器和浏览器解码全部正常。

---

## 12. 局域网、公网和 TURN

### 12.1 局域网访问

查看 Linux 主机地址：

```bash
ip -br address
hostname -I
```

假设主机地址是 `192.168.1.10`，将 `config.json` 中的 `PublicIp` 改为该地址，并从其他设备打开：

```text
http://192.168.1.10:18088/
```

使用 `ufw` 的系统可以放行端口：

```bash
sudo ufw allow from 192.168.1.0/24 to any port 18088 proto tcp
sudo ufw allow from 192.168.1.0/24 to any port 18777 proto tcp
```

使用 firewalld：

```bash
sudo firewall-cmd --permanent --add-port=18088/tcp
sudo firewall-cmd --permanent --add-port=18777/tcp
sudo firewall-cmd --reload
```

只放行实际使用的可信网段，不要把开发端口永久暴露到公网。

### 12.2 STUN/TURN

不同网络、NAT、公司防火墙或云安全组下通常需要 STUN/TURN：

- STUN 帮助端点发现外部地址；
- TURN 在端点无法直连时中继媒体；
- `peerConnectionOptions` 将 ICE 服务器传给 WebRTC；
- TURN 的 UDP/TCP 端口还需要在云安全组、主机防火墙和 NAT 映射中放行。

示例：

```json
{
  "peerConnectionOptions": "{\"iceServers\":[{\"urls\":[\"turn:203.0.113.10:3478?transport=udp\",\"turn:203.0.113.10:3478?transport=tcp\"],\"username\":\"replace_user\",\"credential\":\"replace_password\"}]}"
}
```

`203.0.113.10` 是文档保留地址，不能直接使用。不要把真实 TURN 密码提交到 Git。

当前 Infrastructure 提供：

```text
platform_scripts/bash/Start_TURNServer.sh
platform_scripts/bash/Start_WithTURN_SignallingServer.sh
platform_scripts/bash/turn_user_pwd.sh
```

先阅读脚本和分支 README，再决定是否使用；不同 UE4.27 快照的参数可能不同。TURN 进程启动不代表媒体正在中继，应在浏览器 `chrome://webrtc-internals` 中检查 selected candidate pair。

### 12.3 HTTPS/WSS

开发机可以使用 HTTP/WS；公网生产环境应配置 HTTPS/WSS、访问控制、认证、日志保护和进程守护。不要为了快速测试把所有端口和认证永久开放。

---

## 13. 日志、CSV 和 GPU 性能数据

### 13.1 UE 日志

常见位置：

```text
项目/Saved/Logs/
打包目录/LinuxNoEditor/Testdemo/Saved/Logs/
```

查找日志：

```bash
find /mnt/ue-workspace/Project/Testdemo -type f -name '*.log' \
  -printf '%T@ %p\n' | sort -nr | head
```

搜索关键错误：

```bash
grep -RniE 'Error:|Fatal|PixelStreaming|WebRTC|NVENC|AMF|Streamer' \
  /mnt/ue-workspace/Project/Testdemo --include='*.log' | head -100
```

### 13.2 CSV Profiler

`-csvCaptureFrames=18000` 在 60 FPS 下约采集 5 分钟。等待 UE 正常退出后再复制 CSV，强制杀进程可能导致最后一段数据未封口。

```bash
find /mnt/ue-workspace/Project/Testdemo -type f -name '*.csv' \
  -path '*Profiling/CSV*' -print
```

需要区分：

- UE Render FPS；
- 编码 FPS；
- 浏览器接收/解码 FPS；
- 网络码率、丢包和往返时延。

它们不是同一个指标。

### 13.3 NVIDIA GPU 采样

```bash
mkdir -p /mnt/ue-workspace/logs/manual_pixel_streaming/raw
nvidia-smi \
  --query-gpu=timestamp,utilization.gpu,utilization.memory,memory.used,power.draw \
  --format=csv -l 1 \
  > /mnt/ue-workspace/logs/manual_pixel_streaming/raw/gpu.csv
```

按 `Ctrl+C` 停止采样。AMD GPU 使用对应驱动和监控工具，不要把 `nvidia-smi` 输出当作通用 GPU 指标。

---

## 14. 停止程序和 systemd

推荐顺序：

1. 让 UE 正常退出；
2. 等待日志和 CSV 写入完成；
3. 在 Cirrus 终端按 `Ctrl+C`；
4. 最后停止 GPU 采样或 Coturn。

先查 PID，再停止明确的目标：

```bash
pgrep -a -f 'Testdemo|UE4Editor|cirrus.js'
kill <PID>
```

如果程序不响应，确认 PID 后再使用：

```bash
kill -TERM <PID>
sleep 5
kill -KILL <PID>
```

不要直接使用 `killall mono`、`pkill -f UE` 等模糊命令，避免终止其他用户或其他项目的进程。

长期运行可以创建 systemd user service，但应先在交互式终端确认命令、环境变量、GPU 和工作目录都正确。示例：

```ini
# ~/.config/systemd/user/cirrus-ue427.service
[Unit]
Description=UE4.27 Pixel Streaming Cirrus
After=network-online.target

[Service]
Type=simple
WorkingDirectory=/mnt/linux_data/git/xmu-cloud-render-cs/PixelStreamingInfrastructure-UE4.27/SignallingWebServer
Environment=NO_SUDO=1
ExecStart=/mnt/linux_data/git/xmu-cloud-render-cs/PixelStreamingInfrastructure-UE4.27/SignallingWebServer/platform_scripts/bash/node/bin/node cirrus.js --configFile=config.json
Restart=on-failure

[Install]
WantedBy=default.target
```

启用：

```bash
systemctl --user daemon-reload
systemctl --user enable --now cirrus-ue427.service
journalctl --user -u cirrus-ue427.service -f
```

`ExecStart` 中的 Node 路径必须实际存在；如果只安装了系统 Node，应改成 `command -v node` 的绝对路径。

---

## 15. 常见排障

| 现象 | 第一检查项 | 处理方向 |
| --- | --- | --- |
| `Setup.sh` 下载 403 | `Engine/Build/Commit.gitdeps.xml` 的 revision | 确认父目录修正版 manifest 已被复制，不能只改 `remote.ue` |
| `Corlib not in sync` | `MONO_PATH`、bundled Mono 可执行文件 | 使用本仓库 `SetupMono.sh`，确认系统 Mono 被选中 |
| 找不到 `mono`/`xbuild` | 系统包和 PATH | 安装 `mono`、`msbuild`，重新打开终端 |
| `GenerateProjectFiles.sh` 失败 | `.ue4dependencies` 和 UBT | 先完成 Setup，再生成项目文件 |
| `make` 内存不足 | `-j` 并发度 | 降低到 `make -j2` 或 `make -j4` |
| `make` 第一个错误不明显 | 首个真正 compiler error | 不要只看最后的汇总，保存完整日志 |
| `UE4Editor` 无法启动 | GPU、Vulkan、共享库、权限 | 检查 `ldd`、`vulkaninfo`、驱动和 `-log` |
| `.uproject` 版本不匹配 | `EngineAssociation` | 用当前 UE4.27 Editor 重新生成项目文件 |
| Linux 打包失败 | 第一个 Cook/Build 错误 | 先用 Development，检查地图、插件和 SDK |
| 打包程序没有执行权限 | 文件 mode | `chmod +x`，检查归档是否来自 Linux |
| Cirrus 启动失败 | Node、npm 依赖、端口 | `npm install`，检查 `node_modules` 和 `ss -ltnp` |
| Arch 的 `setup.sh` 因 apt 失败 | 脚本只支持 apt-get 安装 Coturn | `pacman -S nodejs npm coturn` 后手动 `npm install` |
| 网页打不开 | `HttpPort`、监听地址、防火墙 | 先访问 `127.0.0.1:18088` |
| 网页能开但没有画面 | UE 进程、StreamerPort、编码器 | 查 Cirrus 的 `Streamer connected` 和 UE 日志 |
| 本机可用、局域网不可用 | `PublicIp`、防火墙、网卡 IP | 改成局域网 IPv4，放行 18088/18777 |
| 局域网可用、公网不可用 | NAT、TURN、HTTPS、安全组 | 检查 ICE candidate pair 和 TURN 日志 |
| 有画面但没有声音 | `-AudioMixer`、浏览器静音 | 检查应用音频设备和 UE 日志 |
| 窗口最小化后停流 | 是否使用离屏运行 | 推流时加 `-RenderOffscreen -ForceRes` |
| 没有 CSV | capture 参数、Saved 路径、退出方式 | 正常退出后递归查找 `Profiling/CSV` |

排障顺序建议是：

1. 先让 UE Editor 打开一个本地项目；
2. 再让打包 Linux 项目本地运行；
3. 再单机启动 Cirrus；
4. 再让 UE 连接 StreamerPort；
5. 最后测试局域网、TURN 和公网。

一次只修改一个变量，保留日志和命令行，避免把多个独立问题混在一起。

---

## 16. 本仓库相关文件

```text
UE4.27/Setup.sh
UE4.27/Engine/Build/Commit.gitdeps.xml
UE4.27/Engine/Build/BatchFiles/Linux/SetupMono.sh
UE4.27/Engine/Build/BatchFiles/Linux/GitDependencies.sh
UE4.27/Engine/Build/BatchFiles/Linux/Build.sh
UE4.27/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh
UE4.27/Engine/Build/BatchFiles/RunUAT.sh
PixelStreamingInfrastructure-UE4.27/SignallingWebServer/config.json
PixelStreamingInfrastructure-UE4.27/SignallingWebServer/platform_scripts/bash/setup.sh
PixelStreamingInfrastructure-UE4.27/SignallingWebServer/platform_scripts/bash/run_local.sh
```

修改 UE 依赖 manifest、Mono 选择逻辑或 Cirrus 配置后，应在文档中同步记录实际路径和命令。不要把密码、私钥、TURN 凭据或生产证书提交到仓库。

---

## 17. 官方参考资料

- [Downloading Unreal Engine Source Code](https://dev.epicgames.com/documentation/en-us/unreal-engine/downloading-unreal-engine-source-code?application_version=4.27)
- [Building Unreal Engine from Source](https://dev.epicgames.com/documentation/en-us/unreal-engine/building-unreal-engine-from-source?application_version=4.27)
- [Hardware and Software Specifications](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications?application_version=4.27)
- [Compiling Game Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/compiling-game-projects?application_version=4.27)
- [Packaging Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-projects?application_version=4.27)
- [Pixel Streaming Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/pixel-streaming-overview?application_version=4.27)
- [Getting Started with Pixel Streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/getting-started-with-pixel-streaming?application_version=4.27)
- [Pixel Streaming Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/pixel-streaming-reference?application_version=4.27)
- [Hosting and Networking Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/hosting-and-networking-guide?application_version=4.27)
- [EpicGames/PixelStreamingInfrastructure](https://github.com/EpicGames/PixelStreamingInfrastructure)
