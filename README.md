
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
curl -L https://github.com/hochenggang/kunlun/raw/refs/heads/main/kunlun-client-install.sh -o kunlun-client-install.sh
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

监测间隔是指 Kunlun 采集系统指标并上报的时间间隔（单位：秒），默认值为 10 秒。


### 2. 上报地址

上报地址是指 Kunlun 将采集到的指标数据发送到的 HTTP 地址。为了确保地址有效，Kunlun 会首先向该地址发送一个 GET 请求，并验证返回内容是否包含 `kunlun`。如果验证通过，Kunlun 会将采集到的指标数据通过 POST 请求以表单形式周期性的发送到该地址。

#### 数据字段

Kunlun 采集的指标数据包括以下字段：



| 参数名                      | 类型           | 说明                                                                                                                                                                                                                                              |
|---------------------------|----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `uptime_s`                  | `long`         | 系统运行时间（秒），从 `/proc/uptime` 读取并截断为 `long` 类型。                                                                                                                                                                                          |
| `load_1min`                 | `double`       | 1 分钟平均负载，从 `/proc/loadavg` 读取。                                                                                                                                                                                                          |
| `load_5min`                 | `double`       | 5 分钟平均负载，从 `/proc/loadavg` 读取。                                                                                                                                                                                                          |
| `load_15min`                | `double`       | 15 分钟平均负载，从 `/proc/loadavg` 读取。                                                                                                                                                                                                         |
| `net_tx_bytes`              | `unsigned long`| 默认路由接口的发送流量（字节），从 `/proc/net/dev` 读取。                                                                                                                                                                                          |
| `net_rx_bytes`              | `unsigned long`| 默认路由接口的接收流量（字节），从 `/proc/net/dev` 读取。                                                                                                                                                                                          |
| `disk_delay_us`             | `long`         | 磁盘延迟（微秒），通过模拟写入 `/dev/null` 测量。                                                                                                                                                                                                   |
| `cpu_delay_us`              | `long`         | CPU 延迟（微秒），通过计算一定迭代次数的圆周率来测量。                                                                                                                                                                                                  |
| `disks_total_kb`            | `unsigned long long` | 磁盘总容量（KB），从 `statvfs` 获取。                                                                                                                                                                                                       |
| `disks_avail_kb`            | `unsigned long long` | 磁盘可用容量（KB），从 `statvfs` 获取。                                                                                                                                                                                                       |
| `tcp_connections`           | `int`          | TCP 连接数，从 `/proc/net/tcp` 读取。                                                                                                                                                                                                           |
| `udp_connections`           | `int`          | UDP 连接数，从 `/proc/net/udp` 读取。                                                                                                                                                                                                           |
| `cpu_num_cores`             | `int`          | CPU 核心数，通过 `sysconf(_SC_NPROCESSORS_ONLN)` 获取。                                                                                                                                                                                          |
| `machine_id`                | `char[33]`     | Linux 服务器的 machine-id，从 `/etc/machine-id` 或 `/var/lib/dbus/machine-id` 读取。                                                                                                                                                                 |
| `hostname`                  | `char[256]`    | 主机名，通过 `gethostname()` 获取。                                                                                                                                                                                                              |
| `total_tasks`               | `int`          | 系统中总的任务数，从 `/proc/loadavg` 读取。                                                                                                                                                                                                     |
| `running_tasks`             | `int`          | 正在运行的任务数，从 `/proc/loadavg` 读取。                                                                                                                                                                                                      |
| `cpu_user`                  | `double`       | 用户空间占用 CPU 的时间（单位通常是 jiffies），从 `/proc/stat` 读取。                                                                                                                                                                                  |
| `cpu_system`                | `double`       | 内核空间占用 CPU 的时间，从 `/proc/stat` 读取。                                                                                                                                                                                                  |
| `cpu_nice`                  | `double`       | 用户进程空间内改变过优先级的进程占用 CPU 的时间，从 `/proc/stat` 读取。                                                                                                                                                                             |
| `cpu_idle`                  | `double`       | 空闲 CPU 的时间，从 `/proc/stat` 读取。                                                                                                                                                                                                        |
| `cpu_iowait`                | `double`       | 等待 I/O 的 CPU 时间，从 `/proc/stat` 读取。                                                                                                                                                                                                     |
| `cpu_irq`                   | `double`       | 硬件中断占用 CPU 的时间，从 `/proc/stat` 读取。                                                                                                                                                                                                  |
| `cpu_steal`                 | `double`       | 虚拟机偷取的 CPU 时间，从 `/proc/stat` 读取。                                                                                                                                                                                                     |
| `mem_total_mib`             | `double`       | 总内存大小 (MiB)，从 `/proc/meminfo` 读取并转换为 MiB。                                                                                                                                                                                          |
| `mem_free_mib`              | `double`       | 空闲内存大小 (MiB)，从 `/proc/meminfo` 读取并转换为 MiB。                                                                                                                                                                                          |
| `mem_used_mib`              | `double`       | 已用内存大小 (MiB)，计算得出（总内存 - 空闲内存）。                                                                                                                                                                                                |
| `mem_buff_cache_mib`        | `double`       | 缓存和缓冲区内存大小 (MiB)，从 `/proc/meminfo` 读取并转换为 MiB。                                                                                                                                                                                     |
| `timestamp`                 | `long`         | 数据采集的时间戳（Unix 时间戳），通过 `time(NULL)` 获取。                                                                                                                                                                                           |
| `root_diskstats_reads_completed` | `unsigned long long` | 根目录所在磁盘成功完成的读操作总数，从 `/proc/diskstats` 读取。                                                                                                                                                                            |
| `root_diskstats_read_sectors`   | `unsigned long long` | 根目录所在磁盘读取的扇区总数，从 `/proc/diskstats` 读取。                                                                                                                                                                                |
| `root_diskstats_reading_ms`     | `unsigned long long` | 根目录所在磁盘花费在读操作上的总毫秒数，从 `/proc/diskstats` 读取。                                                                                                                                                                         |
| `root_diskstats_writes_completed`| `unsigned long long` | 根目录所在磁盘成功完成的写操作总数，从 `/proc/diskstats` 读取。                                                                                                                                                                            |
| `root_diskstats_write_sectors`  | `unsigned long long` | 根目录所在磁盘写入的扇区总数，从 `/proc/diskstats` 读取。                                                                                                                                                                                |
| `root_diskstats_writing_ms`    | `unsigned long long` | 根目录所在磁盘花费在写操作上的总毫秒数，从 `/proc/diskstats` 读取。                                                                                                                                                                         |
| `root_diskstats_ios_in_progress`| `unsigned long long` | 根目录所在磁盘当前正在进行的 I/O 操作数，从 `/proc/diskstats` 读取。                                                                                                                                                                         |
| `root_diskstats_iotime_ms`      | `unsigned long long` | 根目录所在磁盘所有 I/O 操作花费的总毫秒数，从 `/proc/diskstats` 读取。                                                                                                                                                                       |
| `root_diskstats_weighted_io_time`| `unsigned long long` | 根目录所在磁盘的加权 I/O 时间（毫秒），从 `/proc/diskstats` 读取。                                                                                                                                                                            |


#### 示例 POST 请求

```plaintext
values=timestamp,uptime_s,load_1min,load_5min,load_15min,running_tasks,total_tasks,cpu_user,cpu_system,cpu_nice,cpu_idle,cpu_iowait,cpu_irq,cpu_softirq,cpu_steal,mem_total_mib,mem_free_mib,mem_used_mib,mem_buff_cache_mib,tcp_connections,udp_connections,default_interface_net_rx_bytes,default_interface_net_tx_bytes,cpu_num_cores,cpu_delay_us,disk_delay_us,root_disk_total_kb,root_disk_avail_kb,reads_completed,writes_completed,reading_ms,writing_ms,iotime_ms,ios_in_progress,weighted_io_time,machine_id,hostname

```

### 3. 服务配置

Kunlun 的 systemd 服务配置文件位于 `/etc/systemd/system/kunlun.service`，内容如下：

```ini
[Unit]
Description=Kunlun System Monitor
After=network.target

[Service]
ExecStart=/home/your-user/bin/kunlun_amd64 -u https://example.com
Restart=always
User=your-user
Environment=HOME=/home/your-user

[Install]
WantedBy=multi-user.target
```

- **ExecStart**：Kunlun 的启动命令，包含上报地址。
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
gcc -o kunlun_amd64 kunlun-client.c
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
