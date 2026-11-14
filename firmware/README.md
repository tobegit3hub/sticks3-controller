# Linko Controller 服务端

Linko Controller(领慧 AI 工作台无线语音控制器)的**电脑端服务端程序**。
手持设备(StickS3)通过 BLE 把语音实时推送到这里,服务端做云端转写,并把文字**注入到光标所在位置**。
支持 Windows / macOS。

## 目录说明

| 文件 | 说明 |
|---|---|
| `ble_client.py` | 主程序:BLE 音频接收 + 转写 + 光标注入 |
| `transcribe_server.py` | 分句 / ASR / 文字清洗管线 |
| `requirements.txt` | 依赖列表 |
| `.env.example` | 配置模板,复制为 `.env` 后填写 |

> 全量包(`LinkoController`)额外含 `firmware/` 设备固件源码(用于自行烧录设备,不装到电脑上)。

## 快速开始

```bash
# 1. 安装依赖(首次)
pip install -r requirements.txt

# 2. 配置 .env:首次运行会自动从 .env.example 生成 .env,填上 ASR_KEY 后重启即可
#    (也可以手动: cp .env.example .env 然后编辑)
#    注意:.env 要和 ble_client.py 放同一个目录 —— host 包在包根目录,全量包在 host/ 目录

# 3. 首次运行先扫描并绑定设备(按屏幕上显示的 6 位配对码)
python ble_client.py --scan          # 列出附近设备,CODE 列 = 屏幕配对码
python ble_client.py --pair D4D7C6   # 照着屏幕的码绑定

# 4. 之后直接运行,自动连接已绑定设备
python ble_client.py
```

终端出现 `[+] subscribed` 后:光标停在任意输入框 → **按住设备 A 键说话 → 松手** → 文字自动出现在光标处 → 短按 B 键发送(回车)。

## 配置

ASR 用 **OpenAI 兼容的 `/chat/completions`** 接口做云端语音转写,三个变量(都在 `.env` 里):

- `ASR_KEY` — 云端转写 API 的密钥(Bearer token)
- `ASR_BASE_URL` — 兼容 chat/completions 的端点地址(不含末尾 `/chat/completions`)
- `ASR_MODEL` — 转写模型名

**换供应商只需改这三个变量,不用改代码**。`.env.example` 里已填好当前用的默认值,一般只需填 `ASR_KEY`。

文字清洗(可选):设置 `CLEANUP_MODEL` 后,对每段转写做轻量校对。默认和 ASR 共用
`ASR_KEY` / `ASR_BASE_URL`;若想用单独的账号/端点,可另配 `CLEANUP_KEY`、`CLEANUP_BASE_URL`
(不配即回落 ASR 的)。不设 `CLEANUP_MODEL` 则保留转写原样(完全不清洗)。

唤醒目标(设备 B 长按):**一个通用 `open -a` 启动器**。`.env` 里 `LINKO_WAKE_APP` 填什么就
打开什么,填 macOS 应用显示名(或带 `.` 的 bundle id):

- `LINKO_WAKE_APP=WeChat` → 长按 B 执行 `open -a WeChat`(唤醒微信并置前)
- `LINKO_WAKE_APP=钉钉` → `open -a 钉钉`
- `LINKO_WAKE_APP=com.tencent.xinWeChat` → `open -b com.tencent.xinWeChat`

**换目标软件 = 改 `.env` 里这一个值**,不用改代码、不用加文件。

`.env` 里 `LINKO_WAKE_APP` 为空时,长按 B 只打印提示,不影响语音输入链路。

## 按键功能

| 操作 | 功能 |
|---|---|
| A 长按(按住说话) | 语音输入:松手停止,文字实时注入光标 |
| A 短按 | 碎碎念速记 toggle:再点一下停止,文字进光标并保存 |
| B 短按 | 发送(模拟 Enter) |
| B 长按(0.5 秒) | 唤醒 AI 工作台 |

## 转写存储(默认 `transcripts/`,可用 `--out` 改)

```
transcripts/
└── YYYY-MM-DD/              # 每天一个目录
    ├── dictation/
    │   └── dictation.md     # 语音输入文字日志,每条 "## HH:MM:SS",超 200 条自动丢最旧
    └── memo/
        ├── memo.md          # 当天速记文字摘要,按时间追加,永不裁剪
        └── HHMMSS/          # 一次速记会话
            ├── memo.wav     # 整段音频(拼接)
            └── transcript.txt
```

语音输入不保留音频;速记永久保留文字 + 整段音频。

## 常用命令行参数

```
--scan          扫描附近的设备(JSON 输出用 --scan --json)
--pair <CODE>   按配对码绑定设备(存到 ~/.sticks3/config.json)
--address <ADDR> 直接指定设备地址/UUID
--no-inject     只复制到剪贴板,不注入光标
--no-clean      关闭文字清洗
--out <dir>     转写存储目录(默认 transcripts/)
```

## 平台注意

- **macOS**:光标注入需要"辅助功能"(Accessibility)权限。授权对象是**发起按键的顶层进程**——在终端跑就授权 `Terminal.app` / `iTerm.app`,打包成 App 就授权那个 App。授权后重启脚本生效。没有权限时文字只进剪贴板。
- **Windows**:一般无需额外权限。ASR 连不上(报 `WinError 10054`)多为公司防火墙拦截当前 ASR 端点,把 `ASR_BASE_URL` 指到网络可达的端点即可。

## 固件烧录(仅全量包含 `firmware/`,新设备需要)

> 只跑服务端的用 host 包,**跳过本节**。只有全量包(`LinkoController.zip`)带设备固件源码;
> 新设备上电前要先烧录一次固件,屏幕上才会显示配对码。

固件是 C++/ESP-IDF,必须编译一次才能烧进芯片:

```bash
# 1. 装 ESP-IDF v6.0.2(macOS / Linux,命令如下;Windows 用官方一键安装器:
#    https://dl.espressif.com/dl/esp-idf/,选 ESP-IDF v6.0.2)

#    拉取源码并安装(只装 esp32s3 的工具链;首次下载+编译依赖较久,属正常现象):
git clone -b v6.0.2 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
./install.sh esp32s3

#    装好后,每个新开的终端都要先激活环境(路径按你的安装位置):
. $HOME/esp/esp-idf/export.sh

# 2. 进固件目录编译(首次会自动下载 idf_component.yml 声明的依赖)
cd firmware
idf.py build

# 3. 烧录 + 串口监视(先查端口号,再替换到下面的命令里)
#    macOS / Linux 查端口:
ls /dev/cu.*
#    Windows 查端口(在设备管理器 > 端口(COM 和 LPT)里看,形如 COM5)
#    macOS 形如 /dev/cu.usbmodemXXXX,Windows 形如 COM5:
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

> **Windows 用户**:首次编译需要下载工具链 + 编译所有依赖,耗时较长(10~30 分钟不等),
> 不是卡死,请耐心等 `idf.py build` 跑完(第二次起就是增量编译,快很多)。

烧好后上电,LCD 显示 6 位配对码(如 `D4D7C6`),host 端用 `--scan` / `--pair` 就能配对。

> 芯片目标已在 `sdkconfig` 里锁为 `esp32s3`,新机器直接 `idf.py build`,无需 `set-target`。
