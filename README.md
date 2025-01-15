
# Kunlun

Kunlun 是一个基于 C 语言实现的高效、轻量级系统监控工具，实时采集服务器性能指标并通过 HTTP 上报，助力记录服务器状态。

---

## 功能特性

- **实时监控**：采集 CPU、内存、磁盘、网络等关键性能指标。
- **灵活上报**：支持自定义上报地址和监测间隔。
- **轻量高效**：基于 C 语言实现，资源占用极低。
- **易于部署**：提供一键安装脚本，支持 systemd 守护进程。
- **跨平台支持**：兼容主流 Linux 发行版（Ubuntu、CentOS、Debian、Arch 等）。

---

## 快速开始

### 1. 安装 Kunlun

使用以下命令下载并运行安装脚本：

```bash
curl -L https://github.com/hochenggang/kunlun/raw/refs/tags/v0.0.1/kunlun-client-install.sh -o kunlun-client-install.sh
chmod +x kunlun-client-install.sh
./kunlun-client-install.sh
```

按照提示输入监测间隔（秒）和上报地址即可完成安装。

### 2. 查看服务状态

安装完成后，Kunlun 会自动启动。您可以使用以下命令查看服务状态：

```bash
systemctl status kunlun
```

### 3. 卸载 Kunlun

如果需要卸载 Kunlun，可以重新运行安装脚本并选择卸载选项：

```bash
./kunlun-client-install.sh
```

选择 `2. 卸载 Kunlun` 即可完成卸载。

---

## 安装步骤

### 1. 依赖要求

- **curl**：用于下载 Kunlun 二进制文件。
- **systemd**：用于服务守护（支持主流 Linux 发行版）。
- **gcc**：用于编译 C 代码（如果需要从源码构建）。

### 2. 安装脚本说明

安装脚本会自动完成以下操作：
1. 安装 `curl`。
2. 引导用户输入监测间隔和上报地址。
3. 校验上报地址。
4. 拉取 Kunlun 二进制文件。
5. 配置并启动 systemd 服务。

---

## 卸载步骤

卸载脚本会自动完成以下操作：
1. 停止并禁用 Kunlun 服务。
2. 删除 systemd 服务文件。
3. 删除 Kunlun 二进制文件。

---

## 配置说明

### 1. 监测间隔

监测间隔是指 Kunlun 采集系统指标并上报的时间间隔（单位：秒）。默认值为 10 秒。


### 2. 上报地址

上报地址是指 Kunlun 将采集到的指标数据发送到的 HTTP 地址。为了确保地址有效，Kunlun 会首先向该地址发送一个 GET 请求，并验证返回内容是否包含 `kunlun`。如果验证通过，Kunlun 会将采集到的指标数据通过 POST 请求以表单形式周期性的发送到该地址。

#### 数据字段

Kunlun 采集的指标数据包括以下字段：

| 字段名           | 描述                         | 示例值       |
|------------------|------------------------------|--------------|
| `uptime`         | 系统运行时间（秒）            | `1485256`    |
| `load_1min`      | 1 分钟平均负载                | `0.02`       |
| `load_5min`      | 5 分钟平均负载                | `0.03`       |
| `load_15min`     | 15 分钟平均负载               | `0.00`       |
| `total_tasks`    | 总任务数                      | `512`        |
| `running_tasks`  | 运行中的任务数                | `1`          |
| `sleeping_tasks` | 休眠中的任务数                | `0`          |
| `stopped_tasks`  | 停止的任务数                  | `512`        |
| `zombie_tasks`   | 僵尸任务数                    | `0`          |
| `cpu_user`       | 用户态 CPU 使用率（百分比）   | `2.87`       |
| `cpu_system`     | 内核态 CPU 使用率（百分比）   | `0.23`       |
| `cpu_nice`       | 优先级调整 CPU 使用率（百分比）| `0.00`       |
| `cpu_idle`       | 空闲 CPU 时间（百分比）       | `96.89`      |
| `cpu_iowait`     | I/O 等待 CPU 时间（百分比）   | `0.01`       |
| `cpu_irq`        | 硬件中断 CPU 时间（百分比）   | `0.00`       |
| `cpu_softirq`    | 软件中断 CPU 时间（百分比）   | `0.01`       |
| `cpu_steal`      | 虚拟机偷取 CPU 时间（百分比） | `0.00`       |
| `mem_total`      | 总内存大小（KB）              | `2027040`    |
| `mem_free`       | 空闲内存大小（KB）            | `87460`      |
| `mem_used`       | 已用内存大小（KB）            | `1939580`    |
| `net_tx`         | 总发送数据量（MB）            | `127804`     |
| `net_rx`         | 总接收数据量（MB）            | `464394`     |
| `disk_delay`     | 磁盘延迟（微秒）              | `2872`       |
| `tcp_connections`| 当前 TCP 连接数               | `11`         |
| `cpu_delay`      | CPU 延迟（微秒）              | `123456`     |

#### 数据格式

采集到的数据会以 `name1=value1&name2=value2` 的形式拼接，并通过 HTTP POST 请求发送到上报地址。例如：

```
uptime=1485256&load_1min=0.02&load_5min=0.03&load_15min=0.00&total_tasks=512&running_tasks=1&sleeping_tasks=0&stopped_tasks=512&zombie_tasks=0&cpu_user=2.87&cpu_system=0.23&cpu_nice=0.00&cpu_idle=96.89&cpu_iowait=0.01&cpu_irq=0.00&cpu_softirq=0.01&cpu_steal=0.00&mem_total=2027040&mem_free=87460&mem_used=1939580&net_tx=127804&net_rx=464394&disk_delay=2872&tcp_connections=11&cpu_delay=123456
```

### 3. 服务配置

Kunlun 的 systemd 服务配置文件位于 `/etc/systemd/system/kunlun.service`，内容如下：

```ini
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart=/home/your-user/bin/kunlun_amd64 -s 10 -u https://example.com
Restart=always
User=your-user
Environment=HOME=/home/your-user

[Install]
WantedBy=multi-user.target
```

- **ExecStart**：Kunlun 的启动命令，包含监测间隔和上报地址。
- **Restart**：服务崩溃后自动重启。
- **User**：运行 Kunlun 的用户。
- **Environment**：设置环境变量。


---

## 从源码构建

### 1. 克隆仓库

```bash
git clone https://github.com/hochenggang/kunlun.git
cd kunlun
```

### 2. 编译代码

使用 `gcc` 编译 Kunlun：

```bash
gcc -o kunlun_amd64 kunlun_client.c
```

---

## 常见问题

### 1. 上报地址校验失败

确保上报地址返回的内容包含 `kunlun`。例如，可以在服务器上创建一个简单的 HTTP 服务，返回 `kunlun`。

### 2. 服务启动失败

检查 `/etc/systemd/system/kunlun.service` 文件中的路径和参数是否正确，确保 Kunlun 二进制文件存在且可执行。

### 3. 如何修改监测间隔或上报地址

修改 `/etc/systemd/system/kunlun.service` 文件中的 `ExecStart` 参数，然后重启服务：

```bash
sudo systemctl daemon-reload
sudo systemctl restart kunlun
```

---

## 贡献指南

欢迎提交 Issue 或 Pull Request 为 Kunlun 贡献力量！

---

## 许可证

Kunlun 基于 [MIT 许可证](https://opensource.org/licenses/MIT) 开源。

---

## 联系我们

如有问题或建议，请 PR

---

感谢使用 Kunlun！

---
