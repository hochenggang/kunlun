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
#include <string.h>
#include <errno.h>

/**
 * @brief 系统运行时间结构体
 *
 * 存储从 /proc/uptime 读取的系统运行时间信息
 */
typedef struct
{
    double uptime_s;double idle_s;
} Uptime;

/**
 * @brief 系统负载结构体
 *
 * 存储从 /proc/loadavg 读取的系统负载和任务信息
 */
typedef struct
{
    double load_1min;double load_5min;double load_15min;
    int running_tasks;
    int total_tasks;
} LoadAvg;

/**
 * @brief CPU 信息结构体
 *
 * 存储从 /proc/stat 读取的 CPU 各状态时间（单位：jiffies）
 */
typedef struct
{
    unsigned long long cpu_user;unsigned long long cpu_system;unsigned long long cpu_nice;unsigned long long cpu_idle;unsigned long long cpu_iowait;unsigned long long cpu_irq;unsigned long long cpu_softirq;unsigned long long cpu_steal;
} CpuInfo;

/**
 * @brief 内存信息结构体
 *
 * 存储从 /proc/meminfo 读取的内存使用信息（单位：MiB）
 */
typedef struct
{
    double mem_total_mib;double mem_free_mib;double mem_used_mib;double mem_buff_cache_mib;
} MemInfo;

/**
 * @brief 网络信息结构体
 *
 * 存储网络连接数和默认网口流量信息
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
 *
 * 存储从 /proc/diskstats 读取的磁盘 I/O 统计信息
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
 *
 * 存储系统级别的静态信息
 */
typedef struct
{
    int cpu_num_cores;
    unsigned long long root_disk_total_kb;
    unsigned long long root_disk_avail_kb;
    char machine_id[33];
    char hostname[256];
} SystemInfo;

/**
 * @brief 获取根目录挂载分区的磁盘统计信息
 *
 * 通过解析 /proc/mounts 找到根目录挂载的设备，
 * 然后从 /proc/diskstats 读取该设备的 I/O 统计信息。
 *
 * @param stats 指向 DiskStats 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
int get_root_diskstats(DiskStats *stats) {
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
            strncpy(root_device, mount_entry->mnt_fsname, sizeof(root_device));
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
        memmove(root_device, root_device + 5, strlen(root_device) - 4);
    }

    FILE *diskstats_file = fopen("/proc/diskstats", "r");
    if (!diskstats_file) {
        perror("/proc/diskstats");
        return -1;
    }

    char line[1024];

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
            fclose(diskstats_file);
            return 0;
        }
    }

    fclose(diskstats_file);
    fprintf(stderr, "Could not find diskstats for root device: %s\n", root_device);
    return -1;
}

/**
 * @brief 安全的文件打开函数
 *
 * 封装 fopen，在打开失败时自动打印错误信息
 *
 * @param filename 要打开的文件名
 * @param mode 打开模式
 * @return 成功返回文件指针，失败返回 NULL
 */
static FILE *safe_fopen(const char *filename, const char *mode)
{
    FILE *fp = fopen(filename, mode);
    if (!fp)
    {
        perror(filename);
    }
    return fp;
}

/**
 * @brief 从 /proc/uptime 读取系统运行时间和空闲时间
 *
 * @param uptime 指向 Uptime 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
int read_uptime(Uptime *uptime)
{
    FILE *fp = safe_fopen("/proc/uptime", "r");
    if (!fp)
        return -1;

    if (fscanf(fp, "%lf %lf", &uptime->uptime_s, &uptime->idle_s) != 2)
    {
        fprintf(stderr, "Invalid /proc/uptime format\n");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/**
 * @brief 从 /proc/loadavg 读取负载信息和任务信息
 *
 * @param loadavg 指向 LoadAvg 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
int read_loadavg(LoadAvg *loadavg)
{
    FILE *fp = safe_fopen("/proc/loadavg", "r");
    if (!fp)
        return -1;

    if (fscanf(fp, "%lf %lf %lf %d/%d",
               &loadavg->load_1min, &loadavg->load_5min, &loadavg->load_15min,
               &loadavg->running_tasks, &loadavg->total_tasks) != 5)
    {
        fprintf(stderr, "Invalid /proc/loadavg format\n");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/**
 * @brief 从 /proc/stat 读取 CPU 信息
 *
 * 读取 CPU 在各状态下花费的时间（user/nice/system/idle/iowait/irq/softirq/steal）
 *
 * @param cpuinfo 指向 CpuInfo 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
int read_cpu_info(CpuInfo *cpuinfo)
{
    FILE *fp = safe_fopen("/proc/stat", "r");
    if (!fp)
        return -1;

    char cpu_label[4];
    if (fscanf(fp, "%3s %llu %llu %llu %llu %llu %llu %llu %llu",
               cpu_label, &cpuinfo->cpu_user, &cpuinfo->cpu_nice, &cpuinfo->cpu_system,
               &cpuinfo->cpu_idle, &cpuinfo->cpu_iowait, &cpuinfo->cpu_irq,
               &cpuinfo->cpu_softirq, &cpuinfo->cpu_steal) != 9 ||
        strcmp(cpu_label, "cpu") != 0)
    {
        fprintf(stderr, "Invalid /proc/stat format\n");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/**
 * @brief 从 /proc/meminfo 读取内存信息
 *
 * 读取 MemTotal、MemFree、Buffers、Cached 并计算已使用内存
 *
 * @param meminfo 指向 MemInfo 结构体的指针，用于存储结果
 * @return 成功返回 0，失败返回 -1
 */
int read_mem_info(MemInfo *meminfo)
{
    FILE *fp = safe_fopen("/proc/meminfo", "r");
    if (!fp)
        return -1;

    char key[32];
    unsigned long long value;
    meminfo->mem_total_mib = 0;
    meminfo->mem_free_mib = 0;
    meminfo->mem_buff_cache_mib = 0;

    while (fscanf(fp, "%31s %llu kB", key, &value) == 2)
    {
        if (strcmp(key, "MemTotal:") == 0)
        {
            meminfo->mem_total_mib = value / 1024.0;
        }
        else if (strcmp(key, "MemFree:") == 0)
        {
            meminfo->mem_free_mib = value / 1024.0;
        }
        else if (strcmp(key, "Buffers:") == 0 || strcmp(key, "Cached:") == 0)
        {
            meminfo->mem_buff_cache_mib += value / 1024.0;
        }
    }
    fclose(fp);

    meminfo->mem_used_mib = meminfo->mem_total_mib - meminfo->mem_free_mib;
    return 0;
}

/**
 * @brief 从 /proc/net/tcp 和 /proc/net/udp 读取 TCP/UDP 连接数
 *
 * 通过统计文件中包含冒号的行数来计算连接数
 *
 * @param netinfo 指向 NetInfo 结构体的指针，用于存储结果
 * @return 成功返回 0
 */
int read_net_info(NetInfo *netinfo)
{
    netinfo->tcp_connections = 0;
    netinfo->udp_connections = 0;

    const char *files[] = {"/proc/net/tcp", "/proc/net/udp", NULL};
    int *counts[] = {&netinfo->tcp_connections, &netinfo->udp_connections, NULL};

    for (int i = 0; files[i] != NULL; i++)
    {
        FILE *fp = safe_fopen(files[i], "r");
        if (fp)
        {
            char line[256];
            if (fgets(line, sizeof(line), fp) == NULL)
            {
                fprintf(stderr, "Error reading header line from %s\n", files[i]);
                fclose(fp);
                continue;
            }

            while (fgets(line, sizeof(line), fp))
            {
                if (strchr(line, ':') != NULL)
                {
                    (*counts[i])++;
                }
            }
            fclose(fp);
        }
        else
        {
            fprintf(stderr, "Failed to open %s\n", files[i]);
        }
    }
    return 0;
}

/**
 * @brief 获取 Linux 服务器的 machine-id
 *
 * 依次尝试从 /etc/machine-id 和 /var/lib/dbus/machine-id 读取
 *
 * @param buffer 用于存储结果的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（此时 buffer 包含 "unknown"）
 */
int get_machine_id(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return -1;
    }
    buffer[0] = '\0';
    const char *paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id", NULL};
    for (int i = 0; paths[i] != NULL; i++)
    {
        FILE *fp = safe_fopen(paths[i], "r");
        if (fp)
        {
            if (fgets(buffer, buffer_size, fp))
            {
                fclose(fp);
                buffer[strcspn(buffer, "\n")] = 0;
                return 0;
            }
            fclose(fp);
        }
    }
    snprintf(buffer, buffer_size, "unknown");
    return -1;
}

/**
 * @brief 获取主机名
 *
 * @param buffer 用于存储结果的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（此时 buffer 包含 "unknown"）
 */
int get_hostname(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return -1;
    }
    buffer[0] = '\0';
    if (gethostname(buffer, buffer_size) != 0)
    {
        snprintf(buffer, buffer_size, "unknown");
        return -1;
    }
    buffer[buffer_size - 1] = '\0';
    return 0;
}

/**
 * @brief 去除字符串的前导空白字符
 *
 * 原地修改字符串，移除开头的空格、制表符、换行符和回车符
 *
 * @param str 要处理的字符串
 */
void trim_leading_whitespace(char *str)
{
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
    {
        start++;
    }
    memmove(str, start, strlen(start) + 1);
}

/**
 * @brief 获取默认路由的网口名称
 *
 * 通过解析 /proc/net/route 找到默认路由对应的网络接口
 *
 * @return 成功返回网口名称字符串（需调用者 free），失败返回 NULL
 */
char *get_default_interface()
{
    FILE *fp;
    char line[256];
    char *iface = NULL;

    fp = fopen("/proc/net/route", "r");
    if (!fp)
    {
        perror("Failed to open /proc/net/route");
        return NULL;
    }

    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp))
    {
        char name[32];
        unsigned long dest, flags;

        if (sscanf(line, "%31s %lx %*x %lx", name, &dest, &flags) == 3)
        {
            if (dest == 0 && (flags & 0x3) == 0x3)
            {
                iface = strdup(name);
                if (!iface)
                {
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
 *
 * 通过解析 /proc/net/dev 获取默认网口的接收和发送字节数
 *
 * @param rx_bytes 用于存储接收字节数的指针
 * @param tx_bytes 用于存储发送字节数的指针
 * @return 成功返回 0，失败返回 -1
 */
int get_default_interface_traffic(unsigned long *rx_bytes, unsigned long *tx_bytes)
{
    FILE *fp;
    char line[256];
    char *iface = get_default_interface();

    *rx_bytes = 0;
    *tx_bytes = 0;

    if (!iface)
    {
        fprintf(stderr, "Failed to find default interface\n");
        return -1;
    }

    fp = fopen("/proc/net/dev", "r");
    if (!fp)
    {
        perror("Failed to open /proc/net/dev");
        free(iface);
        return -1;
    }

    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp))
    {
        char name[32];
        unsigned long rx, tx;

        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   name, &rx, &tx) == 3)
        {
            trim_leading_whitespace(name);

            if (strcmp(name, iface) == 0)
            {
                *rx_bytes = rx;
                *tx_bytes = tx;
                break;
            }
        }
    }

    fclose(fp);
    free(iface);
    return 0;
}

/**
 * @brief 获取磁盘空间信息
 *
 * 使用 statvfs 系统调用获取指定路径的磁盘空间信息
 *
 * @param path 要查询的路径（NULL 或空字符串默认为 "/"）
 * @param total_size_kb 用于存储总容量（KB）的指针，可为 NULL
 * @param available_size_kb 用于存储可用容量（KB）的指针，可为 NULL
 * @return 成功返回 0，失败返回 -1
 */
int get_disk_space_kb(const char *path, unsigned long long *total_size_kb, unsigned long long *available_size_kb)
{
    const char *effective_path = (path && *path) ? path : "/";
    struct statvfs vfs;

    if (statvfs(effective_path, &vfs) == -1)
    {
        perror("statvfs");
        return -1;
    }

    if (total_size_kb)
    {
        *total_size_kb = (vfs.f_blocks * vfs.f_frsize) / 1024;
    }
    if (available_size_kb)
    {
        *available_size_kb = (vfs.f_bavail * vfs.f_frsize) / 1024;
    }

    return 0;
}

/**
 * @brief 使用外部 curl 命令发送 POST 请求
 *
 * 构造并执行 curl 命令，将数据以 POST 方式发送到指定 URL
 *
 * @param url 目标 URL
 * @param data 要发送的数据（URL 编码格式）
 * @return 成功返回 curl 的退出码（0 表示成功），失败返回 -1
 */
int send_post_request(const char *url, const char *data)
{
    char command[4096];
    int ret = snprintf(command, sizeof(command), "curl -X POST -d '%s' '%s'", data, url);
    if (ret < 0 || ret >= sizeof(command))
    {
        fprintf(stderr, "Error: Command too long or encoding error\n");
        return -1;
    }
    return system(command);
}

/**
 * @brief 采集所有系统指标
 *
 * 调用各个采集函数，将结果存储到对应的结构体中
 *
 * @param uptime 用于存储运行时间信息
 * @param loadavg 用于存储负载信息
 * @param cpuinfo 用于存储 CPU 信息
 * @param meminfo 用于存储内存信息
 * @param netinfo 用于存储网络信息
 * @param sysinfo 用于存储系统信息
 * @param diskstats 用于存储磁盘统计信息
 */
void collect_metrics(Uptime *uptime, LoadAvg *loadavg, CpuInfo *cpuinfo, MemInfo *meminfo, NetInfo *netinfo, SystemInfo *sysinfo, DiskStats *diskstats)
{
    if (read_uptime(uptime) != 0)
    {
        fprintf(stderr, "Failed to read uptime\n");
    }
    if (read_loadavg(loadavg) != 0)
    {
        fprintf(stderr, "Failed to read loadavg\n");
    }
    if (read_cpu_info(cpuinfo) != 0)
    {
        fprintf(stderr, "Failed to read cpu info\n");
    }
    if (read_mem_info(meminfo) != 0)
    {
        fprintf(stderr, "Failed to read mem info\n");
    }
    if (read_net_info(netinfo) != 0)
    {
        fprintf(stderr, "Failed to read net info\n");
    }
    if (get_machine_id(sysinfo->machine_id, sizeof(sysinfo->machine_id)) != 0)
    {
        fprintf(stderr, "Failed to get machine id\n");
    }
    if (get_hostname(sysinfo->hostname, sizeof(sysinfo->hostname)) != 0)
    {
        fprintf(stderr, "Failed to get hostname\n");
    }
    if (get_root_diskstats(diskstats) != 0){
        fprintf(stderr, "Failed to get diskstats\n");
    }

    sysinfo->cpu_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (get_disk_space_kb("/", &sysinfo->root_disk_total_kb, &sysinfo->root_disk_avail_kb) != 0)
    {
        fprintf(stderr, "Failed to get disk space\n");
    }
    if (get_default_interface_traffic(&netinfo->default_interface_net_rx_bytes, &netinfo->default_interface_net_tx_bytes) != 0)
    {
        fprintf(stderr, "Failed to get net traffic\n");
    }
}

/**
 * @brief 将指标转换为 values=v1,v2,v3,... 格式的字符串
 *
 * 将所有采集的指标按固定顺序拼接成逗号分隔的字符串，
 * 并添加 "values=" 前缀
 *
 * 输出字段顺序：
 * timestamp, uptime_s, load_1min, load_5min, load_15min, running_tasks, total_tasks,
 * cpu_user, cpu_system, cpu_nice, cpu_idle, cpu_iowait, cpu_irq, cpu_softirq, cpu_steal,
 * mem_total_mib, mem_free_mib, mem_used_mib, mem_buff_cache_mib,
 * tcp_connections, udp_connections, net_rx_bytes, net_tx_bytes,
 * cpu_num_cores, root_disk_total_kb, root_disk_avail_kb,
 * disk_reads, disk_writes, disk_reading_ms, disk_writing_ms,
 * disk_iotime_ms, disk_ios_in_progress, disk_weighted_io_time,
 * machine_id, hostname
 *
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
char *metrics_to_kv(int timestamp, Uptime *uptime, LoadAvg *loadavg, CpuInfo *cpuinfo, MemInfo *meminfo, NetInfo *netinfo, SystemInfo *sysinfo, DiskStats *diskstats) {
    char *kv_string = malloc(8192);
    if (!kv_string) {
        perror("malloc");
        return NULL;
    }

    char values_buffer[8192];
    int values_len = 0;

    values_len += snprintf(values_buffer + values_len, sizeof(values_buffer) - values_len,
                           "%ld,%ld,%.2lf,%.2lf,%.2lf,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2lf,%.2lf,%.2lf,%.2lf,%d,%d,%lu,%lu,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s,%s",
                           timestamp,(long)uptime->uptime_s,
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
                           diskstats->reads_completed, diskstats->writes_completed, diskstats->reading_ms, diskstats->writing_ms,
                           diskstats->iotime_ms, diskstats->ios_in_progress,diskstats->weighted_io_time,
                           sysinfo->machine_id, sysinfo->hostname);

    if (values_len < 0 || values_len >= sizeof(values_buffer)) {
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
 *
 * 解析命令行参数，获取目标 URL，然后进入无限循环：
 * 每 10 秒采集一次指标并上报
 *
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 正常情况不返回，参数错误返回 EXIT_FAILURE
 */
int main(int argc, char *argv[])
{
    char url[256] = "";
    int opt;

    while ((opt = getopt(argc, argv, "u:")) != -1)
    {
        switch (opt)
        {
        case 'u':
            snprintf(url, sizeof(url), "%s", optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (strlen(url) == 0)
    {
        fprintf(stderr, "Error: -u <url> is required.\n");
        fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
        return EXIT_FAILURE;
    }

    Uptime uptime;
    LoadAvg loadavg;
    CpuInfo cpuinfo;
    MemInfo meminfo;
    NetInfo netinfo;
    SystemInfo sysinfo;
    DiskStats diskstats;

    while (1)
    {
        time_t now = time(NULL);
        int seconds_to_wait = 10 - (now % 10);
        sleep(seconds_to_wait);

        time_t timestamp = time(NULL);
        collect_metrics(&uptime, &loadavg, &cpuinfo, &meminfo, &netinfo, &sysinfo, &diskstats);
        char *kv_data = metrics_to_kv(timestamp, &uptime, &loadavg, &cpuinfo, &meminfo, &netinfo, &sysinfo, &diskstats);
        if (!kv_data)
        {
            fprintf(stderr, "Failed to convert metrics to key-value pairs\n");
            continue;
        }

        int ret = send_post_request(url, kv_data);
        if (ret != 0)
        {
            fprintf(stderr, "Failed to send data (curl returned %d)\n", ret);
        }
        free(kv_data);
    }
    return 0;
}
