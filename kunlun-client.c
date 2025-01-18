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
    long uptime;
    double load_1min, load_5min, load_15min;
    int total_tasks, running_tasks, sleeping_tasks, stopped_tasks, zombie_tasks;
    double cpu_user, cpu_system, cpu_nice, cpu_idle, cpu_iowait, cpu_irq, cpu_softirq, cpu_steal;
    unsigned long mem_total, mem_free, mem_used, mem_buffers, mem_cached;
    unsigned long swap_total, swap_free;
    unsigned long net_tx, net_rx; // 默认路由接口的流量
    long disk_delay; // 磁盘延迟（微秒）
    int tcp_connections;
    long cpu_delay; // CPU 延迟（微秒）
    unsigned long disks_total_kb; // 磁盘总容量（KB）
    unsigned long disks_avail_kb; // 磁盘可用容量（KB）
} SystemMetrics;

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

// 获取任务信息
void get_task_info(int *total_tasks, int *running_tasks, int *sleeping_tasks, int *stopped_tasks, int *zombie_tasks) {
    DIR *dir;
    struct dirent *entry;
    *total_tasks = *running_tasks = *sleeping_tasks = *stopped_tasks = *zombie_tasks = 0;

    dir = opendir("/proc");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0) {
                (*total_tasks)++;
                char path[256];
                snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
                FILE *fp = fopen(path, "r");
                if (fp) {
                    char state;
                    fscanf(fp, "%*d %*s %c", &state);
                    fclose(fp);
                    switch (state) {
                        case 'R': (*running_tasks)++; break;
                        case 'S': (*sleeping_tasks)++; break;
                        case 'D': (*sleeping_tasks)++; break;
                        case 'T': (*stopped_tasks)++; break;
                        case 'Z': (*zombie_tasks)++; break;
                    }
                }
            }
        }
        closedir(dir);
    }
}

// 获取 CPU 使用情况
void get_cpu_usage(double *cpu_user, double *cpu_system, double *cpu_nice, double *cpu_idle, double *cpu_iowait, double *cpu_irq, double *cpu_softirq, double *cpu_steal) {
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "cpu ", 4) == 0) {
                unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
                sscanf(line + 5, "%lu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
                unsigned long total = user + nice + system + idle + iowait + irq + softirq + steal;
                if (total > 0) {
                    *cpu_user = (double)user / total * 100;
                    *cpu_system = (double)system / total * 100;
                    *cpu_nice = (double)nice / total * 100;
                    *cpu_idle = (double)idle / total * 100;
                    *cpu_iowait = (double)iowait / total * 100;
                    *cpu_irq = (double)irq / total * 100;
                    *cpu_softirq = (double)softirq / total * 100;
                    *cpu_steal = (double)steal / total * 100;
                }
                break;
            }
        }
        fclose(fp);
    } else {
        *cpu_user = *cpu_system = *cpu_nice = *cpu_idle = *cpu_iowait = *cpu_irq = *cpu_softirq = *cpu_steal = 0.0;
    }
}

// 获取内存使用情况
void get_memory_usage(unsigned long *mem_total, unsigned long *mem_free, unsigned long *mem_used, unsigned long *mem_buffers, unsigned long *mem_cached, unsigned long *swap_total, unsigned long *swap_free) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", mem_total);
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line + 8, "%lu", mem_free);
            } else if (strncmp(line, "Buffers:", 8) == 0) {
                sscanf(line + 8, "%lu", mem_buffers);
            } else if (strncmp(line, "Cached:", 7) == 0) {
                sscanf(line + 7, "%lu", mem_cached);
            } else if (strncmp(line, "SwapTotal:", 10) == 0) {
                sscanf(line + 10, "%lu", swap_total);
            } else if (strncmp(line, "SwapFree:", 9) == 0) {
                sscanf(line + 9, "%lu", swap_free);
            }
        }
        *mem_used = *mem_total - *mem_free - *mem_buffers - *mem_cached;
        fclose(fp);
    } else {
        *mem_total = *mem_free = *mem_used = *mem_buffers = *mem_cached = *swap_total = *swap_free = 0;
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

        // 解析接口名称和流量
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


// 获取磁盘延迟（微秒）
long get_disk_delay() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start); // 获取开始时间

    // 模拟磁盘 I/O 操作
    for (int i = 0; i < 100; i++) {
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

// 获取 TCP 连接数
int get_tcp_connections() {
    FILE *fp = fopen("/proc/net/tcp", "r");
    if (fp) {
        int count = 0;
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "  sl", 4) != 0 && strncmp(line, "sl", 2) != 0) {
                count++;
            }
        }
        fclose(fp);
        return count;
    }
    return 0;
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


// 将结构体转换为键值对字符串
char *metrics_to_kv(const SystemMetrics *metrics) {
    char *kv = malloc(4096); // 分配足够大的缓冲区
    if (!kv) {
        return NULL;
    }

    snprintf(kv, 4096,
             "uptime=%ld&"
             "load_1min=%.2f&"
             "load_5min=%.2f&"
             "load_15min=%.2f&"
             "total_tasks=%d&"
             "running_tasks=%d&"
             "sleeping_tasks=%d&"
             "stopped_tasks=%d&"
             "zombie_tasks=%d&"
             "cpu_user=%.2f&"
             "cpu_system=%.2f&"
             "cpu_nice=%.2f&"
             "cpu_idle=%.2f&"
             "cpu_iowait=%.2f&"
             "cpu_irq=%.2f&"
             "cpu_softirq=%.2f&"
             "cpu_steal=%.2f&"
             "mem_total=%lu&"
             "mem_free=%lu&"
             "mem_used=%lu&"
             "mem_buffers=%lu&"
             "mem_cached=%lu&"
             "swap_total=%lu&"
             "swap_free=%lu&"
             "net_tx=%lu&"
             "net_rx=%lu&"
             "disk_delay=%ld&"
             "tcp_connections=%d&"
             "cpu_delay=%ld&"
             "disks_total_kb=%lu&"
             "disks_avail_kb=%lu",
             metrics->uptime,
             metrics->load_1min,
             metrics->load_5min,
             metrics->load_15min,
             metrics->total_tasks,
             metrics->running_tasks,
             metrics->sleeping_tasks,
             metrics->stopped_tasks,
             metrics->zombie_tasks,
             metrics->cpu_user,
             metrics->cpu_system,
             metrics->cpu_nice,
             metrics->cpu_idle,
             metrics->cpu_iowait,
             metrics->cpu_irq,
             metrics->cpu_softirq,
             metrics->cpu_steal,
             metrics->mem_total,
             metrics->mem_free,
             metrics->mem_used,
             metrics->mem_buffers,
             metrics->mem_cached,
             metrics->swap_total,
             metrics->swap_free,
             metrics->net_tx,
             metrics->net_rx,
             metrics->disk_delay,
             metrics->tcp_connections,
             metrics->cpu_delay,
             metrics->disks_total_kb,
             metrics->disks_avail_kb);

    return kv;
}

// 使用外部 curl 命令发送 POST 请求
int send_post_request(const char *url, const char *data) {
    // printf("URL: %s\n", url);
    // printf("Data: %s\n", data);
    char command[4096];
    snprintf(command, sizeof(command), "curl -X POST -d '%s' '%s'", data, url);
    return system(command);
}

// 采集所有指标
void collect_metrics(SystemMetrics *metrics) {
    metrics->uptime = get_uptime();
    get_load_avg(&metrics->load_1min, &metrics->load_5min, &metrics->load_15min);
    get_task_info(&metrics->total_tasks, &metrics->running_tasks, &metrics->sleeping_tasks, &metrics->stopped_tasks, &metrics->zombie_tasks);
    get_cpu_usage(&metrics->cpu_user, &metrics->cpu_system, &metrics->cpu_nice, &metrics->cpu_idle, &metrics->cpu_iowait, &metrics->cpu_irq, &metrics->cpu_softirq, &metrics->cpu_steal);
    get_memory_usage(&metrics->mem_total, &metrics->mem_free, &metrics->mem_used, &metrics->mem_buffers, &metrics->mem_cached, &metrics->swap_total, &metrics->swap_free);
    get_total_traffic(&metrics->net_tx, &metrics->net_rx);
    metrics->disk_delay = get_disk_delay();
    metrics->tcp_connections = get_tcp_connections();
    metrics->cpu_delay = calculate_pi(1000000);
    get_disk_usage(&metrics->disks_total_kb, &metrics->disks_avail_kb);
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
        collect_metrics(&metrics);

        // 将指标转换为键值对
        char *kv_data = metrics_to_kv(&metrics);
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