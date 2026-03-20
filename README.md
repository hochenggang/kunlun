# Kunlun

Kunlun 是一款基于 C 语言开发的轻量级 Linux 系统监控工具，专注于高效采集服务器性能指标并通过 HTTP 上报，助力运维监控与数据分析。

---

## 功能特性

- **实时监控**：周期性采集 CPU、内存、磁盘、网络等核心性能指标
- **轻量高效**：纯 C 语言实现，静态编译，无外部依赖，资源占用极低
- **灵活部署**：支持一键安装脚本，自动配置 systemd 守护进程
- **广泛兼容**：支持主流 Linux 发行版（Ubuntu、CentOS、Debian、Arch 等）
- **多架构支持**：提供 amd64 和 arm64 两种架构的预编译二进制文件

---

## 快速开始

### 安装

执行以下命令即可完成安装：

```bash
bash <(curl -sL https://github.com/hochenggang/kunlun/raw/refs/heads/main/kunlun-client-install.sh)
```

或手动下载后执行：

```bash
curl -L https://github.com/hochenggang/kunlun/raw/refs/heads/main/kunlun-client-install.sh -o kunlun-client-install.sh
chmod +x kunlun-client-install.sh
./kunlun-client-install.sh
```

按提示输入上报地址即可完成安装，服务将自动启动。

### 查看状态

```bash
systemctl status kunlun
```

### 卸载

重新运行安装脚本，选择卸载选项：

```bash
./kunlun-client-install.sh
# 选择 2. 卸载 Kunlun
```

---

## 依赖要求

- **curl**：用于下载二进制文件（安装脚本会自动安装）
- **systemd**：用于服务守护（主流发行版均已内置）

---

## 配置说明

### 上报地址验证

安装时需提供上报地址。Kunlun 会先发送 GET 请求验证地址有效性，要求返回内容包含 `kunlun` 字符串。验证通过后，将按固定间隔（10 秒）通过 POST 请求（Content-Type: application/x-www-form-urlencoded）上报逗号分隔的 35 个监控数据，数据键为 values

### 数据字段

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `timestamp` | `long` | 数据采集时间戳（Unix 时间戳） |
| `uptime_s` | `long` | 系统运行时间（秒） |
| `load_1min` | `double` | 1 分钟平均负载 |
| `load_5min` | `double` | 5 分钟平均负载 |
| `load_15min` | `double` | 15 分钟平均负载 |
| `running_tasks` | `int` | 正在运行的任务数 |
| `total_tasks` | `int` | 系统总任务数 |
| `cpu_user` | `unsigned long long` | 用户态 CPU 时间 |
| `cpu_system` | `unsigned long long` | 内核态 CPU 时间 |
| `cpu_nice` | `unsigned long long` | 低优先级用户态 CPU 时间 |
| `cpu_idle` | `unsigned long long` | 空闲 CPU 时间 |
| `cpu_iowait` | `unsigned long long` | I/O 等待 CPU 时间 |
| `cpu_irq` | `unsigned long long` | 硬件中断 CPU 时间 |
| `cpu_softirq` | `unsigned long long` | 软件中断 CPU 时间 |
| `cpu_steal` | `unsigned long long` | 虚拟化偷取 CPU 时间 |
| `mem_total_mib` | `double` | 总内存（MiB） |
| `mem_free_mib` | `double` | 空闲内存（MiB） |
| `mem_used_mib` | `double` | 已用内存（MiB） |
| `mem_buff_cache_mib` | `double` | 缓冲区/缓存内存（MiB） |
| `tcp_connections` | `int` | TCP 连接数 |
| `udp_connections` | `int` | UDP 连接数 |
| `net_rx_bytes` | `unsigned long` | 所有默认网口接收字节数 |
| `net_tx_bytes` | `unsigned long` | 所有默认网口发送字节数 |
| `cpu_num_cores` | `int` | CPU 核心数 |
| `root_disk_total_kb` | `unsigned long long` | 根分区总容量（KB） |
| `root_disk_avail_kb` | `unsigned long long` | 根分区可用容量（KB） |
| `disk_reads_completed` | `unsigned long long` | 磁盘读操作完成次数 |
| `disk_writes_completed` | `unsigned long long` | 磁盘写操作完成次数 |
| `disk_reading_ms` | `unsigned long long` | 磁盘读操作耗时（ms） |
| `disk_writing_ms` | `unsigned long long` | 磁盘写操作耗时（ms） |
| `disk_iotime_ms` | `unsigned long long` | 磁盘 I/O 总耗时（ms） |
| `disk_ios_in_progress` | `unsigned long long` | 正在进行的 I/O 操作数 |
| `disk_weighted_io_time` | `unsigned long long` | 加权 I/O 时间（ms） |
| `machine_id` | `string` | 机器唯一标识 |
| `hostname` | `string` | 主机名 |

### 数据格式示例

```plaintext
values=1712345678,123456,0.50,0.75,1.00,2,150,1000000,500000,10000,8000000,50000,1000,500,200,8192.00,1024.00,7168.00,2048.00,10,5,1234567890,987654321,8,104857600,52428800,10000,5000,1000,500,1500,2,1600,abc123def456,myserver
```

### 服务配置

systemd 服务文件位于 `/etc/systemd/system/kunlun.service`：

```ini
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart=/home/your-user/bin/kunlun -u https://example.com/api/report
Restart=always
User=your-user
Environment=HOME=/home/your-user

[Install]
WantedBy=multi-user.target
```

修改配置后需重载并重启：

```bash
sudo systemctl daemon-reload
sudo systemctl restart kunlun
```

---

## 从源码构建

```bash
git clone https://github.com/hochenggang/kunlun.git
cd kunlun
gcc -O2 -Wall -static -o kunlun kunlun-client.c
```

---

## 常见问题

### 上报地址验证失败

确保服务端 GET 请求返回内容包含 `kunlun` 字符串。

### 服务启动失败

检查服务文件中的路径是否正确，确认二进制文件存在且可执行：

```bash
ls -la ~/bin/kunlun
```

### 修改上报地址

编辑服务文件后重启：

```bash
sudo nano /etc/systemd/system/kunlun.service
sudo systemctl daemon-reload
sudo systemctl restart kunlun
```

---

## 许可证

本项目基于 [MIT 许可证](https://opensource.org/licenses/MIT) 开源。

---

## 贡献

欢迎提交 Issue 或 Pull Request！
