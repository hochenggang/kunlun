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


// 简单计算圆周率并返回时间（毫秒）
long calculate_pi(int iterations) {
    double pi = 0.0;
    int sign = 1;  // 符号位，用于交替加减
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);  // 记录开始时间

    for (int i = 0; i < iterations; i++) {
        pi += sign * (4.0 / (2 * i + 1));  // 莱布尼茨级数公式
        sign *= -1;  // 符号交替
    }

    clock_gettime(CLOCK_MONOTONIC, &end);  // 记录结束时间

    // 返回计算时间（毫秒）
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

// 获取系统运行时间
long get_uptime() {
    struct sysinfo info;
    sysinfo(&info);
    return info.uptime;
}

// 获取系统负载
void get_load_avg(double *load_1min, double *load_5min, double *load_15min) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        perror("Failed to open /proc/loadavg");
        return;
    }
    fscanf(fp, "%lf %lf %lf", load_1min, load_5min, load_15min);
    fclose(fp);
}

// 获取任务信息
void get_task_info(int *total_tasks, int *running_tasks, int *sleeping_tasks, int *stopped_tasks, int *zombie_tasks) {
    DIR *dir;
    struct dirent *entry;
    *total_tasks = 0;
    *running_tasks = 0;
    *sleeping_tasks = 0;
    *stopped_tasks = 0;
    *zombie_tasks = 0;

    dir = opendir("/proc");
    if (!dir) {
        perror("Failed to open /proc");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0) {
            (*total_tasks)++;
            char path[256];
            sprintf(path, "/proc/%s/stat", entry->d_name);
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

// 获取 CPU 使用情况
void get_cpu_usage(double *cpu_user, double *cpu_system, double *cpu_nice, double *cpu_idle, double *cpu_iowait, double *cpu_irq, double *cpu_softirq, double *cpu_steal) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Failed to open /proc/stat");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
            sscanf(line + 5, "%lu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
            unsigned long total = user + nice + system + idle + iowait + irq + softirq + steal;
            *cpu_user = (double)user / total * 100;
            *cpu_system = (double)system / total * 100;
            *cpu_nice = (double)nice / total * 100;
            *cpu_idle = (double)idle / total * 100;
            *cpu_iowait = (double)iowait / total * 100;
            *cpu_irq = (double)irq / total * 100;
            *cpu_softirq = (double)softirq / total * 100;
            *cpu_steal = (double)steal / total * 100;
            break;
        }
    }
    fclose(fp);
}

// 获取内存使用情况
void get_memory_usage(unsigned long *mem_total, unsigned long *mem_free, unsigned long *mem_used, unsigned long *mem_buffers, unsigned long *mem_cached) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("Failed to open /proc/meminfo");
        return;
    }
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
        }
    }
    *mem_used = *mem_total - *mem_free - *mem_buffers - *mem_cached;
    fclose(fp);
}

// 获取网络流量
void get_network_usage(unsigned long *net_tx, unsigned long *net_rx) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        perror("Failed to open /proc/net/dev");
        return;
    }
    char line[256];
    *net_tx = 0;
    *net_rx = 0;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long rx, tx;
        if (sscanf(line, "%*s %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu", &rx, &tx) == 2) {
            *net_rx += rx;
            *net_tx += tx;
        }
    }
    fclose(fp);
}

// 获取磁盘延迟（毫秒）
long get_disk_delay() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 100; i++) {
        char filename[256];
        sprintf(filename, "tempfile%d", i);
        int fd = open(filename, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            write(fd, "test", 4);
            close(fd);
            unlink(filename);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

// 获取 TCP 连接数
int get_tcp_connections() {
    FILE *fp = fopen("/proc/net/tcp", "r");
    if (!fp) {
        perror("Failed to open /proc/net/tcp");
        return -1;
    }
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

// 拼接字符串
void append_str(char *dest, size_t dest_size, const char *name, const char *value) {
    snprintf(dest + strlen(dest), dest_size - strlen(dest), "%s=%s&", name, value);
}

// 调用 curl 发送 HTTP POST 请求
int send_post_request(const char *url, const char *data) {
    char command[4096];
    snprintf(command, sizeof(command), "curl -X POST -d '%s' '%s'", data, url);
    return system(command);
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
        char post_data[8192] = {0};

        // 采集指标
        long uptime = get_uptime();
        double load_1min, load_5min, load_15min;
        get_load_avg(&load_1min, &load_5min, &load_15min);
        int total_tasks, running_tasks, sleeping_tasks, stopped_tasks, zombie_tasks;
        get_task_info(&total_tasks, &running_tasks, &sleeping_tasks, &stopped_tasks, &zombie_tasks);
        double cpu_user, cpu_system, cpu_nice, cpu_idle, cpu_iowait, cpu_irq, cpu_softirq, cpu_steal;
        get_cpu_usage(&cpu_user, &cpu_system, &cpu_nice, &cpu_idle, &cpu_iowait, &cpu_irq, &cpu_softirq, &cpu_steal);
        unsigned long mem_total, mem_free, mem_used, mem_buffers, mem_cached;
        get_memory_usage(&mem_total, &mem_free, &mem_used, &mem_buffers, &mem_cached);
        unsigned long net_tx, net_rx;
        get_network_usage(&net_tx, &net_rx);
        long disk_delay = get_disk_delay();
        int tcp_connections = get_tcp_connections();
        long cpu_delay = calculate_pi(100000000); // 计算 CPU 延迟

        // 拼接结果
        char temp_buffer[128];
        sprintf(temp_buffer, "%ld", uptime);
        append_str(post_data, sizeof(post_data), "uptime", temp_buffer);
        sprintf(temp_buffer, "%.2f", load_1min);
        append_str(post_data, sizeof(post_data), "load_1min", temp_buffer);
        sprintf(temp_buffer, "%.2f", load_5min);
        append_str(post_data, sizeof(post_data), "load_5min", temp_buffer);
        sprintf(temp_buffer, "%.2f", load_15min);
        append_str(post_data, sizeof(post_data), "load_15min", temp_buffer);
        sprintf(temp_buffer, "%d", total_tasks);
        append_str(post_data, sizeof(post_data), "total_tasks", temp_buffer);
        sprintf(temp_buffer, "%d", running_tasks);
        append_str(post_data, sizeof(post_data), "running_tasks", temp_buffer);
        sprintf(temp_buffer, "%d", sleeping_tasks);
        append_str(post_data, sizeof(post_data), "sleeping_tasks", temp_buffer);
        sprintf(temp_buffer, "%d", stopped_tasks);
        append_str(post_data, sizeof(post_data), "stopped_tasks", temp_buffer);
        sprintf(temp_buffer, "%d", zombie_tasks);
        append_str(post_data, sizeof(post_data), "zombie_tasks", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_user);
        append_str(post_data, sizeof(post_data), "cpu_user", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_system);
        append_str(post_data, sizeof(post_data), "cpu_system", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_nice);
        append_str(post_data, sizeof(post_data), "cpu_nice", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_idle);
        append_str(post_data, sizeof(post_data), "cpu_idle", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_iowait);
        append_str(post_data, sizeof(post_data), "cpu_iowait", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_irq);
        append_str(post_data, sizeof(post_data), "cpu_irq", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_softirq);
        append_str(post_data, sizeof(post_data), "cpu_softirq", temp_buffer);
        sprintf(temp_buffer, "%.2f", cpu_steal);
        append_str(post_data, sizeof(post_data), "cpu_steal", temp_buffer);
        sprintf(temp_buffer, "%lu", mem_total);
        append_str(post_data, sizeof(post_data), "mem_total", temp_buffer);
        sprintf(temp_buffer, "%lu", mem_free);
        append_str(post_data, sizeof(post_data), "mem_free", temp_buffer);
        sprintf(temp_buffer, "%lu", mem_used);
        append_str(post_data, sizeof(post_data), "mem_used", temp_buffer);
        sprintf(temp_buffer, "%lu", net_tx);
        append_str(post_data, sizeof(post_data), "net_tx", temp_buffer);
        sprintf(temp_buffer, "%lu", net_rx);
        append_str(post_data, sizeof(post_data), "net_rx", temp_buffer);
        sprintf(temp_buffer, "%ld", disk_delay);
        append_str(post_data, sizeof(post_data), "disk_delay", temp_buffer);
        sprintf(temp_buffer, "%d", tcp_connections);
        append_str(post_data, sizeof(post_data), "tcp_connections", temp_buffer);
        sprintf(temp_buffer, "%ld", cpu_delay);
        append_str(post_data, sizeof(post_data), "cpu_delay", temp_buffer);

        // 去掉最后一个多余的 '&'
        post_data[strlen(post_data) - 1] = '\0';

        // 调用 curl 发送 HTTP POST 请求
        if (send_post_request(url, post_data) != 0) {
            fprintf(stderr, "Failed to send data\n");
        }

        // 等待指定间隔
        sleep(interval);
    }

    return 0;
}