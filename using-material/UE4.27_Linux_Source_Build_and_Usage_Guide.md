# UE4.27 Linux Source Build, Project Packaging and Pixel Streaming Guide

> Applicable version: Unreal Engine 4.27.2, Linux x86_64, the current `xmu-cloud-render-cs` repository.  
> Recommended distribution: Arch Linux; Ubuntu/Debian can also follow this guide, but package names and the Node.js installation method may differ.  
> Example workspace: `/mnt/linux_data/git/xmu-cloud-render-cs`. Replace the variables in the commands with your actual paths, project names and network interface addresses.

This document is the Linux version of `original-material/UE4.27_Windows源码编译与使用手册.md`. The Windows-specific Visual Studio, `.bat`, Win64 and Windows Firewall steps have been replaced with the Linux toolchain, shell scripts, Linux build targets and `systemd`/firewall commands.

The current repository already contains:

```text
xmu-cloud-render-cs/
├── Commit.gitdeps.xml                         # Patched UE dependency manifest in the parent directory
├── UE4.27/                                    # UE4.27.2 source
├── PixelStreamingInfrastructure-UE4.27/      # Pixel Streaming infrastructure for UE4.27
├── original-material/                         # Original Windows manual and reference material
└── using-material/                            # Converted usage manuals
```

The current repository has no dedicated business `.uproject`; the projects in `UE4.27/Samples` and `UE4.27/Templates` are used to verify the engine. The actual Pixel Streaming project should be placed outside the UE source directory, for example:

```text
/mnt/ue-workspace/
├── xmu-cloud-render-cs/                        # This repository
└── Project/Testdemo/                           # External UE4.27 project
    ├── Testdemo.uproject
    ├── Content/
    ├── Config/
    ├── Source/                                 # C++ projects only
    └── Saved/
```

---

## 1. Components and Linux Paths

| Component | Purpose | Location in the current repository or example |
| --- | --- | --- |
| UE4.27 source | Engine source and build tooling | `UE4.27/` |
| UE4Editor | Linux editor | `UE4.27/Engine/Binaries/Linux/UE4Editor` |
| UE project | Levels, blueprints, C++ code and assets | `/mnt/ue-workspace/Project/Testdemo/` |
| Pixel Streaming plugin | Captures picture/audio and streams it over WebRTC | UE4.27 engine plugin or project plugin |
| Cirrus | Signalling Web Server that serves the web page and the WebSocket signalling | `PixelStreamingInfrastructure-UE4.27/SignallingWebServer/` |
| Coturn | STUN/TURN service that handles WebRTC connections on complex networks | System `coturn` or the Infrastructure scripts |
| Node.js | Runs Cirrus | Node 16 bundled with the Infrastructure, or system Node |

A few concepts need to be distinguished:

- **Build the engine**: compile the UE source into `UE4Editor`, `UE4Game` and related tools.
- **Build the project**: compile the project's C++ modules; pure blueprint projects usually do not need a project C++ build target.
- **Cook**: convert materials, textures, maps and other assets into the target platform format.
- **Package**: organize the code and the cooked assets into a runnable directory.
- **Pixel Streaming**: encode the UE picture and audio and send them to a browser over WebRTC.
- `StreamerPort` is the WebSocket port that UE connects to Cirrus on; `HttpPort` is the HTTP port the browser opens the web page on. The two must not be mixed up.

---

## 2. Hardware, System and Disk Requirements

### 2.1 Recommended Hardware

- 64-bit Linux;
- 16 GB of RAM as a starting point, 32 GB or more recommended;
- Multi-core CPU;
- At least 150 GB free on an SSD; the full dependencies, build intermediates, shader cache and project assets may need more space;
- A GPU with hardware encoding support;
- Network access to GitHub and the Epic CDN.

The first build produces a large amount of intermediate files. `Engine/Intermediate`, `Engine/DerivedDataCache` and the project `Intermediate` directory should not be placed on a small partition.

### 2.2 GPU and Hardware Encoding

UE4.27 Pixel Streaming requires a working video encoder:

- NVIDIA: the driver and GPU support NVENC;
- AMD: the driver and GPU support AMF;
- On Linux, first confirm that Vulkan/OpenGL rendering and the GPU driver work correctly.

On NVIDIA you can check with:

```bash
nvidia-smi
```

If you see `No compatible GPU found`, encoder loading failures or a black screen, first troubleshoot the GPU driver, permissions and the remote session environment, and only then troubleshoot Pixel Streaming.

### 2.3 Arch Linux Base Tools

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

Check the versions:

```bash
git --version
mono --version
msbuild --version
clang --version
node --version
npm --version
vulkaninfo --summary
```

UE4.27 ships a historical Mono build, but the bundled Mono in the current repository may lack an executable `mono-boehm`. This repository has already modified the Linux `SetupMono.sh`: if the bundled Mono executable does not exist, it uses the system Mono and clears the conflicting `MONO_PATH`. Therefore the system Mono should be preferred on Arch Linux.

### 2.4 File Handle and Process Limits

The UE editor and the compiler open a large number of files. Check first:

```bash
ulimit -n
```

Temporarily raise the limit for the current shell:

```bash
ulimit -n 65536
```

If you still hit `Too many open files`, configure `/etc/security/limits.conf` for the user or systemd user limits, log in again and verify. Do not only modify the current terminal while actually running UE inside another service process.

---

## 3. Getting and Inspecting the Current Repository

If you already have this repository, you can skip the clone step:

```bash
cd /mnt/linux_data/git
git clone git@github.com:27652/xmu-cloud-render-cs.git
cd xmu-cloud-render-cs
```

Check the UE version:

```bash
cat UE4.27/Engine/Build/Build.version
```

It should contain:

```json
{
    "MajorVersion": 4,
    "MinorVersion": 27,
    "PatchVersion": 2,
    "CompatibleChangelist": 17155196,
    "BranchName": "++UE4+Release-4.27"
}
```

Check the Pixel Streaming branch:

```bash
git -C PixelStreamingInfrastructure-UE4.27 branch --show-current 2>/dev/null || true
git -C PixelStreamingInfrastructure-UE4.27 log -1 --oneline
```

The infrastructure version must match UE4.27; do not mix the Cirrus scripts from the UE5 `master` branch into this directory.

### 3.1 UE Source Permissions

If you re-obtain the source from Epic's official repository, you need an Epic Games account linked to your GitHub account with access to `EpicGames/UnrealEngine`. You can use SSH:

```bash
git clone --branch 4.27.2-release --single-branch \
  git@github.com:EpicGames/UnrealEngine.git UE4.27
```

You can also use HTTPS:

```bash
git clone --branch 4.27.2-release --single-branch \
  https://github.com/EpicGames/UnrealEngine.git UE4.27
```

If GitHub returns 404, first confirm that the account behind the current Git credentials has been authorized by Epic. Git source permissions and the binary dependency CDN are not the same thing.

---

## 4. Downloading the UE4.27 Binary Dependencies

### 4.1 The Patched Manifest in the Current Repository

Epic's `GitDependencies.exe` reads `Engine/*/Build/*.gitdeps.xml`. The parent directory of this repository also provides a patched version:

```text
/mnt/linux_data/git/xmu-cloud-render-cs/Commit.gitdeps.xml
```

It uses a new set of dependency packs (for example `UnrealEngine-18310727`), while some of the older packs in the original UE4.27 manifest return HTTP 403. `UE4.27/Setup.sh` already handles this difference: at runtime it prefers the `Commit.gitdeps.xml` in the parent directory, converts the CDN address to HTTPS, and writes it to `UE4.27/Engine/Build/Commit.gitdeps.xml`.

Therefore you must run it from the UE directory of the current repository:

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
./Setup.sh
```

If you are using a separately copied UE directory without the patched manifest in the parent directory, first place `Commit.gitdeps.xml` one level above the UE directory, or manually copy the correct version into `Engine/Build/`.

### 4.2 Setup Output and Failure Handling

Normal output looks like this:

```text
Registering git hooks...
Using system mono: /usr/bin/mono
Checking dependencies...
Updating dependencies...
```

If you see:

```text
Corlib not in sync with this runtime
```

confirm that the system Mono has been selected:

```bash
command -v mono
mono --version
```

The `SetupMono.sh` in this repository should print:

```text
Using system mono: /usr/bin/mono
```

If you get a CDN 403, first confirm that the manifest actually in use has switched:

```bash
grep -o 'BaseUrl="[^"]*"' Engine/Build/Commit.gitdeps.xml | head -1
grep -o 'UnrealEngine-[0-9A-Za-z_-]*' Engine/Build/Commit.gitdeps.xml \
  | sort -u | tail
grep -n '12245497' Engine/Build/Commit.gitdeps.xml
```

The current patched manifest should use `https://cdn.unrealengine.com/dependencies`, and the old `12245497` revision should no longer appear. If the old revision still appears, the `Setup.sh` being executed is not the one from the current repository, or the parent directory manifest is missing.

You can perform a check without downloading anything first:

```bash
mono Engine/Binaries/DotNET/GitDependencies.exe --dry-run --threads=1
```

Do not treat `remote.origin` or `remote.ue` as the binary dependency mirror address. A Git remote is only responsible for Git objects; GitDependencies uses the CDN `BaseUrl` and `RemotePath` from the manifest.

### 4.3 Cache

By default GitDependencies uses the following location under the parent Git repository:

```text
.git/ue4-gitdeps/
```

If another machine on the team has already successfully downloaded the cache for the same manifest, you can copy that directory to `.git/ue4-gitdeps/` in the current repository. The cache must come from the same UE version and the same dependency manifest; do not copy only part of it and assume every file is available.

---

## 5. Generating the Linux Project Files

After the dependencies have been downloaded:

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
./GenerateProjectFiles.sh
```

This script builds or updates UnrealBuildTool and generates the Makefile, CMake files and other project files. After success, check:

```bash
test -f Makefile && echo "Makefile exists"
test -f Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh && echo "Linux generator exists"
```

Generate the project files for an external project:

```bash
./GenerateProjectFiles.sh \
  -project="/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -game -engine
```

If you are told that `UnrealBuildTool.exe`, Mono or dependency files are missing, first re-run `Setup.sh` and the system dependency checks above.

---

## 6. Building the UE4.27 Linux Editor

### 6.1 Using Make

Run it in the UE root directory:

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
make
```

You can usually also build the common targets explicitly:

```bash
make UE4Editor
make ShaderCompileWorker
make UnrealLightmass
make UnrealPak
```

Build in parallel:

```bash
make -j"$(nproc)"
```

If memory is tight, do not blindly use all CPU threads; lower the concurrency instead:

```bash
make -j4
```

Debug configuration:

```bash
make UE4Editor-Linux-Debug
```

Clean and rebuild from scratch:

```bash
make UE4Editor ARGS="-clean"
make -j"$(nproc)" UE4Editor
```

`-clean` significantly increases build time. Use it only when the target files, build configuration or intermediates are genuinely inconsistent.

### 6.2 Using Build.sh

You can also build through UnrealBuildTool:

```bash
cd /mnt/linux_data/git/xmu-cloud-render-cs/UE4.27
Engine/Build/BatchFiles/Linux/Build.sh \
  UE4Editor Linux Development \
  -WaitMutex -NoHotReload
```

For an external C++ project:

```bash
Engine/Build/BatchFiles/Linux/Build.sh \
  TestdemoEditor Linux Development \
  -Project="/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -WaitMutex -NoHotReload
```

The target name must come from the project's `Target.cs`. If a pure blueprint project has no `TestdemoEditor.Target.cs`, do not artificially create or invoke a target that does not exist.

### 6.3 Verify the Editor

```bash
test -x Engine/Binaries/Linux/UE4Editor && echo "UE4Editor exists"
./Engine/Binaries/Linux/UE4Editor -version
```

Open the project:

```bash
./Engine/Binaries/Linux/UE4Editor \
  "/mnt/ue-workspace/Project/Testdemo/Testdemo.uproject" \
  -log
```

You can also open a template that ships with the engine, for example:

```bash
./Engine/Binaries/Linux/UE4Editor \
  Templates/TP_ThirdPerson/TP_ThirdPerson.uproject \
  -log
```

The first launch generates the shader cache and the Derived Data Cache, so sustained CPU, disk and memory usage is normal. Do not force-kill the process just because the window is briefly unresponsive; check the log and disk activity first.

---

## 7. Building and Packaging the Project on Linux

### 7.1 Project Checks

After opening the project, check:

- The default map in Maps & Modes;
- The build configuration in Packaging;
- Whether the maps to be packaged are added to `List of Maps to Include in a Packaged Build`;
- Whether the Pixel Streaming plugin is enabled;
- Whether the project runs correctly as a Standalone Game;
- Whether the project plugins support Linux;
- Whether a C++ project has the `Target.cs` required for the Linux target.

The Windows `.exe` packaging result cannot be used directly on Linux. The target directory is usually `LinuxNoEditor`, and the project should be re-cooked/re-packaged on Linux.

### 7.2 Using RunUAT.sh

The current repository with UE4.27 contains:

```text
UE4.27/Engine/Build/BatchFiles/RunUAT.sh
```

A typical Linux packaging command:

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

Find the result:

```bash
find "$ARCHIVE" -type f -executable -name 'Testdemo' -print
find "$ARCHIVE" -maxdepth 3 -type f -name '*.uproject' -print
```

UAT arguments and third-party SDK requirements differ between projects. When troubleshooting, first remove `-pak` or use the Development configuration and resolve the first real build/cook error.

### 7.3 Packaging from within the Editor

You can also choose the following in the editor:

```text
File -> Package Project -> Linux
```

It is recommended to place the output directory outside the project or in a clearly defined archive directory, for example:

```text
/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux
```

---

## 8. Running the Project Locally

First confirm that the project itself runs without Pixel Streaming.

### 8.1 Windowed Run

```bash
GAME=/mnt/ue-workspace/Project/Testdemo/Packaged_SourceUE_Linux/LinuxNoEditor/Testdemo

"$GAME" \
  -windowed -ResX=1920 -ResY=1080 \
  -log \
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

If the project binary is not executable:

```bash
chmod +x "$GAME"
```

A graphical session, DISPLAY/Wayland, GPU drivers and audio devices must all be available. Headless servers should use offscreen mode and a correct Vulkan/rendering environment.

### 8.2 Offscreen Run

```bash
"$GAME" \
  -RenderOffscreen -ForceRes \
  -ResX=1920 -ResY=1080 \
  -log \
  -ExecCmds="t.MaxFPS 60,r.VSync 0,stat fps,stat unit"
```

`-RenderOffscreen` only hides the local window; it does not mean Pixel Streaming is enabled. Server deployments usually use this flag to avoid pausing rendering when the window is minimized.

### 8.3 Common Launch Parameters

| Parameter | Purpose |
| --- | --- |
| `-windowed` | Windowed mode |
| `-RenderOffscreen` | Offscreen rendering, commonly used on servers |
| `-ForceRes` | Force the use of `-ResX/-ResY` |
| `-ResX` / `-ResY` | Set the resolution |
| `-AudioMixer` | Enable the audio mixing required by Pixel Streaming |
| `-log` | Output the log |
| `-ExecCmds="..."` | Execute console commands at startup |
| `-PixelStreamingURL=ws://...` | WebSocket connection to Cirrus |

---

## 9. Enabling Pixel Streaming

1. Open the project with the source-built `UE4Editor`;
2. Go to `Edit -> Plugins`;
3. Enable `Pixel Streaming`;
4. Restart the editor;
5. Test with `Standalone Game` first; do not treat the ordinary PIE viewport as the final streaming result;
6. Rebuild/repackage the Linux project.

Add `-AudioMixer` when launching the streaming application. The UE4.27 Pixel Streaming plugin parameters are not fully compatible with the UE5 version, so do not copy UE5 manual commands directly.

---

## 10. Installing and Starting Cirrus on Linux

### 10.1 The Current Infrastructure Directory

```bash
export PS_ROOT=/mnt/linux_data/git/xmu-cloud-render-cs/PixelStreamingInfrastructure-UE4.27
export SIGNAL_ROOT="$PS_ROOT/SignallingWebServer"
cd "$SIGNAL_ROOT"
```

If you need to re-obtain the correct branch:

```bash
cd /mnt/ue-workspace
git clone --branch UE4.27 --single-branch --depth 1 \
  https://github.com/EpicGames/PixelStreamingInfrastructure.git \
  PixelStreamingInfrastructure-UE4.27
```

### 10.2 Node.js Dependencies

The Linux `setup.sh` of the Infrastructure targets `apt-get` based distributions by default and downloads Node 16.14.2. Running it directly on Arch Linux may fail during the Coturn installation step. You can first run:

```bash
cd "$SIGNAL_ROOT"
platform_scripts/bash/setup.sh
```

If the script stops because of an `apt-get` or Coturn installation failure, install them manually:

```bash
sudo pacman -S --needed nodejs npm coturn
npm install
```

If the project needs to use the Node bundled with the Infrastructure instead of the system Node:

```bash
cd "$SIGNAL_ROOT"
platform_scripts/bash/setup.sh
```

Check:

```bash
node --version
npm --version
test -x platform_scripts/bash/node/bin/node && \
  platform_scripts/bash/node/bin/node --version
```

The project scripts require a Node version close to 16.x at minimum. If the current Arch Node version is too new and changes dependency behavior, use the Node bundled with the project or install Node 16 with a version manager.

### 10.3 Configuring config.json

The configuration file in the current repository:

```text
PixelStreamingInfrastructure-UE4.27/SignallingWebServer/config.json
```

The current default values include:

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

For local development it is recommended to switch to high ports that do not require root:

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

Port meanings:

| Purpose | Example port | Who connects |
| --- | ---: | --- |
| Browser HTTP | 18088 | The browser opens `http://127.0.0.1:18088/` |
| UE Streamer | 18777 | UE connects to `ws://127.0.0.1:18777` |
| HTTPS | 443 | Only used after certificates are configured and HTTPS is enabled |

`PublicIp` should be changed to the LAN IPv4 address of the host running Cirrus during LAN testing; do not leave `localhost` as an address that other machines are expected to reach.

### 10.4 Starting Cirrus

Start Cirrus on the current branch directly:

```bash
cd "$SIGNAL_ROOT"
NO_SUDO=1 platform_scripts/bash/run_local.sh \
  --publicIp=127.0.0.1 \
  --httpPort=18088 \
  --streamerPort=18777
```

If the script version does not accept the port arguments, use the configuration file and Node directly:

```bash
cd "$SIGNAL_ROOT"
PATH="$SIGNAL_ROOT/platform_scripts/bash/node/bin:$PATH" \
  "$SIGNAL_ROOT/platform_scripts/bash/node/bin/node" \
  cirrus.js --configFile=config.json
```

Keep this terminal running. In another terminal, check:

```bash
ss -ltnp | grep -E ':18088|:18777'
curl -I http://127.0.0.1:18088/
```

On success you should see the HTTP service listening, and the web page should open in a browser.

---

## 11. Starting the Linux Pixel Streaming Application

Start Cirrus first, then start the already packaged Linux application.

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

For a connection-only test you can use the minimal command:

```bash
"$GAME" \
  -RenderOffscreen -AudioMixer -ForceRes \
  -ResX=1920 -ResY=1080 \
  -PixelStreamingURL="ws://127.0.0.1:18777" \
  -log
```

You should see a message about connecting to Cirrus in the UE log, and the Cirrus terminal should print something like `Streamer connected`. UE uses `StreamerPort` while the browser uses `HttpPort`:

```text
UE -> ws://127.0.0.1:18777
Browser -> http://127.0.0.1:18088/
```

Open the browser:

```text
http://127.0.0.1:18088/
```

A web page that opens only means the HTTP service is healthy; seeing a picture also requires UE, the Streamer WebSocket, WebRTC, the encoder and browser decoding to all work.

---

## 12. LAN, Public Network and TURN

### 12.1 LAN Access

View the addresses of the Linux host:

```bash
ip -br address
hostname -I
```

Assuming the host address is `192.168.1.10`, change `PublicIp` in `config.json` to that address and open it from another device:

```text
http://192.168.1.10:18088/
```

On systems using `ufw`, allow the ports:

```bash
sudo ufw allow from 192.168.1.0/24 to any port 18088 proto tcp
sudo ufw allow from 192.168.1.0/24 to any port 18777 proto tcp
```

When using firewalld:

```bash
sudo firewall-cmd --permanent --add-port=18088/tcp
sudo firewall-cmd --permanent --add-port=18777/tcp
sudo firewall-cmd --reload
```

Only allow the trusted network ranges you actually use; do not permanently expose development ports to the public internet.

### 12.2 STUN/TURN

STUN/TURN is usually required across different networks, behind NAT, corporate firewalls or cloud security groups:

- STUN helps endpoints discover their external address;
- TURN relays media when endpoints cannot connect directly;
- `peerConnectionOptions` passes the ICE servers to WebRTC;
- The UDP/TCP ports of TURN must also be allowed in the cloud security group, the host firewall and NAT mappings.

Example:

```json
{
  "peerConnectionOptions": "{\"iceServers\":[{\"urls\":[\"turn:203.0.113.10:3478?transport=udp\",\"turn:203.0.113.10:3478?transport=tcp\"],\"username\":\"replace_user\",\"credential\":\"replace_password\"}]}"
}
```

`203.0.113.10` is a documentation-reserved address and cannot be used directly. Do not commit real TURN passwords to Git.

The current Infrastructure provides:

```text
platform_scripts/bash/Start_TURNServer.sh
platform_scripts/bash/Start_WithTURN_SignallingServer.sh
platform_scripts/bash/turn_user_pwd.sh
```

Read the scripts and the branch README before deciding whether to use them; the parameters may differ between UE4.27 snapshots. A running TURN process does not mean media is actually being relayed; check the selected candidate pair in the browser at `chrome://webrtc-internals`.

### 12.3 HTTPS/WSS

Development machines can use HTTP/WS; public production environments should configure HTTPS/WSS, access control, authentication, log protection and process supervision. Do not permanently open all ports and disable authentication just for a quick test.

---

## 13. Logs, CSV and GPU Performance Data

### 13.1 UE Logs

Common locations:

```text
Project/Saved/Logs/
PackagedDirectory/LinuxNoEditor/Testdemo/Saved/Logs/
```

Find the logs:

```bash
find /mnt/ue-workspace/Project/Testdemo -type f -name '*.log' \
  -printf '%T@ %p\n' | sort -nr | head
```

Search for key errors:

```bash
grep -RniE 'Error:|Fatal|PixelStreaming|WebRTC|NVENC|AMF|Streamer' \
  /mnt/ue-workspace/Project/Testdemo --include='*.log' | head -100
```

### 13.2 CSV Profiler

`-csvCaptureFrames=18000` captures roughly 5 minutes at 60 FPS. Wait for UE to exit normally before copying the CSV; force-killing the process may leave the last segment of data unclosed.

```bash
find /mnt/ue-workspace/Project/Testdemo -type f -name '*.csv' \
  -path '*Profiling/CSV*' -print
```

You need to distinguish between:

- UE render FPS;
- Encoding FPS;
- Browser receive/decode FPS;
- Network bitrate, packet loss and round-trip latency.

They are not the same metric.

### 13.3 NVIDIA GPU Sampling

```bash
mkdir -p /mnt/ue-workspace/logs/manual_pixel_streaming/raw
nvidia-smi \
  --query-gpu=timestamp,utilization.gpu,utilization.memory,memory.used,power.draw \
  --format=csv -l 1 \
  > /mnt/ue-workspace/logs/manual_pixel_streaming/raw/gpu.csv
```

Press `Ctrl+C` to stop sampling. For AMD GPUs use the corresponding driver and monitoring tools; do not treat `nvidia-smi` output as a universal GPU metric.

---

## 14. Stopping Programs and systemd

Recommended order:

1. Let UE exit normally;
2. Wait for the logs and CSV to finish writing;
3. Press `Ctrl+C` in the Cirrus terminal;
4. Stop GPU sampling or Coturn last.

Look up the PID first, then stop the specific target:

```bash
pgrep -a -f 'Testdemo|UE4Editor|cirrus.js'
kill <PID>
```

If the program does not respond, confirm the PID before using:

```bash
kill -TERM <PID>
sleep 5
kill -KILL <PID>
```

Do not use vague commands such as `killall mono` or `pkill -f UE` directly, as they may terminate processes belonging to other users or other projects.

For long-running use you can create a systemd user service, but you should first confirm the command, environment variables, GPU and working directory in an interactive terminal. Example:

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

Enable it:

```bash
systemctl --user daemon-reload
systemctl --user enable --now cirrus-ue427.service
journalctl --user -u cirrus-ue427.service -f
```

The Node path in `ExecStart` must actually exist; if only the system Node is installed, change it to the absolute path reported by `command -v node`.

---

## 15. Common Troubleshooting

| Symptom | First thing to check | Direction |
| --- | --- | --- |
| `Setup.sh` download returns 403 | The revision in `Engine/Build/Commit.gitdeps.xml` | Confirm the patched manifest from the parent directory has been copied; changing only `remote.ue` is not enough |
| `Corlib not in sync` | `MONO_PATH`, bundled Mono executable | Use this repository's `SetupMono.sh` and confirm the system Mono is selected |
| Cannot find `mono`/`xbuild` | System packages and PATH | Install `mono` and `msbuild`, reopen the terminal |
| `GenerateProjectFiles.sh` fails | `.ue4dependencies` and UBT | Complete Setup first, then generate the project files |
| `make` runs out of memory | `-j` concurrency | Lower it to `make -j2` or `make -j4` |
| The first error in `make` is not obvious | The first real compiler error | Do not only look at the final summary; save the complete log |
| `UE4Editor` will not start | GPU, Vulkan, shared libraries, permissions | Check `ldd`, `vulkaninfo`, the drivers and `-log` |
| `.uproject` version mismatch | `EngineAssociation` | Regenerate the project files with the current UE4.27 editor |
| Linux packaging fails | The first cook/build error | Start with Development, check the maps, plugins and SDK |
| Packaged program is not executable | File mode | `chmod +x`, and check that the archive came from Linux |
| Cirrus fails to start | Node, npm dependencies, ports | `npm install`, check `node_modules` and `ss -ltnp` |
| Arch `setup.sh` fails because of apt | The script only supports apt-get for installing Coturn | Run `pacman -S nodejs npm coturn`, then `npm install` manually |
| The web page will not open | `HttpPort`, listen address, firewall | First try `127.0.0.1:18088` |
| The web page opens but shows no picture | UE process, StreamerPort, encoder | Check Cirrus for `Streamer connected` and check the UE log |
| Works locally but not on the LAN | `PublicIp`, firewall, NIC address | Change to the LAN IPv4 and allow 18088/18777 |
| Works on the LAN but not on the public network | NAT, TURN, HTTPS, security group | Check the ICE candidate pair and the TURN log |
| Picture but no sound | `-AudioMixer`, browser mute | Check the application audio device and the UE log |
| Streaming stops after the window is minimized | Whether offscreen running is used | Add `-RenderOffscreen -ForceRes` when streaming |
| No CSV | Capture parameters, Saved path, exit method | Search recursively for `Profiling/CSV` after a normal exit |

The recommended troubleshooting order is:

1. First get the UE Editor to open a local project;
2. Then get the packaged Linux project to run locally;
3. Then start Cirrus on a single machine;
4. Then let UE connect to StreamerPort;
5. Finally test the LAN, TURN and the public network.

Change only one variable at a time and keep the logs and command lines, so that independent problems are not mixed together.

---

## 16. Relevant Files in This Repository

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

After modifying the UE dependency manifest, the Mono selection logic or the Cirrus configuration, record the actual paths and commands in the documentation. Do not commit passwords, private keys, TURN credentials or production certificates to the repository.

---

## 17. Official Reference Material

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
