/**
 * @file kunlun-client.c
 * @brief 昆仑客户端 - Linux 系统监控数据采集器
 *
 * 本程序用于采集 Linux 系统的各项性能指标，并通过 HTTP POST 请求
 * 将数据上报到指定的服务器端点。采集间隔为 10 秒。
 *
 * 采集的指标包括：
 * - 系统运行时间和空闲时间
 * - 系统负载（1/5/15分钟）
 * - CPU 各状态时间（user/nice/system/idle/iowait/irq/softirq/steal）
 * - 内存使用情况（total/free/used/buff_cache）
 * - 网络连接数（TCP/UDP）
 * - 默认网口流量（RX/TX 字节数）
 * - CPU 核心数
 * - 根分区磁盘空间（总量/可用量）
 * - 根分区磁盘 I/O 统计
 * - 机器 ID 和主机名
 *
 * @author imhcg
 * @version 0.2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/statvfs.h>
#include <mntent.h>
#include <errno.h>

#define MAX_URL_LEN 512
#define MAX_HOSTNAME_LEN 256
#define MAX_MACHINE_ID_LEN 33
#define MAX_CMD_LEN 8192

/**
 * @brief 系统运行时间结构体
 */
typedef struct
{
    double uptime_s;
    double idle_s;
} Uptime;

/**
 * @brief 系统负载结构体
 */
typedef struct
{
    double load_1min;
    double load_5min;
    double load_15min;
    int running_tasks;
    int total_tasks;
} LoadAvg;

/**
 * @brief CPU 信息结构体
 */
typedef struct
{
    unsigned long long cpu_user;
    unsigned long long cpu_system;
    unsigned long long cpu_nice;
    unsigned long long cpu_idle;
    unsigned long long cpu_iowait;
    unsigned long long cpu_irq;
    unsigned long long cpu_softirq;
    unsigned long long cpu_steal;
} CpuInfo;

/**
 * @brief 内存信息结构体
 */
typedef struct
{
    double mem_total_mib;
    double mem_free_mib;
    double mem_used_mib;
    double mem_buff_cache_mib;
} MemInfo;

/**
 * @brief 网络信息结构体
 */
typedef struct
{
    int tcp_connections;
    int udp_connections;
    unsigned long default_interface_net_tx_bytes;
    unsigned long default_interface_net_rx_bytes;
} NetInfo;

/**
 * @brief 磁盘统计信息结构体
 */
typedef struct
{
    unsigned long long reads_completed;
    unsigned long long read_merges;
    unsigned long long read_sectors;
    unsigned long long reading_ms;
    unsigned long long writes_completed;
    unsigned long long write_merges;
    unsigned long long write_sectors;
    unsigned long long writing_ms;
    unsigned long long ios_in_progress;
    unsigned long long iotime_ms;
    unsigned long long weighted_io_time;
} DiskStats;

/**
 * @brief 系统信息结构体
 */
typedef struct
{
    int cpu_num_cores;
    unsigned long long root_disk_total_kb;
    unsigned long long root_disk_avail_kb;
    char machine_id[MAX_MACHINE_ID_LEN];
    char hostname[MAX_HOSTNAME_LEN];
} SystemInfo;

/**
 * @brief 获取根目录挂载分区的磁盘统计信息
 * @param stats 指向 DiskStats 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
static int get_root_diskstats(DiskStats *stats) {
    if (!stats) return -1;
    memset(stats, 0, sizeof(DiskStats));

    FILE *mounts_file = setmntent("/proc/mounts", "r");
    if (!mounts_file) {
        perror("setmntent");
        return -1;
    }

    char root_device[256] = "";
    struct mntent *mount_entry;

    while ((mount_entry = getmntent(mounts_file)) != NULL) {
        if (strcmp(mount_entry->mnt_dir, "/") == 0) {
            strncpy(root_device, mount_entry->mnt_fsname, sizeof(root_device) - 1);
            root_device[sizeof(root_device) - 1] = '\0';
            break;
        }
    }
    endmntent(mounts_file);

    if (strlen(root_device) == 0) {
        fprintf(stderr, "Could not find root device in /proc/mounts\n");
        return -1;
    }

    if (strncmp(root_device, "/dev/", 5) == 0) {
        size_t len = strlen(root_device);
        if (len > 5) {
            memmove(root_device, root_device + 5, len - 4);
            root_device[len - 5] = '\0';
        }
    }

    FILE *diskstats_file = fopen("/proc/diskstats", "r");
    if (!diskstats_file) {
        perror("/proc/diskstats");
        return -1;
    }

    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), diskstats_file)) {
        int major, minor;
        char name[256];

        int fields_read;
        fields_read = sscanf(line, "%d %d %255s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &major, &minor, name,
                   &stats->reads_completed, &stats->read_merges, &stats->read_sectors, &stats->reading_ms,
                   &stats->writes_completed, &stats->write_merges, &stats->write_sectors, &stats->writing_ms,
                   &stats->ios_in_progress, &stats->iotime_ms, &stats->weighted_io_time);
        if (fields_read >= 11 && strcmp(name, root_device) == 0) {
            found = 1;
            break;
        }
    }

    fclose(diskstats_file);
    if (!found) {
        fprintf(stderr, "Could not find diskstats for root device: %s\n", root_device);
        return -1;
    }
    return 0;
}

/**
 * @brief 安全的文件打开函数
 * @param filename 文件名
 * @param mode 打开模式
 * @return 成功返回文件指针，失败返回 NULL
 */
static FILE *safe_fopen(const char *filename, const char *mode)
{
    if (!filename || !mode) {
        fprintf(stderr, "Invalid parameters to safe_fopen\n");
        return NULL;
    }
    FILE *fp = fopen(filename, mode);
    if (!fp) {
        perror(filename);
    }
    return fp;
}

/**
 * @brief 从 /proc/uptime 读取系统运行时间和空闲时间
 * @param uptime 指向 Uptime 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
static int read_uptime(Uptime *uptime)
{
    if (!uptime) return -1;

    FILE *fp = safe_fopen("/proc/uptime", "r");
    if (!fp) return -1;

    int result = fscanf(fp, "%lf %lf", &uptime->uptime_s, &uptime->idle_s);
    fclose(fp);

    if (result != 2) {
        fprintf(stderr, "Invalid /proc/uptime format\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 从 /proc/loadavg 读取负载信息和任务信息
 * @param loadavg 指向 LoadAvg 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
static int read_loadavg(LoadAvg *loadavg)
{
    if (!loadavg) return -1;

    FILE *fp = safe_fopen("/proc/loadavg", "r");
    if (!fp) return -1;

    int result = fscanf(fp, "%lf %lf %lf %d/%d",
               &loadavg->load_1min, &loadavg->load_5min, &loadavg->load_15min,
               &loadavg->running_tasks, &loadavg->total_tasks);
    fclose(fp);

    if (result != 5) {
        fprintf(stderr, "Invalid /proc/loadavg format\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 从 /proc/stat 读取 CPU 信息
 * @param cpuinfo 指向 CpuInfo 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
static int read_cpu_info(CpuInfo *cpuinfo)
{
    if (!cpuinfo) return -1;

    FILE *fp = safe_fopen("/proc/stat", "r");
    if (!fp) return -1;

    char cpu_label[4];
    int result = fscanf(fp, "%3s %llu %llu %llu %llu %llu %llu %llu %llu",
               cpu_label, &cpuinfo->cpu_user, &cpuinfo->cpu_nice, &cpuinfo->cpu_system,
               &cpuinfo->cpu_idle, &cpuinfo->cpu_iowait, &cpuinfo->cpu_irq,
               &cpuinfo->cpu_softirq, &cpuinfo->cpu_steal);
    fclose(fp);

    if (result != 9 || strcmp(cpu_label, "cpu") != 0) {
        fprintf(stderr, "Invalid /proc/stat format\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 从 /proc/meminfo 读取内存信息
 * @param meminfo 指向 MemInfo 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
static int read_mem_info(MemInfo *meminfo)
{
    if (!meminfo) return -1;

    FILE *fp = safe_fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char key[32];
    unsigned long long value;
    meminfo->mem_total_mib = 0;
    meminfo->mem_free_mib = 0;
    meminfo->mem_buff_cache_mib = 0;

    while (fscanf(fp, "%31s %llu kB", key, &value) == 2) {
        if (strcmp(key, "MemTotal:") == 0) {
            meminfo->mem_total_mib = value / 1024.0;
        } else if (strcmp(key, "MemFree:") == 0) {
            meminfo->mem_free_mib = value / 1024.0;
        } else if (strcmp(key, "Buffers:") == 0 || strcmp(key, "Cached:") == 0) {
            meminfo->mem_buff_cache_mib += value / 1024.0;
        }
    }
    fclose(fp);

    meminfo->mem_used_mib = meminfo->mem_total_mib - meminfo->mem_free_mib;
    return 0;
}

/**
 * @brief 从 /proc/net/tcp 和 /proc/net/udp 读取 TCP/UDP 连接数
 * @param netinfo 指向 NetInfo 结构体的指针，用于存储结果
 * @return 成功返回 0
 */
static int read_net_info(NetInfo *netinfo)
{
    if (!netinfo) return -1;

    netinfo->tcp_connections = 0;
    netinfo->udp_connections = 0;

    const char *files[] = {"/proc/net/tcp", "/proc/net/udp", NULL};
    int *counts[] = {&netinfo->tcp_connections, &netinfo->udp_connections, NULL};

    for (int i = 0; files[i] != NULL; i++) {
        FILE *fp = safe_fopen(files[i], "r");
        if (!fp) {
            fprintf(stderr, "Failed to open %s\n", files[i]);
            continue;
        }

        char line[256];
        if (fgets(line, sizeof(line), fp) == NULL) {
            fprintf(stderr, "Error reading header line from %s\n", files[i]);
            fclose(fp);
            continue;
        }

        while (fgets(line, sizeof(line), fp)) {
            if (strchr(line, ':') != NULL) {
                (*counts[i])++;
            }
        }
        fclose(fp);
    }
    return 0;
}

/**
 * @brief 获取 Linux 服务器的 machine-id
 * @param buffer 用于存储结果的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（此时 buffer 包含 "unknown"）
 */
static int get_machine_id(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return -1;
    buffer[0] = '\0';

    const char *paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id", NULL};
    for (int i = 0; paths[i] != NULL; i++) {
        FILE *fp = safe_fopen(paths[i], "r");
        if (!fp) continue;

        if (fgets(buffer, buffer_size, fp)) {
            fclose(fp);
            size_t len = strcspn(buffer, "\n\r");
            if (len >= buffer_size) len = buffer_size - 1;
            buffer[len] = '\0';
            return 0;
        }
        fclose(fp);
    }

    snprintf(buffer, buffer_size, "unknown");
    return -1;
}

/**
 * @brief 获取主机名
 * @param buffer 用于存储结果的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（此时 buffer 包含 "unknown"）
 */
static int get_hostname(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return -1;
    buffer[0] = '\0';

    if (gethostname(buffer, buffer_size) != 0) {
        snprintf(buffer, buffer_size, "unknown");
        return -1;
    }

    buffer[buffer_size - 1] = '\0';
    return 0;
}

/**
 * @brief 去除字符串的前导空白字符
 * @param str 要处理的字符串
 */
static void trim_leading_whitespace(char *str)
{
    if (!str || !*str) return;

    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * @brief 获取默认路由的网口名称
 * @return 成功返回网口名称字符串（需调用者 free），失败返回 NULL
 */
static char *get_default_interface(void)
{
    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp) {
        perror("Failed to open /proc/net/route");
        return NULL;
    }

    char line[256];
    char *iface = NULL;

    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char name[32];
        unsigned long dest, flags;

        if (sscanf(line, "%31s %lx %*x %lx", name, &dest, &flags) == 3) {
            if (dest == 0 && (flags & 0x3) == 0x3) {
                iface = strdup(name);
                if (!iface) {
                    perror("strdup");
                }
                break;
            }
        }
    }

    fclose(fp);
    return iface;
}

/**
 * @brief 获取默认路由网口的流量数据
 * @param rx_bytes 用于存储接收字节数的指针
 * @param tx_bytes 用于存储发送字节数的指针
 * @return 成功返回 0，失败返回 -1
 */
static int get_default_interface_traffic(unsigned long *rx_bytes, unsigned long *tx_bytes)
{
    if (!rx_bytes || !tx_bytes) return -1;

    *rx_bytes = 0;
    *tx_bytes = 0;

    char *iface = get_default_interface();
    if (!iface) {
        fprintf(stderr, "Failed to find default interface\n");
        return -1;
    }

    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        perror("Failed to open /proc/net/dev");
        free(iface);
        return -1;
    }

    char line[256];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        free(iface);
        return -1;
    }

    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        free(iface);
        return -1;
    }

    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        char name[32];
        unsigned long rx, tx;

        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   name, &rx, &tx) == 3) {
            trim_leading_whitespace(name);
            if (strcmp(name, iface) == 0) {
                *rx_bytes = rx;
                *tx_bytes = tx;
                found = 1;
                break;
            }
        }
    }

    fclose(fp);
    free(iface);
    return found ? 0 : -1;
}

/**
 * @brief 获取磁盘空间信息
 * @param path 要查询的路径（NULL 或空字符串默认为 "/"）
 * @param total_size_kb 用于存储总容量（KB）的指针，可为 NULL
 * @param available_size_kb 用于存储可用容量（KB）的指针，可为 NULL
 * @return 成功返回 0，失败返回 -1
 */
static int get_disk_space_kb(const char *path, unsigned long long *total_size_kb, unsigned long long *available_size_kb)
{
    const char *effective_path = (path && *path) ? path : "/";
    struct statvfs vfs;

    if (statvfs(effective_path, &vfs) == -1) {
        perror("statvfs");
        return -1;
    }

    if (total_size_kb) {
        *total_size_kb = (vfs.f_blocks * vfs.f_frsize) / 1024;
    }
    if (available_size_kb) {
        *available_size_kb = (vfs.f_bavail * vfs.f_frsize) / 1024;
    }

    return 0;
}

/**
 * @brief 使用外部 curl 命令发送 POST 请求
 * @param url 目标 URL
 * @param data 要发送的数据（URL 编码格式）
 * @return 成功返回 curl 的退出码（0 表示成功），失败返回 -1
 */
static int send_post_request(const char *url, const char *data)
{
    if (!url || !data) {
        fprintf(stderr, "Invalid parameters to send_post_request\n");
        return -1;
    }

    char command[MAX_CMD_LEN];
    int ret = snprintf(command, sizeof(command), "curl -s -X POST -d \"%s\" \"%s\"", data, url);
    if (ret < 0 || ret >= (int)sizeof(command)) {
        fprintf(stderr, "Error: Command too long or encoding error\n");
        return -1;
    }

    return system(command);
}

/**
 * @brief 采集所有系统指标
 * @param uptime 用于存储运行时间信息
 * @param loadavg 用于存储负载信息
 * @param cpuinfo 用于存储 CPU 信息
 * @param meminfo 用于存储内存信息
 * @param netinfo 用于存储网络信息
 * @param sysinfo 用于存储系统信息
 * @param diskstats 用于存储磁盘统计信息
 * @return 成功返回 0，部分失败返回 -1
 */
static int collect_metrics(Uptime *uptime, LoadAvg *loadavg, CpuInfo *cpuinfo, 
                           MemInfo *meminfo, NetInfo *netinfo, 
                           SystemInfo *sysinfo, DiskStats *diskstats)
{
    int errors = 0;

    if (read_uptime(uptime) != 0) {
        fprintf(stderr, "Failed to read uptime\n");
        errors++;
    }
    if (read_loadavg(loadavg) != 0) {
        fprintf(stderr, "Failed to read loadavg\n");
        errors++;
    }
    if (read_cpu_info(cpuinfo) != 0) {
        fprintf(stderr, "Failed to read cpu info\n");
        errors++;
    }
    if (read_mem_info(meminfo) != 0) {
        fprintf(stderr, "Failed to read mem info\n");
        errors++;
    }
    if (read_net_info(netinfo) != 0) {
        fprintf(stderr, "Failed to read net info\n");
        errors++;
    }
    if (get_machine_id(sysinfo->machine_id, sizeof(sysinfo->machine_id)) != 0) {
        fprintf(stderr, "Failed to get machine id\n");
        errors++;
    }
    if (get_hostname(sysinfo->hostname, sizeof(sysinfo->hostname)) != 0) {
        fprintf(stderr, "Failed to get hostname\n");
        errors++;
    }
    if (get_root_diskstats(diskstats) != 0) {
        fprintf(stderr, "Failed to get diskstats\n");
        errors++;
    }

    sysinfo->cpu_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (sysinfo->cpu_num_cores <= 0) {
        fprintf(stderr, "Failed to get CPU core count\n");
        errors++;
    }

    if (get_disk_space_kb("/", &sysinfo->root_disk_total_kb, &sysinfo->root_disk_avail_kb) != 0) {
        fprintf(stderr, "Failed to get disk space\n");
        errors++;
    }

    if (get_default_interface_traffic(&netinfo->default_interface_net_rx_bytes, 
                                    &netinfo->default_interface_net_tx_bytes) != 0) {
        fprintf(stderr, "Failed to get net traffic\n");
        errors++;
    }

    return errors > 0 ? -1 : 0;
}

/**
 * @brief 将指标转换为 values=v1,v2,v3,... 格式的字符串
 * @param timestamp 时间戳
 * @param uptime 运行时间信息
 * @param loadavg 负载信息
 * @param cpuinfo CPU 信息
 * @param meminfo 内存信息
 * @param netinfo 网络信息
 * @param sysinfo 系统信息
 * @param diskstats 磁盘统计信息
 * @return 成功返回分配的字符串指针（需调用者 free），失败返回 NULL
 */
static char *metrics_to_kv(int timestamp, Uptime *uptime, LoadAvg *loadavg, 
                             CpuInfo *cpuinfo, MemInfo *meminfo, 
                             NetInfo *netinfo, SystemInfo *sysinfo, 
                             DiskStats *diskstats)
{
    char *kv_string = malloc(8192);
    if (!kv_string) {
        perror("malloc");
        return NULL;
    }

    char values_buffer[8192];
    int values_len = 0;

    values_len += snprintf(values_buffer + values_len, sizeof(values_buffer) - values_len,
                           "%ld,%ld,%.2lf,%.2lf,%.2lf,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2lf,%.2lf,%.2lf,%.2lf,%d,%d,%lu,%lu,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s,%s",
                           timestamp, (long)uptime->uptime_s,
                           loadavg->load_1min, loadavg->load_5min, loadavg->load_15min,
                           loadavg->running_tasks, loadavg->total_tasks,
                           cpuinfo->cpu_user, cpuinfo->cpu_system, cpuinfo->cpu_nice,
                           cpuinfo->cpu_idle, cpuinfo->cpu_iowait, cpuinfo->cpu_irq,
                           cpuinfo->cpu_softirq, cpuinfo->cpu_steal,
                           meminfo->mem_total_mib, meminfo->mem_free_mib, meminfo->mem_used_mib,
                           meminfo->mem_buff_cache_mib,
                           netinfo->tcp_connections, netinfo->udp_connections,
                           netinfo->default_interface_net_rx_bytes, netinfo->default_interface_net_tx_bytes,
                           sysinfo->cpu_num_cores,
                           sysinfo->root_disk_total_kb, sysinfo->root_disk_avail_kb,
                           diskstats->reads_completed, diskstats->writes_completed, 
                           diskstats->reading_ms, diskstats->writing_ms,
                           diskstats->iotime_ms, diskstats->ios_in_progress,
                           diskstats->weighted_io_time,
                           sysinfo->machine_id, sysinfo->hostname);

    if (values_len < 0 || values_len >= (int)sizeof(values_buffer)) {
        fprintf(stderr, "Error: Values string too long\n");
        free(kv_string);
        return NULL;
    }

    int kv_len = snprintf(kv_string, 8192, "values=%s", values_buffer);
    if (kv_len < 0 || kv_len >= 8192) {
        fprintf(stderr, "Error: Key-value string too long\n");
        free(kv_string);
        return NULL;
    }

    return kv_string;
}

/**
 * @brief 程序入口
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 正常情况不返回，参数错误返回 EXIT_FAILURE
 */
int main(int argc, char *argv[])
{
    char url[MAX_URL_LEN] = "";
    int opt;

    while ((opt = getopt(argc, argv, "u:")) != -1) {
        switch (opt) {
            case 'u':
                if (strlen(optarg) >= MAX_URL_LEN - 1) {
                    fprintf(stderr, "Error: URL too long (max %d characters)\n", MAX_URL_LEN - 1);
                    return EXIT_FAILURE;
                }
                snprintf(url, sizeof(url), "%s", optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (strlen(url) == 0) {
        fprintf(stderr, "Error: -u <url> is required.\n");
        fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
        return EXIT_FAILURE;
    }

    Uptime uptime = {0};
    LoadAvg loadavg = {0};
    CpuInfo cpuinfo = {0};
    MemInfo meminfo = {0};
    NetInfo netinfo = {0};
    SystemInfo sysinfo = {0};
    DiskStats diskstats = {0};

    while (1) {
        time_t now = time(NULL);
        int seconds_to_wait = 10 - (now % 10);
        if (seconds_to_wait < 0) seconds_to_wait += 10;
        sleep(seconds_to_wait);

        time_t timestamp = time(NULL);
        if (collect_metrics(&uptime, &loadavg, &cpuinfo, &meminfo, &netinfo, &sysinfo, &diskstats) != 0) {
            fprintf(stderr, "Warning: Some metrics collection failed, continuing...\n");
        }

        char *kv_data = metrics_to_kv(timestamp, &uptime, &loadavg, &cpuinfo, &meminfo, &netinfo, &sysinfo, &diskstats);
        if (!kv_data) {
            fprintf(stderr, "Failed to convert metrics to key-value pairs\n");
            sleep(5);
            continue;
        }

        int ret = send_post_request(url, kv_data);
        if (ret != 0) {
            fprintf(stderr, "Failed to send data (curl returned %d)\n", ret);
        }
        free(kv_data);
    }

    return 0;
}
