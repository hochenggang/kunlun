/**
 * Kunlun - Linux System Monitor Client
 * 
 * 轻量级系统监控工具，周期性采集服务器性能指标并通过HTTP上报
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>

/* ==================== 常量定义 ==================== */

#define REPORT_INTERVAL_SEC     10
#define URL_MAX_LEN             512
#define HOSTNAME_MAX_LEN        256
#define MACHINE_ID_MAX_LEN      33
#define BUFFER_SIZE             4096
#define VALUES_BUFFER_SIZE      2048

/* ==================== 数据结构 ==================== */

/* 系统运行时间信息 */
typedef struct {
    long uptime_s;          /* 系统运行时间（秒） */
} UptimeInfo;

/* 系统负载信息 */
typedef struct {
    double load_1min;       /* 1分钟平均负载 */
    double load_5min;       /* 5分钟平均负载 */
    double load_15min;      /* 15分钟平均负载 */
    int running_tasks;      /* 正在运行的任务数 */
    int total_tasks;        /* 系统总任务数 */
} LoadAvgInfo;

/* CPU统计信息 */
typedef struct {
    unsigned long long user;      /* 用户态CPU时间 */
    unsigned long long system;    /* 内核态CPU时间 */
    unsigned long long nice;      /* 低优先级用户态CPU时间 */
    unsigned long long idle;      /* 空闲CPU时间 */
    unsigned long long iowait;    /* I/O等待CPU时间 */
    unsigned long long irq;       /* 硬件中断CPU时间 */
    unsigned long long softirq;   /* 软件中断CPU时间 */
    unsigned long long steal;     /* 虚拟化偷取CPU时间 */
} CpuStats;

/* 内存信息 */
typedef struct {
    double total_mib;       /* 总内存（MiB） */
    double free_mib;        /* 空闲内存（MiB） */
    double used_mib;        /* 已用内存（MiB） */
    double buff_cache_mib;  /* 缓冲区/缓存内存（MiB） */
} MemStats;

/* 网络连接信息 */
typedef struct {
    int tcp_connections;    /* TCP连接数 */
    int udp_connections;    /* UDP连接数 */
} NetConnStats;

/* 网卡流量信息 */
typedef struct {
    unsigned long rx_bytes; /* 接收字节数 */
    unsigned long tx_bytes; /* 发送字节数 */
} NetTrafficStats;

/* 磁盘统计信息 */
typedef struct {
    unsigned long long reads_completed;     /* 读操作完成次数 */
    unsigned long long writes_completed;    /* 写操作完成次数 */
    unsigned long long reading_ms;          /* 读操作耗时（ms） */
    unsigned long long writing_ms;          /* 写操作耗时（ms） */
    unsigned long long iotime_ms;           /* I/O总耗时（ms） */
    unsigned long long ios_in_progress;     /* 正在进行的I/O操作数 */
    unsigned long long weighted_io_time;    /* 加权I/O时间（ms） */
} DiskStats;

/* 磁盘空间信息 */
typedef struct {
    unsigned long long total_kb;    /* 总容量（KB） */
    unsigned long long avail_kb;    /* 可用容量（KB） */
} DiskSpaceInfo;

/* 系统基本信息 */
typedef struct {
    int cpu_num_cores;                      /* CPU核心数 */
    char machine_id[MACHINE_ID_MAX_LEN];    /* 机器唯一标识 */
    char hostname[HOSTNAME_MAX_LEN];        /* 主机名 */
} SystemInfo;

/* 完整的监控指标 */
typedef struct {
    time_t timestamp;           /* 数据采集时间戳 */
    UptimeInfo uptime;
    LoadAvgInfo loadavg;
    CpuStats cpu;
    MemStats mem;
    NetConnStats net_conn;
    NetTrafficStats net_traffic;
    DiskStats disk;
    DiskSpaceInfo disk_space;
    SystemInfo sys;
} Metrics;

/* ==================== 工具函数 ==================== */

/**
 * 安全地打开文件，失败时打印错误信息
 */
static FILE *safe_fopen(const char *filename, const char *mode)
{
    FILE *fp = fopen(filename, mode);
    if (!fp) {
        fprintf(stderr, "Error opening %s: %s\n", filename, strerror(errno));
    }
    return fp;
}

/**
 * 去除字符串两端的空白字符
 */
static void trim_whitespace(char *str)
{
    if (!str || !*str) return;
    
    /* 去除前导空白 */
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    /* 去除尾部空白 */
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/**
 * 从文件读取第一行并去除换行符
 * 返回值: 成功返回0，失败返回-1
 */
static int read_first_line(const char *filename, char *buffer, size_t size)
{
    FILE *fp = safe_fopen(filename, "r");
    if (!fp) return -1;
    
    if (!fgets(buffer, size, fp)) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    
    /* 去除换行符 */
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    
    return 0;
}

/* ==================== 指标采集函数 ==================== */

/**
 * 读取系统运行时间
 */
static int collect_uptime(UptimeInfo *uptime)
{
    FILE *fp = safe_fopen("/proc/uptime", "r");
    if (!fp) return -1;
    
    double uptime_sec, idle_sec;
    if (fscanf(fp, "%lf %lf", &uptime_sec, &idle_sec) != 2) {
        fprintf(stderr, "Invalid /proc/uptime format\n");
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    uptime->uptime_s = (long)uptime_sec;
    return 0;
}

/**
 * 读取系统负载和任务信息
 */
static int collect_loadavg(LoadAvgInfo *loadavg)
{
    FILE *fp = safe_fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    
    int running, total;
    if (fscanf(fp, "%lf %lf %lf %d/%d",
               &loadavg->load_1min, &loadavg->load_5min, &loadavg->load_15min,
               &running, &total) != 5) {
        fprintf(stderr, "Invalid /proc/loadavg format\n");
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    loadavg->running_tasks = running;
    loadavg->total_tasks = total;
    return 0;
}

/**
 * 读取CPU统计信息
 */
static int collect_cpu_stats(CpuStats *cpu)
{
    FILE *fp = safe_fopen("/proc/stat", "r");
    if (!fp) return -1;
    
    char label[16];
    if (fscanf(fp, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
               label,
               &cpu->user, &cpu->nice, &cpu->system,
               &cpu->idle, &cpu->iowait, &cpu->irq,
               &cpu->softirq, &cpu->steal) != 9) {
        fprintf(stderr, "Invalid /proc/stat format\n");
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    
    if (strcmp(label, "cpu") != 0) {
        fprintf(stderr, "Unexpected label in /proc/stat: %s\n", label);
        return -1;
    }
    
    return 0;
}

/**
 * 读取内存信息
 */
static int collect_mem_stats(MemStats *mem)
{
    FILE *fp = safe_fopen("/proc/meminfo", "r");
    if (!fp) return -1;
    
    char line[256];
    unsigned long long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long long buffers = 0, cached = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long value;
        if (sscanf(line, "MemTotal: %llu", &value) == 1) {
            mem_total = value;
        } else if (sscanf(line, "MemFree: %llu", &value) == 1) {
            mem_free = value;
        } else if (sscanf(line, "MemAvailable: %llu", &value) == 1) {
            mem_available = value;
        } else if (sscanf(line, "Buffers: %llu", &value) == 1) {
            buffers = value;
        } else if (sscanf(line, "Cached: %llu", &value) == 1) {
            cached = value;
        }
    }
    
    fclose(fp);
    
    if (mem_total == 0) {
        fprintf(stderr, "Failed to read MemTotal from /proc/meminfo\n");
        return -1;
    }
    
    mem->total_mib = mem_total / 1024.0;
    mem->free_mib = mem_free / 1024.0;
    mem->buff_cache_mib = (buffers + cached) / 1024.0;
    
    /* 计算已用内存：优先使用MemAvailable，否则使用传统计算方式 */
    if (mem_available > 0) {
        mem->used_mib = (mem_total - mem_available) / 1024.0;
    } else {
        mem->used_mib = (mem_total - mem_free - buffers - cached) / 1024.0;
    }
    
    return 0;
}

/**
 * 读取网络连接数（TCP/UDP）
 */
static int collect_net_connections(NetConnStats *net)
{
    net->tcp_connections = 0;
    net->udp_connections = 0;
    
    /* 读取TCP连接数 */
    FILE *fp = safe_fopen("/proc/net/tcp", "r");
    if (fp) {
        char line[256];
        /* 跳过标题行 */
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                /* 每行代表一个连接 */
                net->tcp_connections++;
            }
        }
        fclose(fp);
    }
    
    /* 读取TCP6连接数 */
    fp = safe_fopen("/proc/net/tcp6", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                net->tcp_connections++;
            }
        }
        fclose(fp);
    }
    
    /* 读取UDP连接数 */
    fp = safe_fopen("/proc/net/udp", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                net->udp_connections++;
            }
        }
        fclose(fp);
    }
    
    /* 读取UDP6连接数 */
    fp = safe_fopen("/proc/net/udp6", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            while (fgets(line, sizeof(line), fp)) {
                net->udp_connections++;
            }
        }
        fclose(fp);
    }
    
    return 0;
}

/**
 * 获取默认网口名称
 * 返回值: 成功返回动态分配的字符串（需调用者释放），失败返回NULL
 */
static char *get_default_interface(void)
{
    FILE *fp = safe_fopen("/proc/net/route", "r");
    if (!fp) return NULL;
    
    char line[256];
    char iface[32] = {0};
    
    /* 跳过标题行 */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return NULL;
    }
    
    /* 查找默认路由（目的地址为0.0.0.0） */
    while (fgets(line, sizeof(line), fp)) {
        unsigned long dest, flags;
        char name[32];
        
        if (sscanf(line, "%31s %lx %*x %lx", name, &dest, &flags) == 3) {
            /* 检查是否为默认路由且网关标志位设置 */
            if (dest == 0 && (flags & 0x2)) {
                strncpy(iface, name, sizeof(iface) - 1);
                iface[sizeof(iface) - 1] = '\0';
                break;
            }
        }
    }
    
    fclose(fp);
    
    if (iface[0] == '\0') {
        return NULL;
    }
    
    return strdup(iface);
}

/**
 * 读取默认网口流量
 */
static int collect_net_traffic(NetTrafficStats *traffic)
{
    traffic->rx_bytes = 0;
    traffic->tx_bytes = 0;
    
    char *iface = get_default_interface();
    if (!iface) {
        fprintf(stderr, "Failed to determine default network interface\n");
        return -1;
    }
    
    FILE *fp = safe_fopen("/proc/net/dev", "r");
    if (!fp) {
        free(iface);
        return -1;
    }
    
    char line[512];
    /* 跳过前两行标题 */
    if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        free(iface);
        return -1;
    }
    
    /* 查找匹配的网口 */
    while (fgets(line, sizeof(line), fp)) {
        char name[32];
        unsigned long rx, tx;
        
        /* 格式: "  eth0: 123 456 ... 789 012" */
        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   name, &rx, &tx) == 3) {
            trim_whitespace(name);
            if (strcmp(name, iface) == 0) {
                traffic->rx_bytes = rx;
                traffic->tx_bytes = tx;
                break;
            }
        }
    }
    
    fclose(fp);
    free(iface);
    return 0;
}

/**
 * 读取根分区磁盘统计信息
 */
static int collect_disk_stats(DiskStats *stats)
{
    /* 首先确定根分区的设备名 */
    FILE *fp = safe_fopen("/proc/mounts", "r");
    if (!fp) return -1;
    
    char line[512];
    char root_device[256] = {0};
    
    while (fgets(line, sizeof(line), fp)) {
        char device[256], mountpoint[256];
        if (sscanf(line, "%255s %255s", device, mountpoint) == 2) {
            if (strcmp(mountpoint, "/") == 0) {
                strncpy(root_device, device, sizeof(root_device) - 1);
                root_device[sizeof(root_device) - 1] = '\0';
                break;
            }
        }
    }
    fclose(fp);
    
    if (root_device[0] == '\0') {
        fprintf(stderr, "Could not find root device\n");
        return -1;
    }
    
    /* 从设备路径中提取设备名（去掉/dev/前缀） */
    char *device_name = root_device;
    if (strncmp(root_device, "/dev/", 5) == 0) {
        device_name = root_device + 5;
    }
    
    /* 处理设备名中的数字（如sda1 -> sda） */
    char base_device[256];
    strncpy(base_device, device_name, sizeof(base_device) - 1);
    base_device[sizeof(base_device) - 1] = '\0';
    
    /* 如果是分区设备（如nvme0n1p1, sda1），去掉末尾数字 */
    size_t len = strlen(base_device);
    while (len > 0 && isdigit((unsigned char)base_device[len - 1])) {
        base_device[--len] = '\0';
    }
    /* 对于nvme设备，去掉末尾的p */
    if (len > 0 && base_device[len - 1] == 'p' && 
        strncmp(base_device, "nvme", 4) == 0) {
        base_device[--len] = '\0';
    }
    
    /* 读取/proc/diskstats */
    fp = safe_fopen("/proc/diskstats", "r");
    if (!fp) return -1;
    
    memset(stats, 0, sizeof(DiskStats));
    int found = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        int major, minor;
        char name[256];
        unsigned long long v[14] = {0};
        
        /* 解析diskstats行，兼容不同内核版本 */
        int n = sscanf(line, "%d %d %255s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &major, &minor, name,
                       &v[0], &v[1], &v[2], &v[3],   /* reads */
                       &v[4], &v[5], &v[6], &v[7],   /* writes */
                       &v[8], &v[9], &v[10]);        /* ios, iotime, weighted */
        
        if (n >= 4 && strcmp(name, base_device) == 0) {
            stats->reads_completed = v[0];
            stats->writes_completed = v[4];
            stats->reading_ms = v[3];
            stats->writing_ms = v[7];
            stats->ios_in_progress = (n > 8) ? v[8] : 0;
            stats->iotime_ms = (n > 9) ? v[9] : 0;
            stats->weighted_io_time = (n > 10) ? v[10] : 0;
            found = 1;
            break;
        }
    }
    
    fclose(fp);
    
    if (!found) {
        fprintf(stderr, "Could not find diskstats for device: %s\n", base_device);
        return -1;
    }
    
    return 0;
}

/**
 * 读取根分区磁盘空间
 */
static int collect_disk_space(DiskSpaceInfo *space)
{
    struct statvfs vfs;
    
    if (statvfs("/", &vfs) != 0) {
        fprintf(stderr, "statvfs failed: %s\n", strerror(errno));
        return -1;
    }
    
    space->total_kb = (vfs.f_blocks * vfs.f_frsize) / 1024;
    space->avail_kb = (vfs.f_bavail * vfs.f_frsize) / 1024;
    
    return 0;
}

/**
 * 获取机器ID
 */
static int collect_machine_id(char *buffer, size_t size)
{
    const char *paths[] = {
        "/etc/machine-id",
        "/var/lib/dbus/machine-id",
        NULL
    };
    
    for (int i = 0; paths[i]; i++) {
        if (read_first_line(paths[i], buffer, size) == 0 && buffer[0]) {
            return 0;
        }
    }
    
    /* 备用：生成一个基于主机名的标识 */
    strncpy(buffer, "unknown", size - 1);
    buffer[size - 1] = '\0';
    return -1;
}

/**
 * 获取主机名
 */
static int collect_hostname(char *buffer, size_t size)
{
    if (gethostname(buffer, size) != 0) {
        strncpy(buffer, "unknown", size - 1);
        buffer[size - 1] = '\0';
        return -1;
    }
    
    /* 确保字符串终止 */
    buffer[size - 1] = '\0';
    return 0;
}

/**
 * 获取CPU核心数
 */
static int collect_cpu_cores(void)
{
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return (nprocs > 0) ? (int)nprocs : 1;
}

/* ==================== 数据上报 ==================== */

/**
 * 将指标格式化为values=v1,v2,v3,...格式
 * 
 * 字段顺序必须与README中定义的数据字段表格一致：
 * timestamp, uptime_s, load_1min, load_5min, load_15min, running_tasks, total_tasks,
 * cpu_user, cpu_system, cpu_nice, cpu_idle, cpu_iowait, cpu_irq, cpu_softirq, cpu_steal,
 * mem_total_mib, mem_free_mib, mem_used_mib, mem_buff_cache_mib,
 * tcp_connections, udp_connections, net_rx_bytes, net_tx_bytes,
 * cpu_num_cores, root_disk_total_kb, root_disk_avail_kb,
 * disk_reads_completed, disk_writes_completed, disk_reading_ms, disk_writing_ms,
 * disk_iotime_ms, disk_ios_in_progress, disk_weighted_io_time,
 * machine_id, hostname
 */
static char *format_metrics(const Metrics *m)
{
    char *result = malloc(BUFFER_SIZE);
    if (!result) {
        perror("malloc");
        return NULL;
    }
    
    char values[VALUES_BUFFER_SIZE];
    int n = snprintf(values, sizeof(values),
        "%ld,%ld,"                    /* timestamp, uptime_s */
        "%.2f,%.2f,%.2f,%d,%d,"       /* load_1min, load_5min, load_15min, running_tasks, total_tasks */
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"  /* cpu_user, system, nice, idle, iowait, irq, softirq, steal */
        "%.2f,%.2f,%.2f,%.2f,"        /* mem_total, free, used, buff_cache */
        "%d,%d,%lu,%lu,"              /* tcp_conn, udp_conn, net_rx, net_tx */
        "%d,%llu,%llu,"               /* cpu_cores, disk_total, disk_avail */
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,"  /* disk reads, writes, reading_ms, writing_ms, iotime, ios_in_progress, weighted_io_time */
        "%s,%s",                      /* machine_id, hostname */
        m->timestamp,
        m->uptime.uptime_s,
        m->loadavg.load_1min, m->loadavg.load_5min, m->loadavg.load_15min,
        m->loadavg.running_tasks, m->loadavg.total_tasks,
        m->cpu.user, m->cpu.system, m->cpu.nice, m->cpu.idle,
        m->cpu.iowait, m->cpu.irq, m->cpu.softirq, m->cpu.steal,
        m->mem.total_mib, m->mem.free_mib, m->mem.used_mib, m->mem.buff_cache_mib,
        m->net_conn.tcp_connections, m->net_conn.udp_connections,
        m->net_traffic.rx_bytes, m->net_traffic.tx_bytes,
        m->sys.cpu_num_cores, m->disk_space.total_kb, m->disk_space.avail_kb,
        m->disk.reads_completed, m->disk.writes_completed,
        m->disk.reading_ms, m->disk.writing_ms, m->disk.iotime_ms,
        m->disk.ios_in_progress, m->disk.weighted_io_time,
        m->sys.machine_id, m->sys.hostname);
    
    if (n < 0 || (size_t)n >= sizeof(values)) {
        fprintf(stderr, "Error: Values string too long\n");
        free(result);
        return NULL;
    }
    
    int total = snprintf(result, BUFFER_SIZE, "values=%s", values);
    if (total < 0 || total >= BUFFER_SIZE) {
        fprintf(stderr, "Error: Final string too long\n");
        free(result);
        return NULL;
    }
    
    return result;
}

/**
 * 使用curl发送POST请求
 */
static int send_post_request(const char *url, const char *data)
{
    char command[BUFFER_SIZE];
    
    /* 转义数据中的单引号，避免shell注入 */
    char escaped_data[VALUES_BUFFER_SIZE * 2];
    size_t j = 0;
    for (size_t i = 0; data[i] && j < sizeof(escaped_data) - 1; i++) {
        if (data[i] == '\'') {
            if (j + 4 < sizeof(escaped_data)) {
                escaped_data[j++] = '\'';
                escaped_data[j++] = '\\';
                escaped_data[j++] = '\'';
                escaped_data[j++] = '\'';
            }
        } else {
            escaped_data[j++] = data[i];
        }
    }
    escaped_data[j] = '\0';
    
    int ret = snprintf(command, sizeof(command), 
                       "curl -s -X POST -d '%s' '%s' --connect-timeout 10 --max-time 30",
                       escaped_data, url);
    if (ret < 0 || (size_t)ret >= sizeof(command)) {
        fprintf(stderr, "Error: Command too long\n");
        return -1;
    }
    
    return system(command);
}

/* ==================== 主函数 ==================== */

/**
 * 采集所有指标
 */
static int collect_all_metrics(Metrics *m)
{
    int errors = 0;
    
    m->timestamp = time(NULL);
    
    if (collect_uptime(&m->uptime) != 0) errors++;
    if (collect_loadavg(&m->loadavg) != 0) errors++;
    if (collect_cpu_stats(&m->cpu) != 0) errors++;
    if (collect_mem_stats(&m->mem) != 0) errors++;
    if (collect_net_connections(&m->net_conn) != 0) errors++;
    if (collect_net_traffic(&m->net_traffic) != 0) errors++;
    if (collect_disk_stats(&m->disk) != 0) errors++;
    if (collect_disk_space(&m->disk_space) != 0) errors++;
    if (collect_machine_id(m->sys.machine_id, sizeof(m->sys.machine_id)) != 0) errors++;
    if (collect_hostname(m->sys.hostname, sizeof(m->sys.hostname)) != 0) errors++;
    
    m->sys.cpu_num_cores = collect_cpu_cores();
    
    return errors;
}

/**
 * 打印使用说明
 */
static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s -u <url>\n", program);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -u <url>    Report URL (required)\n");
    fprintf(stderr, "  -h          Show this help message\n");
}

int main(int argc, char *argv[])
{
    char url[URL_MAX_LEN] = {0};
    int opt;
    
    while ((opt = getopt(argc, argv, "u:h")) != -1) {
        switch (opt) {
            case 'u':
                strncpy(url, optarg, sizeof(url) - 1);
                url[sizeof(url) - 1] = '\0';
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    if (url[0] == '\0') {
        fprintf(stderr, "Error: -u <url> is required\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    Metrics metrics;
    
    /* 主循环：每10秒采集并上报一次 */
    while (1) {
        /* 对齐到10秒边界 */
        time_t now = time(NULL);
        int sleep_sec = REPORT_INTERVAL_SEC - (now % REPORT_INTERVAL_SEC);
        sleep(sleep_sec);
        
        /* 采集指标 */
        if (collect_all_metrics(&metrics) > 0) {
            fprintf(stderr, "Warning: Some metrics failed to collect\n");
        }
        
        /* 格式化数据 */
        char *data = format_metrics(&metrics);
        if (!data) {
            fprintf(stderr, "Failed to format metrics\n");
            continue;
        }
        
        /* 上报数据 */
        int ret = send_post_request(url, data);
        if (ret != 0) {
            fprintf(stderr, "Failed to send data (curl returned %d)\n", ret);
        }
        
        free(data);
    }
    
    return EXIT_SUCCESS;
}
