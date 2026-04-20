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

### Runtime Command Usage

Show help/version:

```bash
./build/bin/cfddns -h
./build/bin/cfddns -V
```

Run with config file:

```bash
./build/bin/cfddns -c config/example.json
```

Show current state without entering normal loop:

```bash
./build/bin/cfddns -c config/example_multi.json -s all
./build/bin/cfddns -c config/example_multi.json -s status
./build/bin/cfddns -c config/example_multi.json -s records
./build/bin/cfddns -c config/example_multi.json -r www_ipv4
```

Run one-shot direct update (without config file):

```bash
./build/bin/cfddns \
  --once \
  --api-token "YOUR_CLOUDFLARE_API_TOKEN_HERE" \
  --zone-name "YOUR_ZONE_NAME" \
  --record-name "YOUR_RECORD_FQDN" \
  --record-type "A" \
  --ttl 300 \
  --proxied false \
  --ip "203.0.113.10"
```

Result behavior for `--once` direct update:

- On failure: prints `DDNS once update: FAILED (...)`
- On success: prints `DDNS once update: SUCCESS`
- After success: queries Cloudflare API and prints server-side record value

Supported show types for `-s`:

- `all`
- `config`
- `records`
- `status`
- `wan`
- `stats`

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

### 运行命令说明

查看帮助和版本：

```bash
./build/bin/cfddns -h
./build/bin/cfddns -V
```

使用配置文件启动：

```bash
./build/bin/cfddns -c config/example.json
```

仅查看状态（不进入常规更新循环）：

```bash
./build/bin/cfddns -c config/example_multi.json -s all
./build/bin/cfddns -c config/example_multi.json -s status
./build/bin/cfddns -c config/example_multi.json -s records
./build/bin/cfddns -c config/example_multi.json -r www_ipv4
```

一次性直更（不依赖配置文件）：

```bash
./build/bin/cfddns \
  --once \
  --api-token "YOUR_CLOUDFLARE_API_TOKEN_HERE" \
  --zone-name "YOUR_ZONE_NAME" \
  --record-name "YOUR_RECORD_FQDN" \
  --record-type "A" \
  --ttl 300 \
  --proxied false \
  --ip "203.0.113.10"
```

`--once` 直更模式结果说明：

- 失败时输出：`DDNS once update: FAILED (...)`
- 成功时输出：`DDNS once update: SUCCESS`
- 成功后会再通过 Cloudflare API 回查并打印服务器端记录值

`-s` 支持的类型：

- `all`
- `config`
- `records`
- `status`
- `wan`
- `stats`

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
