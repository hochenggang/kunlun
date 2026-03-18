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

typedef struct
{
    double uptime_s;
    double idle_s;
} Uptime;

typedef struct
{
    double load_1min;
    double load_5min;
    double load_15min;
    int running_tasks;
    int total_tasks;
} LoadAvg;

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

typedef struct
{
    double mem_total_mib;
    double mem_free_mib;
    double mem_used_mib;
    double mem_buff_cache_mib;
} MemInfo;

typedef struct
{
    int tcp_connections;
    int udp_connections;
    unsigned long default_interface_net_tx_bytes;
    unsigned long default_interface_net_rx_bytes;
} NetInfo;

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

typedef struct
{
    int cpu_num_cores;
    unsigned long long root_disk_total_kb;
    unsigned long long root_disk_avail_kb;
    char machine_id[33];
    char hostname[256];
} SystemInfo;

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

static FILE *safe_fopen(const char *filename, const char *mode)
{
    FILE *fp = fopen(filename, mode);
    if (!fp)
    {
        perror(filename);
    }
    return fp;
}

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

void trim_leading_whitespace(char *str)
{
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
    {
        start++;
    }
    memmove(str, start, strlen(start) + 1);
}

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
                break;
            }
        }
    }

    fclose(fp);

    return iface;
}

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
