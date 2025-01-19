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


typedef struct {
    long cpu_delay;                // CPU 延迟（微秒）
    long disk_delay;               // 磁盘延迟（微秒）
    unsigned long net_tx;          // 默认路由接口的发送流量（字节）
    unsigned long net_rx;          // 默认路由接口的接收流量（字节）
    unsigned long disks_total_kb;  // 磁盘总容量（KB）
    unsigned long disks_avail_kb;  // 磁盘可用容量（KB）
    int cpu_num_cores;             // CPU 核心数
    char machine_id[33];           // 固定长度的字符数组，存储机器 ID
} SystemMetrics;


typedef struct {
    long uptime;                   // 系统运行时间（秒）
    double load_1min;              // 1 分钟负载
    double load_5min;              // 5 分钟负载
    double load_15min;             // 15 分钟负载
    int task_total;                // 总任务数
    int task_running;              // 正在运行的任务数
    double cpu_us;                 // 用户空间占用 CPU 统计值
    double cpu_sy;                 // 内核空间占用 CPU 统计值
    double cpu_ni;                 // 用户进程空间内改变过优先级的进程占用 CPU 统计值
    double cpu_id;                 // 空闲 CPU 统计值
    double cpu_wa;                 // 等待 I/O 的 CPU 统计值
    double cpu_hi;                 // 硬件中断占用 CPU 统计值
    double cpu_st;                 // 虚拟机偷取的 CPU 统计值
    double mem_total;              // 总内存大小
    double mem_free;               // 空闲内存大小
    double mem_used;               // 已用内存大小
    double mem_buff_cache;         // 缓存和缓冲区内存大小
    int tcp_connections;           // TCP 连接数
    int udp_connections;           // UDP 连接数
} ProcResult;

// 从 /proc/uptime 读取系统运行时间
void read_uptime(ProcResult *result) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        perror("Failed to open /proc/uptime");
        exit(EXIT_FAILURE);
    }
    double uptime;
    fscanf(fp, "%lf", &uptime);
    fclose(fp);
    result->uptime = (long)uptime;
}

// 从 /proc/loadavg 读取负载信息和任务信息
void read_loadavg_and_tasks(ProcResult *result) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        perror("Failed to open /proc/loadavg");
        exit(EXIT_FAILURE);
    }
    char line[256];
    fgets(line, sizeof(line), fp);
    fclose(fp);
    sscanf(line, "%lf %lf %lf %d/%d",
           &result->load_1min, &result->load_5min, &result->load_15min,
           &result->task_running, &result->task_total);
}

// 从 /proc/stat 读取 CPU 信息
void read_cpu_info(ProcResult *result) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Failed to open /proc/stat");
        exit(EXIT_FAILURE);
    }
    char line[256];
    fgets(line, sizeof(line), fp); // 读取第一行（总 CPU 信息）
    fclose(fp);
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    
    result->cpu_us = user;
    result->cpu_sy = system;
    result->cpu_ni = nice;
    result->cpu_id = idle;
    result->cpu_wa = iowait;
    result->cpu_hi = irq;
    result->cpu_st = steal;
}

// 从 /proc/meminfo 读取内存信息
void read_mem_info(ProcResult *result) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("Failed to open /proc/meminfo");
        exit(EXIT_FAILURE);
    }
    char line[256];
    unsigned long long mem_total = 0, mem_free = 0, mem_buffers = 0, mem_cached = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %llu kB", &mem_total) == 1) continue;
        if (sscanf(line, "MemFree: %llu kB", &mem_free) == 1) continue;
        if (sscanf(line, "Buffers: %llu kB", &mem_buffers) == 1) continue;
        if (sscanf(line, "Cached: %llu kB", &mem_cached) == 1) continue;
    }
    fclose(fp);
    result->mem_total = mem_total / 1024.0; // 转换为 MiB
    result->mem_free = mem_free / 1024.0;
    result->mem_used = (mem_total - mem_free) / 1024.0;
    result->mem_buff_cache = (mem_buffers + mem_cached) / 1024.0;
}

// 从 /proc/net/tcp 和 /proc/net/udp 读取 TCP/UDP 连接数
void read_network_info(ProcResult *result) {
    FILE *fp;
    char line[256];
    result->tcp_connections = 0;
    fp = fopen("/proc/net/tcp", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, ":") != NULL) result->tcp_connections++;
        }
        fclose(fp);
    } else {
        perror("Failed to open /proc/net/tcp");
    }
    result->udp_connections = 0;
    fp = fopen("/proc/net/udp", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, ":") != NULL) result->udp_connections++;
        }
        fclose(fp);
    } else {
        perror("Failed to open /proc/net/udp");
    }
}

// 获取 Linux 服务器的 machine-id
int get_machine_id(char *buffer, size_t buffer_size) {
    char *paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id", NULL};
    for (int i = 0; paths[i] != NULL; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (fp) {
            if (fgets(buffer, buffer_size, fp)) {
                fclose(fp);
                char *newline = strchr(buffer, '\n');
                if (newline) *newline = '\0';
                return 0; // 成功读取
            }
            fclose(fp);
        }
    }
    return -1; // 读取失败
}

// 获取总的出流量和入流量
void get_total_traffic(unsigned long *net_tx, unsigned long *net_rx) {
    FILE *fp;
    char line[256];
    *net_tx = 0;
    *net_rx = 0;
    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        perror("Failed to open /proc/net/dev");
        return;
    }
    fgets(line, sizeof(line), fp); // 跳过表头
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        unsigned long rx, tx;
        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   iface, &rx, &tx) == 3) {
            char *end = iface + strlen(iface) - 1;
            while (end >= iface && (*end == ' ' || *end == '\t')) *end-- = '\0';
            if (strncmp(iface, "lo", 2) == 0 || strncmp(iface, "br", 2) == 0 ||
                strncmp(iface, "docker", 6) == 0 || strncmp(iface, "veth", 4) == 0 ||
                strncmp(iface, "virbr", 5) == 0) continue;
            *net_rx += rx;
            *net_tx += tx;
        }
    }
    fclose(fp);
}

// 计算圆周率并返回时间（微秒）
long calculate_pi(int iterations) {
    double pi = 0.0;
    int sign = 1;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        pi += sign * (4.0 / (2 * i + 1));
        sign *= -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
}

// 获取磁盘延迟（微秒）
long get_disk_delay(int iterations) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "tempfile%d", i);
        int fd = open(filename, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            write(fd, "test", 4);
            close(fd);
            unlink(filename);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
}

// 获取磁盘总容量和可用容量
void get_disk_usage(unsigned long *disks_total_kb, unsigned long *disks_avail_kb) {
    FILE *fp;
    struct mntent *mnt;
    struct statvfs vfs;
    unsigned long total_kb = 0, avail_kb = 0;
    fp = setmntent("/proc/mounts", "r");
    if (!fp) {
        perror("Failed to open /proc/mounts");
        *disks_total_kb = *disks_avail_kb = 0;
        return;
    }
    while ((mnt = getmntent(fp)) != NULL) {
        if (strncmp(mnt->mnt_fsname, "/dev/", 5) == 0 &&
            strncmp(mnt->mnt_fsname, "/dev/loop", 9) != 0 &&
            strncmp(mnt->mnt_fsname, "/dev/ram", 8) != 0 &&
            strncmp(mnt->mnt_fsname, "/dev/dm-", 8) != 0) {
            if (statvfs(mnt->mnt_dir, &vfs) == 0) {
                unsigned long block_size = vfs.f_frsize / 1024;
                total_kb += vfs.f_blocks * block_size;
                avail_kb += vfs.f_bavail * block_size;
            }
        }
    }
    endmntent(fp);
    *disks_total_kb = total_kb;
    *disks_avail_kb = avail_kb;
}

// 将结构体转换为键值对字符串
char *metrics_to_kv(const SystemMetrics *metrics, const ProcResult *proc_result) {
    char *kv = malloc(4096);
    if (!kv) return NULL;
    snprintf(kv, 4096,
             "uptime=%ld&"
             "load_1min=%.2f&"
             "load_5min=%.2f&"
             "load_15min=%.2f&"
             "net_tx=%lu&"
             "net_rx=%lu&"
             "disk_delay=%ld&"
             "cpu_delay=%ld&"
             "disks_total_kb=%lu&"
             "disks_avail_kb=%lu&"
             "tcp_connections=%d&"
             "udp_connections=%d&"
             "cpu_num_cores=%d&"
             "machine_id=%s&"
             "task_total=%d&"
             "task_running=%d&"
             "cpu_us=%.1f&"
             "cpu_sy=%.1f&"
             "cpu_ni=%.1f&"
             "cpu_id=%.1f&"
             "cpu_wa=%.1f&"
             "cpu_hi=%.1f&"
             "cpu_st=%.1f&"
             "mem_total=%.1f&"
             "mem_free=%.1f&"
             "mem_used=%.1f&"
             "mem_buff_cache=%.1f",
             proc_result->uptime, proc_result->load_1min, proc_result->load_5min, proc_result->load_15min,
             metrics->net_tx, metrics->net_rx, metrics->disk_delay, metrics->cpu_delay,
             metrics->disks_total_kb, metrics->disks_avail_kb, proc_result->tcp_connections,
             proc_result->udp_connections, metrics->cpu_num_cores, metrics->machine_id,
             proc_result->task_total, proc_result->task_running, proc_result->cpu_us,
             proc_result->cpu_sy, proc_result->cpu_ni, proc_result->cpu_id, proc_result->cpu_wa,
             proc_result->cpu_hi, proc_result->cpu_st, proc_result->mem_total, proc_result->mem_free,
             proc_result->mem_used, proc_result->mem_buff_cache);
    return kv;
}

// 使用外部 curl 命令发送 POST 请求
int send_post_request(const char *url, const char *data) {
    char command[4096];
    snprintf(command, sizeof(command), "curl -X POST -d '%s' '%s'", data, url);
    return system(command);
}

// 采集所有指标
void collect_metrics(SystemMetrics *metrics, ProcResult *proc_result) {
    metrics->cpu_delay = calculate_pi(1000000);
    metrics->disk_delay = get_disk_delay(100);
    get_disk_usage(&metrics->disks_total_kb, &metrics->disks_avail_kb);
    get_total_traffic(&metrics->net_tx, &metrics->net_rx);
    metrics->cpu_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (get_machine_id(metrics->machine_id, sizeof(metrics->machine_id)) != 0) {
        strncpy(metrics->machine_id, "unknown", sizeof(metrics->machine_id));
    }
    read_uptime(proc_result);
    read_loadavg_and_tasks(proc_result);
    read_cpu_info(proc_result);
    read_mem_info(proc_result);
    read_network_info(proc_result);
}

int main(int argc, char *argv[]) {
    int interval = 10;
    char url[256] = "";
    int opt;
    while ((opt = getopt(argc, argv, "s:u:")) != -1) {
        switch (opt) {
            case 's':
                interval = atoi(optarg);
                break;
            case 'u':
                strncpy(url, optarg, sizeof(url) - 1);
                break;
            default:
                fprintf(stderr, "Usage: %s -s <interval> -u <url>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (strlen(url) == 0) {
        fprintf(stderr, "Error: -u <url> is required.\n");
        fprintf(stderr, "Usage: %s -s <interval> -u <url>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    while (1) {
        SystemMetrics metrics = {0};
        ProcResult proc_result = {0};
        collect_metrics(&metrics, &proc_result);
        char *kv_data = metrics_to_kv(&metrics, &proc_result);
        if (!kv_data) {
            fprintf(stderr, "Failed to convert metrics to key-value pairs\n");
            continue;
        }
        if (send_post_request(url, kv_data) != 0) {
            fprintf(stderr, "Failed to send data\n");
        }
        free(kv_data);
        sleep(interval);
    }
    return 0;
}
