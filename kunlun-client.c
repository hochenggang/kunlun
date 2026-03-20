/**
 * @file kunlun-client.c
 * @brief Kunlun 系统监控客户端
 *
 * 轻量级 Linux 系统监控工具，周期性采集服务器性能指标并通过 HTTP POST 上报。
 * 支持采集：系统运行时间、负载、CPU、内存、磁盘、网络等核心指标。
 *
 * 编译命令：gcc -O2 -Wall -static -o kunlun kunlun-client.c
 * 运行方式：./kunlun -u https://example.com/api/report
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

/* ============================================================================
 * 数据结构定义
 * ============================================================================ */

/**
 * @brief 系统运行时间信息
 */
typedef struct
{
    double uptime_s;    /**< 系统运行时间（秒） */
    double idle_s;      /**< 系统空闲时间（秒） */
} Uptime;

/**
 * @brief 系统负载和任务信息
 */
typedef struct
{
    double load_1min;       /**< 1分钟平均负载 */
    double load_5min;       /**< 5分钟平均负载 */
    double load_15min;      /**< 15分钟平均负载 */
    int running_tasks;      /**< 正在运行的任务数 */
    int total_tasks;        /**< 系统总任务数 */
} LoadAvg;

/**
 * @brief CPU 时间统计信息（单位：USER_HZ，通常为 1/100 秒）
 */
typedef struct
{
    unsigned long long cpu_user;        /**< 用户态 CPU 时间 */
    unsigned long long cpu_system;      /**< 内核态 CPU 时间 */
    unsigned long long cpu_nice;        /**< 低优先级用户态 CPU 时间 */
    unsigned long long cpu_idle;        /**< 空闲 CPU 时间 */
    unsigned long long cpu_iowait;      /**< I/O 等待 CPU 时间 */
    unsigned long long cpu_irq;         /**< 硬件中断 CPU 时间 */
    unsigned long long cpu_softirq;     /**< 软件中断 CPU 时间 */
    unsigned long long cpu_steal;       /**< 虚拟化偷取 CPU 时间 */
} CpuInfo;

/**
 * @brief 内存信息（单位：MiB）
 */
typedef struct
{
    double mem_total_mib;       /**< 总内存 */
    double mem_free_mib;        /**< 空闲内存 */
    double mem_used_mib;        /**< 已用内存 */
    double mem_buff_cache_mib;  /**< 缓冲区/缓存内存 */
} MemInfo;

/**
 * @brief 网络连接和流量信息
 */
typedef struct
{
    int tcp_connections;                        /**< TCP 连接数 */
    int udp_connections;                        /**< UDP 连接数 */
    unsigned long default_interface_net_tx_bytes;   /**< 默认网口发送字节数 */
    unsigned long default_interface_net_rx_bytes;   /**< 默认网口接收字节数 */
} NetInfo;

/**
 * @brief 磁盘 I/O 统计信息
 */
typedef struct
{
    unsigned long long reads_completed;     /**< 成功完成的读操作总数 */
    unsigned long long read_merges;         /**< 读合并次数 */
    unsigned long long read_sectors;        /**< 读取的扇区总数 */
    unsigned long long reading_ms;          /**< 读操作总耗时（毫秒） */
    unsigned long long writes_completed;    /**< 成功完成的写操作总数 */
    unsigned long long write_merges;        /**< 写合并次数 */
    unsigned long long write_sectors;       /**< 写入的扇区总数 */
    unsigned long long writing_ms;          /**< 写操作总耗时（毫秒） */
    unsigned long long ios_in_progress;     /**< 当前正在进行的 I/O 操作数 */
    unsigned long long iotime_ms;           /**< 所有 I/O 操作总耗时（毫秒） */
    unsigned long long weighted_io_time;    /**< 加权 I/O 时间（毫秒） */
} DiskStats;

/**
 * @brief 系统基本信息
 */
typedef struct
{
    int cpu_num_cores;                  /**< CPU 核心数 */
    unsigned long long root_disk_total_kb;  /**< 根分区总容量（KB） */
    unsigned long long root_disk_avail_kb;  /**< 根分区可用容量（KB） */
    char machine_id[33];                /**< 机器唯一标识（32字符 + '\0'） */
    char hostname[256];                 /**< 主机名 */
} SystemInfo;

/* ============================================================================
 * 磁盘统计函数
 * ============================================================================ */

/**
 * @brief 获取根目录挂载分区的磁盘 I/O 统计信息
 *
 * 通过解析 /proc/mounts 找到根目录对应的设备，再从 /proc/diskstats 读取该设备的统计信息。
 *
 * @param stats 输出参数，存储磁盘统计信息
 * @return 成功返回 0，失败返回 -1
 */
int get_root_diskstats(DiskStats *stats) {
    if (!stats) return -1;

    memset(stats, 0, sizeof(DiskStats));

    /* 从 /proc/mounts 获取根目录挂载的设备名 */
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

    /* 移除 /dev/ 前缀，/proc/diskstats 中不包含此前缀 */
    if (strncmp(root_device, "/dev/", 5) == 0) {
        memmove(root_device, root_device + 5, strlen(root_device) - 4);
    }

    /* 从 /proc/diskstats 读取设备统计信息 */
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

/* ============================================================================
 * 工具函数
 * ============================================================================ */

/**
 * @brief 安全打开文件，失败时打印错误信息
 *
 * @param filename 文件名
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

/* ============================================================================
 * 系统信息读取函数
 * ============================================================================ */

/**
 * @brief 从 /proc/uptime 读取系统运行时间
 *
 * @param uptime 输出参数，存储运行时间信息
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
 * @brief 从 /proc/loadavg 读取系统负载和任务信息
 *
 * @param loadavg 输出参数，存储负载信息
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
 * @brief 从 /proc/stat 读取 CPU 时间统计
 *
 * @param cpuinfo 输出参数，存储 CPU 信息
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
 * @param meminfo 输出参数，存储内存信息（单位：MiB）
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
 * @brief 从 /proc/net/tcp 和 /proc/net/udp 读取连接数
 *
 * @param netinfo 输出参数，存储网络连接信息
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
            /* 跳过标题行 */
            if (fgets(line, sizeof(line), fp) == NULL)
            {
                fprintf(stderr, "Error reading header line from %s\n", files[i]);
                fclose(fp);
                continue;
            }

            /* 统计包含 ':' 的行数（即连接条目） */
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
 * @brief 获取机器唯一标识
 *
 * 依次尝试从 /etc/machine-id 和 /var/lib/dbus/machine-id 读取。
 *
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（buffer 中存储 "unknown"）
 */
int get_machine_id(char *buffer, size_t buffer_size)
{
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
    strncpy(buffer, "unknown", buffer_size);
    buffer[buffer_size - 1] = '\0';
    return -1;
}

/**
 * @brief 获取主机名
 *
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0，失败返回 -1（buffer 中存储 "unknown"）
 */
int get_hostname(char *buffer, size_t buffer_size)
{
    if (gethostname(buffer, buffer_size) != 0)
    {
        strncpy(buffer, "unknown", buffer_size);
        buffer[buffer_size - 1] = '\0';
        return -1;
    }
    return 0;
}

/* ============================================================================
 * 网络流量采集函数
 * ============================================================================ */

/**
 * @brief 去除字符串前导空白字符
 *
 * @param str 待处理的字符串（原地修改）
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
 * @brief 检查网口名是否已在列表中
 *
 * @param list 网口名列表
 * @param count 列表中元素数量
 * @param name 待检查的网口名
 * @return 存在返回 1，不存在返回 0
 */
static int interface_list_contains(char **list, int count, const char *name)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(list[i], name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 释放网口列表内存
 *
 * @param list 网口名列表
 * @param count 列表中元素数量
 */
static void free_interface_list(char **list, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(list[i]);
    }
}

/**
 * @brief 从 IPv4 路由表收集默认路由网口
 *
 * 解析 /proc/net/route，查找目标地址为 0.0.0.0 且标志包含 RTF_UP|RTF_GATEWAY 的路由。
 *
 * @param list 网口名列表（输出）
 * @param max_count 列表最大容量
 * @param count 当前列表元素数量（输入/输出）
 * @return 新增的网口数量
 */
static int collect_default_interfaces_ipv4(char **list, int max_count, int *count)
{
    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp)
    {
        return 0;
    }

    char line[256];
    int added = 0;

    /* 跳过标题行 */
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp) && *count < max_count)
    {
        char name[32];
        unsigned long dest, flags;

        if (sscanf(line, "%31s %lx %*x %lx", name, &dest, &flags) == 3)
        {
            /* 默认路由：dest == 0 且 flags 包含 RTF_UP(0x1) | RTF_GATEWAY(0x2) */
            if (dest == 0 && (flags & 0x3) == 0x3)
            {
                /* 去重：避免重复添加同一网口 */
                if (!interface_list_contains(list, *count, name))
                {
                    list[*count] = strdup(name);
                    if (list[*count])
                    {
                        (*count)++;
                        added++;
                    }
                }
            }
        }
    }

    fclose(fp);
    return added;
}

/**
 * @brief 从 IPv6 路由表收集默认路由网口
 *
 * 解析 /proc/net/ipv6_route，查找目标地址全为 0 且前缀长度为 0 的默认路由。
 *
 * @param list 网口名列表（输出）
 * @param max_count 列表最大容量
 * @param count 当前列表元素数量（输入/输出）
 * @return 新增的网口数量
 */
static int collect_default_interfaces_ipv6(char **list, int max_count, int *count)
{
    FILE *fp = fopen("/proc/net/ipv6_route", "r");
    if (!fp)
    {
        return 0;
    }

    char line[512];
    int added = 0;

    while (fgets(line, sizeof(line), fp) && *count < max_count)
    {
        char dest[33];
        char gateway[33];
        int prefix_len, metric, refcnt, use;
        unsigned long flags;
        char name[32];

        /* /proc/net/ipv6_route 格式：
         * dest(32字符) prefix_len gateway(32字符) metric refcnt use flags iface
         */
        int fields = sscanf(line, "%32s %02x %32s %x %x %x %lx %31s",
                            dest, &prefix_len, gateway, &metric, &refcnt, &use, &flags, name);

        if (fields >= 8)
        {
            /* 检查是否为默认路由：目标地址全为 0 且前缀长度为 0 */
            int is_default = 1;
            for (int i = 0; i < 32; i++)
            {
                if (dest[i] != '0')
                {
                    is_default = 0;
                    break;
                }
            }

            /* 默认路由：dest 全 0，prefix_len == 0，flags 包含 RTF_UP|RTF_GATEWAY */
            if (is_default && prefix_len == 0 && (flags & 0x3) == 0x3)
            {
                /* 去重：避免重复添加同一网口 */
                if (!interface_list_contains(list, *count, name))
                {
                    list[*count] = strdup(name);
                    if (list[*count])
                    {
                        (*count)++;
                        added++;
                    }
                }
            }
        }
    }

    fclose(fp);
    return added;
}

/**
 * @brief 获取默认路由网口的流量统计
 *
 * 同时支持 IPv4 和 IPv6 路由表，累加所有默认路由网口的流量。
 * 适用于单栈 IPv4、单栈 IPv6、双栈等场景。
 *
 * @param rx_bytes 输出参数，接收字节数（累加）
 * @param tx_bytes 输出参数，发送字节数（累加）
 * @return 成功返回 0，失败返回 -1
 */
int get_default_interface_traffic(unsigned long *rx_bytes, unsigned long *tx_bytes)
{
    *rx_bytes = 0;
    *tx_bytes = 0;

    char *ifaces[16];
    int iface_count = 0;

    /* 收集所有默认路由网口（IPv4 + IPv6） */
    collect_default_interfaces_ipv4(ifaces, 16, &iface_count);
    collect_default_interfaces_ipv6(ifaces, 16, &iface_count);

    if (iface_count == 0)
    {
        fprintf(stderr, "Failed to find any default interface (tried IPv4 and IPv6 routes)\n");
        return -1;
    }

    /* 从 /proc/net/dev 读取流量数据 */
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp)
    {
        perror("Failed to open /proc/net/dev");
        free_interface_list(ifaces, iface_count);
        return -1;
    }

    char line[256];
    /* 跳过前两行标题 */
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp))
    {
        char name[32];
        unsigned long rx, tx;

        /* /proc/net/dev 格式：接口名: rx_bytes rx_packets ... tx_bytes ... */
        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   name, &rx, &tx) == 3)
        {
            trim_leading_whitespace(name);

            /* 如果是默认路由网口，累加流量 */
            if (interface_list_contains(ifaces, iface_count, name))
            {
                *rx_bytes += rx;
                *tx_bytes += tx;
            }
        }
    }

    fclose(fp);
    free_interface_list(ifaces, iface_count);
    return 0;
}

/* ============================================================================
 * 磁盘空间函数
 * ============================================================================ */

/**
 * @brief 获取指定路径的磁盘空间信息
 *
 * @param path 路径（为空或 NULL 时默认为 "/"）
 * @param total_size_kb 输出参数，总容量（KB）
 * @param available_size_kb 输出参数，可用容量（KB）
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

/* ============================================================================
 * HTTP 上报函数
 * ============================================================================ */

/**
 * @brief 使用 curl 发送 POST 请求
 *
 * @param url 目标 URL
 * @param data POST 数据（已格式化为 key=value 形式）
 * @return 成功返回 0，失败返回 -1
 */
int send_post_request(const char *url, const char *data)
{
    char command[4096];
    int ret = snprintf(command, sizeof(command), "curl -s -X POST -d '%s' '%s'", data, url);
    if (ret < 0 || ret >= sizeof(command))
    {
        fprintf(stderr, "Error: Command too long or encoding error\n");
        return -1;
    }
    return system(command);
}

/* ============================================================================
 * 指标采集与格式化
 * ============================================================================ */

/**
 * @brief 采集所有系统指标
 *
 * @param uptime 输出参数，运行时间
 * @param loadavg 输出参数，负载信息
 * @param cpuinfo 输出参数，CPU 信息
 * @param meminfo 输出参数，内存信息
 * @param netinfo 输出参数，网络信息
 * @param sysinfo 输出参数，系统信息
 * @param diskstats 输出参数，磁盘统计
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
 * 输出字段顺序（共 35 个字段）：
 * timestamp, uptime_s, load_1min, load_5min, load_15min, running_tasks, total_tasks,
 * cpu_user, cpu_system, cpu_nice, cpu_idle, cpu_iowait, cpu_irq, cpu_softirq, cpu_steal,
 * mem_total_mib, mem_free_mib, mem_used_mib, mem_buff_cache_mib,
 * tcp_connections, udp_connections, net_rx_bytes, net_tx_bytes, cpu_num_cores,
 * root_disk_total_kb, root_disk_avail_kb,
 * disk_reads_completed, disk_writes_completed, disk_reading_ms, disk_writing_ms,
 * disk_iotime_ms, disk_ios_in_progress, disk_weighted_io_time,
 * machine_id, hostname
 *
 * @param timestamp 时间戳
 * @param uptime 运行时间
 * @param loadavg 负载信息
 * @param cpuinfo CPU 信息
 * @param meminfo 内存信息
 * @param netinfo 网络信息
 * @param sysinfo 系统信息
 * @param diskstats 磁盘统计
 * @return 成功返回格式化字符串（需调用者 free），失败返回 NULL
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
                           "%ld,%ld,%.2lf,%.2lf,%.2lf,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2lf,%.2lf,%.2lf,%.2lf,%d,%d,%lu,%lu,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s,%s",
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
                           diskstats->iotime_ms, diskstats->ios_in_progress, diskstats->weighted_io_time,
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

/* ============================================================================
 * 主函数
 * ============================================================================ */

/**
 * @brief 程序入口
 *
 * 用法：./kunlun -u <url>
 *
 * 每 10 秒采集一次系统指标，并通过 HTTP POST 上报到指定 URL。
 *
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 正常退出返回 0（实际上程序无限循环，不会退出）
 */
int main(int argc, char *argv[])
{
    char url[256] = "";
    int opt;

    /* 解析命令行参数 */
    while ((opt = getopt(argc, argv, "u:")) != -1)
    {
        switch (opt)
        {
        case 'u':
            strncpy(url, optarg, sizeof(url) - 1);
            url[sizeof(url) - 1] = '\0';
            break;
        default:
            fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* 检查必需参数 */
    if (strlen(url) == 0)
    {
        fprintf(stderr, "Error: -u <url> is required.\n");
        fprintf(stderr, "Usage: %s -u <url>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* 定义存储指标的变量 */
    Uptime uptime;
    LoadAvg loadavg;
    CpuInfo cpuinfo;
    MemInfo meminfo;
    NetInfo netinfo;
    SystemInfo sysinfo;
    DiskStats diskstats;

    /* 主循环：每 10 秒采集并上报一次 */
    while (1)
    {
        /* 计算到下一个整 10 秒的等待时间 */
        time_t now = time(NULL);
        int seconds_to_wait = 10 - (now % 10);
        sleep(seconds_to_wait);

        /* 采集指标 */
        time_t timestamp = time(NULL);
        collect_metrics(&uptime, &loadavg, &cpuinfo, &meminfo, &netinfo, &sysinfo, &diskstats);

        /* 格式化并上报 */
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
