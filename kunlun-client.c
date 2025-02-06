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

// 定义 uptime 结构体
typedef struct
{
    double uptime_s;
    double idle_s;
} Uptime;

// 定义 LoadAvg 结构体
typedef struct
{
    double load_1min;
    double load_5min;
    double load_15min;
    int running_tasks;
    int total_tasks;
} LoadAvg;

// 定义 CpuInfo 结构体
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

// 定义 MemInfo 结构体
typedef struct
{
    double mem_total_mib;
    double mem_free_mib;
    double mem_used_mib;
    double mem_buff_cache_mib;
} MemInfo;

// 定义 NetInfo 结构体
typedef struct
{
    int tcp_connections;
    int udp_connections;
    unsigned long default_interface_net_tx_bytes;
    unsigned long default_interface_net_rx_bytes;
} NetInfo;

// 磁盘统计信息结构体
typedef struct
{
    unsigned long long reads_completed;  // 成功完成的读操作总数
    unsigned long long read_merges;      // 读合并次数
    unsigned long long read_sectors;     // 读取的扇区总数
    unsigned long long reading_ms;       // 花费在读操作上的总毫秒数
    unsigned long long writes_completed; // 成功完成的写操作总数
    unsigned long long write_merges;     // 写合并次数
    unsigned long long write_sectors;    // 写入的扇区总数
    unsigned long long writing_ms;       // 花费在写操作上的总毫秒数
    unsigned long long ios_in_progress;  // 当前正在进行的 I/O 操作数
    unsigned long long iotime_ms;        // 所有 I/O 操作花费的总毫秒数
    unsigned long long weighted_io_time; // 加权 I/O 时间（毫秒）
} DiskStats;

typedef struct
{
    int cpu_num_cores;                     // CPU 核心数
    long cpu_delay_us;                     // CPU 延迟（微秒）
    long disk_delay_us;                    // 磁盘延迟（微秒）
    unsigned long long root_disk_total_kb; // 磁盘总容量（KB）
    unsigned long long root_disk_avail_kb; // 磁盘可用容量（KB）
    char machine_id[33];                   // 机器 ID
    char hostname[256];                    // 主机名
} SystemInfo;

// 获取根目录挂载分区的磁盘统计信息
int get_root_diskstats(DiskStats *stats)
{
    // 检查输入参数是否有效
    if (!stats)
        return -1;

    // 初始化 stats 结构体，避免使用未初始化的值
    memset(stats, 0, sizeof(DiskStats));

    // 1. 获取根目录挂载的设备名
    FILE *mounts_file = setmntent("/proc/mounts", "r"); // 打开 /proc/mounts 文件以读取挂载信息
    if (!mounts_file)
    {
        perror("setmntent"); // 打印错误信息
        return -1;
    }

    char root_device[256] = ""; // 存储根目录设备名的缓冲区
    struct mntent *mount_entry; // 存储挂载信息的结构体指针

    // 遍历 /proc/mounts 中的每一项挂载信息
    while ((mount_entry = getmntent(mounts_file)) != NULL)
    {
        // 查找根目录的挂载项
        if (strcmp(mount_entry->mnt_dir, "/") == 0)
        {
            strncpy(root_device, mount_entry->mnt_fsname, sizeof(root_device)); // 复制设备名
            root_device[sizeof(root_device) - 1] = '\0';                        // 确保字符串以 null 结尾，防止缓冲区溢出
            break;                                                              // 找到根目录，退出循环
        }
    }
    endmntent(mounts_file); // 关闭 /proc/mounts 文件

    // 如果没有找到根目录的挂载信息
    if (strlen(root_device) == 0)
    {
        fprintf(stderr, "Could not find root device in /proc/mounts\n");
        return -1;
    }

    // 处理 /dev/ 前缀，/proc/diskstats 中不包含 /dev/
    if (strncmp(root_device, "/dev/", 5) == 0)
    {
        memmove(root_device, root_device + 5, strlen(root_device) - 4); // 移除 "/dev/" 前缀
    }

    // 2. 读取 /proc/diskstats
    FILE *diskstats_file = fopen("/proc/diskstats", "r"); // 打开 /proc/diskstats 文件
    if (!diskstats_file)
    {
        perror("/proc/diskstats"); // 打印错误信息
        return -1;
    }

    char line[1024]; // 存储 /proc/diskstats 文件中每一行的缓冲区

    // 遍历 /proc/diskstats 中的每一行
    while (fgets(line, sizeof(line), diskstats_file))
    {
        int major, minor; // 主设备号和次设备号
        char name[256];   // 设备名

        // 3. 解析并匹配设备名，兼容不同内核版本（11/14个字段），并处理可能的解析错误
        int fields_read;
        fields_read = sscanf(line, "%d %d %255s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                             &major, &minor, name,
                             &stats->reads_completed, &stats->read_merges, &stats->read_sectors, &stats->reading_ms,
                             &stats->writes_completed, &stats->write_merges, &stats->write_sectors, &stats->writing_ms,
                             &stats->ios_in_progress, &stats->iotime_ms, &stats->weighted_io_time);
        if (fields_read >= 11 && strcmp(name, root_device) == 0)
        {                           // 至少需要前11个字段才能判断设备名
            fclose(diskstats_file); // 关闭文件
            return 0;               // 成功
        }
    }

    fclose(diskstats_file);                                                         // 关闭文件
    fprintf(stderr, "Could not find diskstats for root device: %s\n", root_device); // 打印错误信息
    return -1;                                                                      // 未找到匹配的行
}

// 优化后的磁盘耗时测量函数
long long measure_disk_io_time(int num_ops)
{
    const size_t file_size = 1024 * 1024; // 1MB 文件大小
    const size_t buffer_size = 4096;      // 缓冲区大小

    char template[] = "/tmp/disk_test_XXXXXX";

    // 创建并打开临时文件
    int fd = mkstemp(template);
    if (fd == -1)
        return -1; // 创建/打开失败

    // 预分配文件空间
    if (ftruncate(fd, file_size) == -1)
    {
        close(fd);
        unlink(template);
        return -1; // 预分配失败
    }

    char buffer[buffer_size];
    // 填充缓冲区，避免读取未初始化内存
    for (size_t i = 0; i < buffer_size; ++i)
    { // 使用 ++i 更高效
        buffer[i] = 'a' + (i % 26);
    }

    struct timespec start, end;

    // 获取开始时间
    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
    {
        close(fd);
        unlink(template);
        return -1; // 获取开始时间失败
    }

    // 执行随机写操作
    for (int i = 0; i < num_ops; ++i)
    { // 使用 ++i
        // 生成随机偏移量，确保不越界
        off_t offset = rand() % (file_size - buffer_size);

        // 执行原子写操作
        if (pwrite(fd, buffer, buffer_size, offset) != buffer_size)
        {
            close(fd);
            unlink(template);
            return -1; // 写入失败
        }
    }

    // 获取结束时间
    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
    {
        close(fd);
        unlink(template);
        return -1; // 获取结束时间失败
    }

    close(fd);
    unlink(template);

    // 计算并返回耗时（微秒）
    long long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
    return elapsed_ns / 1000;
}

// 安全的文件打开函数
static FILE *safe_fopen(const char *filename, const char *mode)
{
    FILE *fp = fopen(filename, mode);
    if (!fp)
    {
        perror(filename);
    }
    return fp;
}

// 从 /proc/uptime 读取系统运行时间和空闲时间
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

// 从 /proc/loadavg 读取负载信息和任务信息
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

// 从 /proc/stat 读取 CPU 信息
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

// 从 /proc/meminfo 读取内存信息
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

// 从 /proc/net/tcp 和 /proc/net/udp 读取 TCP/UDP 连接数
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
            // 跳过第一行标题行
            if (fgets(line, sizeof(line), fp) == NULL)
            {
                fprintf(stderr, "Error reading header line from %s\n", files[i]);
                fclose(fp);
                continue; // 继续下一个文件
            }

            while (fgets(line, sizeof(line), fp))
            {
                if (strchr(line, ':') != NULL)
                { // 使用 strchr 效率更高
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

// 获取 Linux 服务器的 machine-id
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
                buffer[strcspn(buffer, "\n")] = 0; // 安全去除换行符
                return 0;
            }
            fclose(fp);
        }
    }
    strncpy(buffer, "unknown", buffer_size);
    buffer[buffer_size - 1] = '\0';
    return -1;
}

// 获取主机名
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

// 去除字符串的前导空格
void trim_leading_whitespace(char *str)
{
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
    {
        start++;
    }
    memmove(str, start, strlen(start) + 1);
}



// 使用默认路由来获取流量数据的方法在实践后发现存在不足，还是直接获取最大流量的接口更加稳健，无论是转发还是 warp 最终都要走真正的外网接口出去
// 获取最大流量接口的上传和下载流量数据（单位为字节）
int get_max_traffic_interface(unsigned long *rx_bytes, unsigned long *tx_bytes) {
    FILE *fp;
    char line[256];
    char max_iface[32] = {0}; // 存储流量最大的接口名称
    unsigned long max_total = 0; // 存储最大流量值

    // 初始化返回值
    *rx_bytes = 0;
    *tx_bytes = 0;

    // 打开 /proc/net/dev 文件
    if ((fp = fopen("/proc/net/dev", "r")) == NULL) {
        perror("Failed to open /proc/net/dev");
        return -1;
    }

    // 跳过前两行（表头）
    for (int i = 0; i < 2; i++) {
        if (!fgets(line, sizeof(line), fp)) {
            fclose(fp);
            fprintf(stderr, "Unexpected end of file\n");
            return -1;
        }
    }

    // 遍历每一行，找到流量最大的接口
    while (fgets(line, sizeof(line), fp)) {
        char name[32];
        unsigned long rx, tx;

        // 解析接口名称和流量数据
        if (sscanf(line, " %31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   name, &rx, &tx) == 3) {
            // 计算当前接口的总流量
            unsigned long total = rx + tx;

            // 如果当前接口的总流量大于已知的最大流量，则更新最大值
            if (total > max_total) {
                max_total = total;
                *rx_bytes = rx;
                *tx_bytes = tx;
                strncpy(max_iface, name, sizeof(max_iface) - 1);
            }
        }
    }

    fclose(fp);

    // 如果没有找到任何接口，返回错误
    if (max_total == 0) {
        fprintf(stderr, "No interface found with traffic\n");
        return -1;
    }

    // printf("Max traffic interface: %s (rx: %lu, tx: %lu)\n", max_iface, *rx_bytes, *tx_bytes);
    return 0;
}




// 获取磁盘空间信息，单位为 KB
int get_disk_space_kb(const char *path, unsigned long long *total_size_kb, unsigned long long *available_size_kb)
{
    const char *effective_path = (path && *path) ? path : "/";
    struct statvfs vfs;

    if (statvfs(effective_path, &vfs) == -1)
    {
        perror("statvfs"); // 打印 statvfs 错误信息
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

// 计算时间差（微秒）
static long time_diff_us(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000 + (end->tv_nsec - start->tv_nsec) / 1000;
}

// 计算圆周率并返回时间（微秒）
long calculate_pi(int iterations)
{
    if (iterations <= 0)
    {
        fprintf(stderr, "Iterations must be positive\n");
        return -1; // 返回错误码
    }

    double pi = 0.0;
    int sign = 1;
    struct timespec start, end;

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
    {
        perror("clock_gettime");
        return -1;
    }

    for (int i = 0; i < iterations; i++)
    {
        pi += sign * (4.0 / (2 * i + 1));
        sign *= -1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
    {
        perror("clock_gettime");
        return -1;
    }

    return time_diff_us(&start, &end);
}

// 获取磁盘延迟（微秒）
long get_disk_delay(int iterations)
{
    if (iterations <= 0)
    {
        fprintf(stderr, "Iterations must be positive\n");
        return -1; // 返回错误码
    }

    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
    {
        perror("clock_gettime");
        return -1;
    }

    for (int i = 0; i < iterations; i++)
    {
        char filename[32]; // 文件名更短，减少内存占用
        snprintf(filename, sizeof(filename), "tmp%d", i);

        int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644); // 添加 O_TRUNC
        if (fd == -1)
        {
            perror("open");
            continue; // 继续下一个迭代，避免提前退出
        }

        if (write(fd, "test", 4) != 4)
        {
            perror("write");
        }

        if (close(fd) == -1)
        {
            perror("close");
        }

        if (unlink(filename) == -1)
        {
            perror("unlink");
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
    {
        perror("clock_gettime");
        return -1;
    }

    return time_diff_us(&start, &end);
}

// 使用外部 curl 命令发送 POST 请求
int send_post_request(const char *url, const char *data)
{
    char command[4096];
    // 使用curl静默跟随POST数据到用户指定的上报地址并静默标准输出
    int ret = snprintf(command, sizeof(command), "curl -sL  -X POST -d '%s' '%s' > /dev/null", data, url);
    if (ret < 0 || ret >= sizeof(command))
    {
        fprintf(stderr, "Error: Command too long or encoding error\n");
        return -1;
    }
    return system(command);
}

// 采集所有指标
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
    if (get_root_diskstats(diskstats) != 0)
    {
        fprintf(stderr, "Failed to get diskstats\n");
    }

    sysinfo->cpu_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    sysinfo->cpu_delay_us = calculate_pi(100000); // 使用 calculate_pi 作为 CPU 延迟的近似值
    sysinfo->disk_delay_us = measure_disk_io_time(10);
    if (get_disk_space_kb("/", &sysinfo->root_disk_total_kb, &sysinfo->root_disk_avail_kb) != 0)
    {
        fprintf(stderr, "Failed to get disk space\n");
    }
    if (get_max_traffic_interface(&netinfo->default_interface_net_rx_bytes, &netinfo->default_interface_net_tx_bytes) != 0)
    {
        fprintf(stderr, "Failed to get net traffic\n");
    }
}

// 将指标转换为 values=v1,v2,v3,... 格式的字符串
char *metrics_to_kv(int timestamp, Uptime *uptime, LoadAvg *loadavg, CpuInfo *cpuinfo, MemInfo *meminfo, NetInfo *netinfo, SystemInfo *sysinfo, DiskStats *diskstats)
{
    char *kv_string = malloc(8192); // Increased buffer size for safety
    if (!kv_string)
    {
        perror("malloc");
        return NULL;
    }

    // Prepare a temporary buffer for the values only
    char values_buffer[8192]; // Increased buffer size
    int values_len = 0;

    // Use snprintf to build the comma-separated values
    values_len += snprintf(values_buffer + values_len, sizeof(values_buffer) - values_len,
                           "%ld,%ld,%.2lf,%.2lf,%.2lf,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2lf,%.2lf,%.2lf,%.2lf,%d,%d,%lu,%lu,%d,%ld,%lld,%llu,%llu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%s,%s",
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
                           sysinfo->cpu_num_cores, sysinfo->cpu_delay_us, sysinfo->disk_delay_us,
                           sysinfo->root_disk_total_kb, sysinfo->root_disk_avail_kb,
                           diskstats->reads_completed, diskstats->writes_completed, diskstats->reading_ms, diskstats->writing_ms,
                           diskstats->iotime_ms, diskstats->ios_in_progress, diskstats->weighted_io_time,
                           sysinfo->machine_id, sysinfo->hostname);

    if (values_len < 0 || values_len >= sizeof(values_buffer))
    {
        fprintf(stderr, "Error: Values string too long\n");
        free(kv_string);
        return NULL;
    }

    // Now build the final "values=..." string
    int kv_len = snprintf(kv_string, 8192, "values=%s", values_buffer);

    if (kv_len < 0 || kv_len >= 8192)
    {
        fprintf(stderr, "Error: Key-value string too long\n");
        free(kv_string);
        return NULL;
    }

    return kv_string;
}

int main(int argc, char *argv[])
{
    char url[256] = "";
    int opt;

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