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

// 定义结构体，封装采集的指标
typedef struct {
    long uptime;                   // 系统运行时间（秒）
    double load_1min;              // 1 分钟负载
    double load_5min;              // 5 分钟负载
    double load_15min;             // 15 分钟负载
    long cpu_delay;                // CPU 延迟（微秒）
    long disk_delay;               // 磁盘延迟（微秒）
    unsigned long net_tx;          // 默认路由接口的发送流量（字节）
    unsigned long net_rx;          // 默认路由接口的接收流量（字节）
    unsigned long disks_total_kb;  // 磁盘总容量（KB）
    unsigned long disks_avail_kb;  // 磁盘可用容量（KB）
    int tcp_connections;           // TCP 连接数
    int udp_connections;           // UDP 连接数
    int cpu_num_cores;             // CPU 核心数
    char machine_id[33];           // 固定长度的字符数组，存储机器 ID
} SystemMetrics;

// 定义结构体，存储任务、CPU、内存信息
typedef struct {
    // 任务信息
    int task_total;                // 总任务数
    int task_running;              // 正在运行的任务数
    int task_sleeping;             // 睡眠中的任务数
    int task_stopped;              // 已停止的任务数
    int task_zombie;               // 僵尸任务数

    // CPU 使用率（百分比）
    double cpu_us;                 // 用户空间占用 CPU 百分比
    double cpu_sy;                 // 内核空间占用 CPU 百分比
    double cpu_ni;                 // 用户进程空间内改变过优先级的进程占用 CPU 百分比
    double cpu_id;                 // 空闲 CPU 百分比
    double cpu_wa;                 // 等待 I/O 的 CPU 百分比
    double cpu_hi;                 // 硬件中断占用 CPU 百分比
    double cpu_st;                 // 虚拟机偷取的 CPU 百分比

    // 内存使用情况（MiB）
    double mem_total;              // 总内存大小
    double mem_free;               // 空闲内存大小
    double mem_used;               // 已用内存大小
    double mem_buff_cache;         // 缓存和缓冲区内存大小
} TopResult;



// 获取 Linux 服务器的 machine-id，通常在操作系统存续的生命周期内不会变化
int get_machine_id(char *buffer, size_t buffer_size) {
    char *paths[] = {"/etc/machine-id", "/var/lib/dbus/machine-id", NULL};
    for (int i = 0; paths[i] != NULL; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (fp) {
            if (fgets(buffer, buffer_size, fp)) {
                fclose(fp);

                // 去掉换行符（如果有）
                char *newline = strchr(buffer, '\n');
                if (newline) *newline = '\0';
                return 0; // 成功读取
            }
            fclose(fp);
        }
    }
    return -1; // 读取失败
}


// 获取系统运行时间
long get_uptime() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
    return 0;
}

// 获取系统负载
void get_load_avg(double *load_1min, double *load_5min, double *load_15min) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp) {
        fscanf(fp, "%lf %lf %lf", load_1min, load_5min, load_15min);
        fclose(fp);
    } else {
        *load_1min = *load_5min = *load_15min = 0.0;
    }
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

    // 跳过前两行（表头）
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        unsigned long rx, tx;

        if (sscanf(line, "%31[^:]: %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   iface, &rx, &tx) == 3) {
            // 去掉接口名称末尾的空格
            char *end = iface + strlen(iface) - 1;
            while (end >= iface && (*end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }

            // 排除不需要的接口
            if (strncmp(iface, "lo", 2) == 0 ||   // 排除本地回环接口
                strncmp(iface, "br", 2) == 0 ||   // 排除网桥接口
                strncmp(iface, "docker", 6) == 0 || // 排除 Docker 接口
                strncmp(iface, "veth", 4) == 0 ||  // 排除虚拟以太网接口
                strncmp(iface, "virbr", 5) == 0) { // 排除虚拟网桥接口
                continue;
            }

            // 累加流量
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

    clock_gettime(CLOCK_MONOTONIC, &start); // 获取开始时间
    for (int i = 0; i < iterations; i++) {
        pi += sign * (4.0 / (2 * i + 1));
        sign *= -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end); // 获取结束时间

    // 计算时间差（微秒）
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
}

// 获取磁盘延迟（微秒）
long get_disk_delay(int iterations) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start); // 获取开始时间

    // 模拟磁盘 I/O 操作
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

    clock_gettime(CLOCK_MONOTONIC, &end); // 获取结束时间

    // 计算时间差（微秒）
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
        // 只处理物理设备（过滤掉虚拟文件系统）
        if (strncmp(mnt->mnt_fsname, "/dev/", 5) == 0 &&
            strncmp(mnt->mnt_fsname, "/dev/loop", 9) != 0 &&
            strncmp(mnt->mnt_fsname, "/dev/ram", 8) != 0 &&
            strncmp(mnt->mnt_fsname, "/dev/dm-", 8) != 0) {
            if (statvfs(mnt->mnt_dir, &vfs) == 0) {
                // 计算总容量和可用容量（以 KB 为单位）
                unsigned long block_size = vfs.f_frsize / 1024; // 块大小转换为 KB
                total_kb += vfs.f_blocks * block_size;
                avail_kb += vfs.f_bavail * block_size;
            }
        }
    }

    endmntent(fp);

    *disks_total_kb = total_kb;
    *disks_avail_kb = avail_kb;
}

// 获取文件的行数（连接数）
int get_connection_count(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open file");
        return -1;
    }

    int count = 0;
    char line[256];

    // 跳过第一行（表头）
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    // 统计剩余行数
    while (fgets(line, sizeof(line), fp)) {
        count++;
    }

    fclose(fp);
    return count;
}

// 获取 TCP 连接数
int get_tcp_connections() {
    return get_connection_count("/proc/net/tcp");
}

// 获取 UDP 连接数
int get_udp_connections() {
    return get_connection_count("/proc/net/udp");
}

// 解析第2行任务信息
void parse_task_line(const char *line, TopResult *result) {
    char *colon_pos = strchr(line, ':'); // 找到冒号位置
    if (!colon_pos) return;

    // 提取冒号后的内容
    char tasks_info[256];
    strncpy(tasks_info, colon_pos + 1, sizeof(tasks_info) - 1);
    tasks_info[sizeof(tasks_info) - 1] = '\0';

    // 用逗号分隔并解析
    char *token = strtok(tasks_info, ",");
    while (token) {
        int value;
        char label[32];
        if (sscanf(token, "%d %s", &value, label) == 2) {
            if (strcmp(label, "total") == 0) result->task_total = value;
            else if (strcmp(label, "running") == 0) result->task_running = value;
            else if (strcmp(label, "sleeping") == 0) result->task_sleeping = value;
            else if (strcmp(label, "stopped") == 0) result->task_stopped = value;
            else if (strcmp(label, "zombie") == 0) result->task_zombie = value;
        }
        token = strtok(NULL, ",");
    }
}

// 解析第3行CPU信息
void parse_cpu_line(const char *line, TopResult *result) {
    char *colon_pos = strchr(line, ':'); // 找到冒号位置
    if (!colon_pos) return;

    // 提取冒号后的内容
    char cpu_info[256];
    strncpy(cpu_info, colon_pos + 1, sizeof(cpu_info) - 1);
    cpu_info[sizeof(cpu_info) - 1] = '\0';

    // 用逗号分隔并解析
    char *token = strtok(cpu_info, ",");
    while (token) {
        double value;
        char label[32];
        if (sscanf(token, "%lf %s", &value, label) == 2) {
            if (strcmp(label, "us") == 0) result->cpu_us = value;
            else if (strcmp(label, "sy") == 0) result->cpu_sy = value;
            else if (strcmp(label, "ni") == 0) result->cpu_ni = value;
            else if (strcmp(label, "id") == 0) result->cpu_id = value;
            else if (strcmp(label, "wa") == 0) result->cpu_wa = value;
            else if (strcmp(label, "hi") == 0) result->cpu_hi = value;
            else if (strcmp(label, "st") == 0) result->cpu_st = value;
        }
        token = strtok(NULL, ",");
    }
}

// 解析第4行内存信息
void parse_mem_line(const char *line, TopResult *result) {
    char *colon_pos = strchr(line, ':'); // 找到冒号位置
    if (!colon_pos) return;

    // 提取冒号后的内容
    char mem_info[256];
    strncpy(mem_info, colon_pos + 1, sizeof(mem_info) - 1);
    mem_info[sizeof(mem_info) - 1] = '\0';

    // 用逗号分隔并解析
    char *token = strtok(mem_info, ",");
    while (token) {
        double value;
        char label[32];
        if (sscanf(token, "%lf %s", &value, label) == 2) {
            if (strcmp(label, "total") == 0) result->mem_total = value;
            else if (strcmp(label, "free") == 0) result->mem_free = value;
            else if (strcmp(label, "used") == 0) result->mem_used = value;
            else if (strcmp(label, "buff/cache") == 0) result->mem_buff_cache = value;
        }
        token = strtok(NULL, ",");
    }
}

// 执行 top 命令并解析结果
int parse_top_result(TopResult *result) {
    FILE *fp = popen("top -bn1 | head -n 5", "r");
    if (!fp) {
        perror("Failed to run top command");
        return -1;
    }

    char line[256];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (line_num == 2) {
            parse_task_line(line, result); // 解析第2行
        } else if (line_num == 3) {
            parse_cpu_line(line, result); // 解析第3行
        } else if (line_num == 4) {
            parse_mem_line(line, result); // 解析第4行
        }
    }

    pclose(fp);
    return 0;
}


// 将结构体转换为键值对字符串
char *metrics_to_kv(const SystemMetrics *metrics, const TopResult *top_result) {
    char *kv = malloc(4096); // 分配足够大的缓冲区
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
             "task_sleeping=%d&"
             "task_stopped=%d&"
             "task_zombie=%d&"
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
             metrics->uptime,
             metrics->load_1min,
             metrics->load_5min,
             metrics->load_15min,
             metrics->net_tx,
             metrics->net_rx,
             metrics->disk_delay,
             metrics->cpu_delay,
             metrics->disks_total_kb,
             metrics->disks_avail_kb,
             metrics->tcp_connections,
             metrics->udp_connections,
             metrics->cpu_num_cores,
             metrics->machine_id,
             top_result->task_total,
             top_result->task_running,
             top_result->task_sleeping,
             top_result->task_stopped,
             top_result->task_zombie,
             top_result->cpu_us,
             top_result->cpu_sy,
             top_result->cpu_ni,
             top_result->cpu_id,
             top_result->cpu_wa,
             top_result->cpu_hi,
             top_result->cpu_st,
             top_result->mem_total,
             top_result->mem_free,
             top_result->mem_used,
             top_result->mem_buff_cache);

    return kv;
}


// 使用外部 curl 命令发送 POST 请求
int send_post_request(const char *url, const char *data) {
    char command[4096];
    snprintf(command, sizeof(command), "curl -X POST -d '%s' '%s'", data, url);
    return system(command);
}

// 采集所有指标
void collect_metrics(SystemMetrics *metrics, TopResult *top_result) {
    metrics->uptime = get_uptime();
    get_load_avg(&metrics->load_1min, &metrics->load_5min, &metrics->load_15min);
    metrics->cpu_delay = calculate_pi(1000000);
    metrics->disk_delay = get_disk_delay(100);
    get_disk_usage(&metrics->disks_total_kb, &metrics->disks_avail_kb);
    get_total_traffic(&metrics->net_tx, &metrics->net_rx);
    metrics->tcp_connections = get_tcp_connections();
    metrics->udp_connections = get_udp_connections();
    metrics->cpu_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    parse_top_result(top_result);
    if (get_machine_id(metrics->machine_id, sizeof(metrics->machine_id)) != 0) {
        strncpy(metrics->machine_id, "unknown", sizeof(metrics->machine_id)); // 读取失败时设置为 "unknown"
    }
}

int main(int argc, char *argv[]) {
    int interval = 10; // 默认采集间隔为 10 秒
    char url[256] = ""; // 取消默认报送地址

    // 解析命令行参数
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

    // 检查是否提供了 -u 参数
    if (strlen(url) == 0) {
        fprintf(stderr, "Error: -u <url> is required.\n");
        fprintf(stderr, "Usage: %s -s <interval> -u <url>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 主循环
    while (1) {
        SystemMetrics metrics = {0};
        TopResult top_result = {0};
        collect_metrics(&metrics, &top_result);

        // 将指标转换为键值对
        char *kv_data = metrics_to_kv(&metrics, &top_result);
        if (!kv_data) {
            fprintf(stderr, "Failed to convert metrics to key-value pairs\n");
            continue;
        }

        // 发送 HTTP POST 请求
        if (send_post_request(url, kv_data) != 0) {
            fprintf(stderr, "Failed to send data\n");
        }

        // 释放键值对数据
        free(kv_data);

        // 等待指定间隔
        sleep(interval);
    }

    return 0;
}