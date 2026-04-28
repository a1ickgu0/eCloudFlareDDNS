# eCloudFlare DDNS

[English](#english) | [中文](#中文)

---

## English

### Overview

eCloudFlare DDNS is a C-based Cloudflare dynamic DNS client.
It detects public IP changes and updates DNS records automatically.

### Key Features

- Cloudflare API token authentication
- IPv4 and IPv6 support
- Single-record and multi-record modes
- Multi-WAN binding and failover
- Layered modular architecture (HAL / adapter / service / app)
- Full Makefile workflow (build, test, install)

### Directory Layout

- `src/`: source code
- `include/`: public headers
- `config/`: configuration samples
- `tests/`: test code and helpers
- `build/`: generated build outputs

### Requirements

- macOS or Linux
- `gcc` or `clang`
- `make`
- `libcurl`
- `cJSON`
- `pthread`

### Build And Test Commands

Build:

```bash
make
```

Binary:

```bash
build/bin/cfddns
```

Common build targets:

```bash
make clean
make debug
make release
make help
```

Test targets:

```bash
make test-phase1
make test-phase2
make test-phase3
make test-phase4
make test-all-gcov-asan
```

Install and uninstall:

```bash
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

### Usage Modes

There are three common ways to run eCloudFlare DDNS:

#### Mode 1: Configuration File (Recommended for daemon mode)

Create your config file from the sample:

```bash
cp config/example.json config/config.json
# Edit config/config.json with your real settings
```

Run as daemon:

```bash
./build/bin/cfddns -c config/config.json
```

Or run once with config file:

```bash
./build/bin/cfddns -c config/config.json --once
```

#### Mode 2: Environment Variables (Recommended for secrets)

Environment variables avoid exposing secrets in process list (`ps`).

Set environment variables:

```bash
export CFDDNS_API_TOKEN="your_cloudflare_api_token_here"
export CFDDNS_ZONE_NAME="example.com"
export CFDDNS_RECORD_NAME="ddns.example.com"
```

Run with environment variables:

```bash
./build/bin/cfddns --once
```

Combine with CLI arguments (CLI overrides env vars):

```bash
export CFDDNS_API_TOKEN="your_token"
export CFDDNS_ZONE_NAME="example.com"
./build/bin/cfddns --once --record-name "custom.example.com" --ttl 300
```

Supported environment variables:

| Variable | Description |
|----------|-------------|
| `CFDDNS_API_TOKEN` | Cloudflare API token |
| `CFDDNS_ZONE_ID` | Cloudflare zone ID |
| `CFDDNS_ZONE_NAME` | Cloudflare zone name |
| `CFDDNS_RECORD_NAME` | DNS record name (FQDN) |

#### Mode 3: CLI Arguments (Quick one-shot update)

For quick manual updates without config or env vars:

```bash
./build/bin/cfddns \
  --once \
  --api-token "YOUR_CLOUDFLARE_API_TOKEN_HERE" \
  --zone-name "example.com" \
  --record-name "ddns.example.com" \
  --record-type "A" \
  --ttl 300 \
  --proxied false \
  --ip "203.0.113.10"
```

**Warning:** CLI arguments with secrets are visible in process list. Use environment variables for production.

#### Quick Reference: Mode Selection

| Scenario | Recommended Mode |
|----------|-----------------|
| Daemon service (continuous monitoring) | Config file |
| Scripts / CI / Automation | Environment variables |
| Quick manual test | CLI arguments |
| Docker / Kubernetes | Environment variables |

### Runtime Command Usage

Show help/version:

```bash
./build/bin/cfddns -h
./build/bin/cfddns -V
```

Show current state without entering normal loop:

```bash
./build/bin/cfddns -c config/example_multi.json -s all
./build/bin/cfddns -c config/example_multi.json -s status
./build/bin/cfddns -c config/example_multi.json -s records
./build/bin/cfddns -c config/example_multi.json -r www_ipv4
```

Supported show types for `-s`:

- `all`
- `config`
- `records`
- `status`
- `wan`
- `stats`

`--once` mode result:

- On failure: prints `DDNS once update: FAILED (...)`
- On success: prints `DDNS once update: SUCCESS`
- After success: queries Cloudflare API and prints server-side record value

### Configuration Usage

Start from:

- `config/example.json` for single-record mode
- `config/example_multi.json` for multi-record and multi-WAN mode

Single-record minimum fields:

- `cloudflare.api_token`
- `cloudflare.zone_name` (or `cloudflare.zone_id`)
- `cloudflare.record_name`
- `cloudflare.record_type` (`A` or `AAAA`)

Multi-record key sections:

- `cloudflare.api_token` and `use_shared_token`
- `wan.interfaces[]` for WAN definitions
- `records[]` for DNS record rules
- Record binding fields:
  - `binding_mode`: `fixed` / `dynamic` / `auto`
  - `wan_interface` or `wan_priority`
  - optional `forced_ip`

### Token And Git Safety

Do not commit real secrets.

- Never commit real values for `cloudflare.api_token`
- Keep local runtime config private (for example local `config/config.json`)
- Commit only sample placeholders such as `YOUR_CLOUDFLARE_API_TOKEN_HERE`

Also avoid committing generated outputs:

- `build/`
- `*.gcov`
- temporary logs and runtime artifacts

---

## 中文

### 项目简介

eCloudFlare DDNS 是一个基于 C 语言的 Cloudflare 动态 DNS 客户端。
它会检测公网 IP 变化并自动更新 DNS 记录。

### 核心特性

- Cloudflare API Token 鉴权
- 支持 IPv4 / IPv6
- 支持单记录与多记录模式
- 支持多 WAN 绑定与故障切换
- 分层架构（HAL / adapter / service / app）
- 基于 Makefile 的完整构建与测试流程

### 目录结构

- `src/`：源码
- `include/`：头文件
- `config/`：配置示例
- `tests/`：测试代码与辅助脚本
- `build/`：构建产物

### 环境依赖

- macOS 或 Linux
- `gcc` 或 `clang`
- `make`
- `libcurl`
- `cJSON`
- `pthread`

### 构建与测试命令

编译：

```bash
make
```

生成可执行文件：

```bash
build/bin/cfddns
```

常用构建命令：

```bash
make clean
make debug
make release
make help
```

常用测试命令：

```bash
make test-phase1
make test-phase2
make test-phase3
make test-phase4
make test-all-gcov-asan
```

安装与卸载：

```bash
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

### 使用模式

eCloudFlare DDNS 支持三种常用运行方式：

#### 模式一：配置文件（守护进程推荐）

从示例文件创建配置：

```bash
cp config/example.json config/config.json
# 编辑 config/config.json，填入真实配置
```

以守护进程方式运行：

```bash
./build/bin/cfddns -c config/config.json
```

或使用配置文件执行一次性更新：

```bash
./build/bin/cfddns -c config/config.json --once
```

#### 模式二：环境变量（密钥安全推荐）

使用环境变量可避免密钥在进程列表（`ps`）中暴露。

设置环境变量：

```bash
export CFDDNS_API_TOKEN="your_cloudflare_api_token_here"
export CFDDNS_ZONE_NAME="example.com"
export CFDDNS_RECORD_NAME="ddns.example.com"
```

使用环境变量运行：

```bash
./build/bin/cfddns --once
```

结合命令行参数（CLI 参数优先级高于环境变量）：

```bash
export CFDDNS_API_TOKEN="your_token"
export CFDDNS_ZONE_NAME="example.com"
./build/bin/cfddns --once --record-name "custom.example.com" --ttl 300
```

支持的环境变量：

| 变量 | 说明 |
|------|------|
| `CFDDNS_API_TOKEN` | Cloudflare API Token |
| `CFDDNS_ZONE_ID` | Cloudflare Zone ID |
| `CFDDNS_ZONE_NAME` | Zone 名称（域名） |
| `CFDDNS_RECORD_NAME` | DNS 记录名称（完整域名） |

#### 模式三：命令行参数（快速手动更新）

无需配置文件或环境变量，快速手动更新：

```bash
./build/bin/cfddns \
  --once \
  --api-token "YOUR_CLOUDFLARE_API_TOKEN_HERE" \
  --zone-name "example.com" \
  --record-name "ddns.example.com" \
  --record-type "A" \
  --ttl 300 \
  --proxied false \
  --ip "203.0.113.10"
```

**注意：** 命令行传入的密钥会在进程列表中可见。生产环境建议使用环境变量。

#### 快速参考：模式选择

| 场景 | 推荐模式 |
|------|----------|
| 守护进程（持续监控） | 配置文件 |
| 脚本 / CI / 自动化 | 环境变量 |
| 快速手动测试 | 命令行参数 |
| Docker / Kubernetes | 环境变量 |

### 运行命令说明

查看帮助和版本：

```bash
./build/bin/cfddns -h
./build/bin/cfddns -V
```

仅查看状态（不进入常规更新循环）：

```bash
./build/bin/cfddns -c config/example_multi.json -s all
./build/bin/cfddns -c config/example_multi.json -s status
./build/bin/cfddns -c config/example_multi.json -s records
./build/bin/cfddns -c config/example_multi.json -r www_ipv4
```

`-s` 支持的类型：

- `all`
- `config`
- `records`
- `status`
- `wan`
- `stats`

`--once` 模式结果说明：

- 失败时输出：`DDNS once update: FAILED (...)`
- 成功时输出：`DDNS once update: SUCCESS`
- 成功后会再通过 Cloudflare API 回查并打印服务器端记录值

### 配置文件使用说明

建议从以下文件拷贝开始：

- `config/example.json`：单记录模式
- `config/example_multi.json`：多记录 + 多 WAN 模式

单记录模式最少需要配置：

- `cloudflare.api_token`
- `cloudflare.zone_name`（或 `cloudflare.zone_id`）
- `cloudflare.record_name`
- `cloudflare.record_type`（`A` 或 `AAAA`）

多记录模式重点配置段：

- `cloudflare.api_token` 与 `use_shared_token`
- `wan.interfaces[]`：WAN 接口定义
- `records[]`：每条 DNS 记录规则
- 记录绑定字段：
  - `binding_mode`: `fixed` / `dynamic` / `auto`
  - `wan_interface` 或 `wan_priority`
  - 可选 `forced_ip`

### Token 与 Git 安全

严禁提交真实密钥。

- 不要提交真实 `cloudflare.api_token`
- 本地运行配置（例如本地 `config/config.json`）应保留在本机
- 仓库里只保留示例占位值（如 `YOUR_CLOUDFLARE_API_TOKEN_HERE`）

同时不要提交生成物：

- `build/`
- `*.gcov`
- 运行日志与临时文件
