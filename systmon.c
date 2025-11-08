#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <errno.h>

#define REFRESH_INTERVAL 1   
#define TOP_N 12             
#define MAX_LINE 4096
#define INIT_PARR 1024

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} cpu_snapshot_t;

typedef struct {
    pid_t pid;
    unsigned long proc_time; 
    unsigned long rss_pages; 
    char comm[256];
    double cpu_percent;
    unsigned long mem_kb;
} proc_info_t;

typedef struct {
    pid_t pid;
    unsigned long prev_time;
} prev_proc_t;

static prev_proc_t *prev_procs = NULL;
static size_t prev_procs_len = 0;
static size_t prev_procs_cap = 0;

void ensure_prev_cap(size_t need) {
    if (prev_procs_cap >= need) 
	    return;
    size_t newcap = prev_procs_cap ? prev_procs_cap * 2 : 256;
    while (newcap < need) newcap *= 2;
    prev_procs = realloc(prev_procs, newcap * sizeof(prev_proc_t));
    prev_procs_cap = newcap;
}

unsigned long get_pagesize_kb() {
    return sysconf(_SC_PAGESIZE) / 1024;
}

int read_cpu_snapshot(cpu_snapshot_t *snap) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) 
	    return -1;
    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), f)) {
	    fclose(f);
	    return -1; 
    }
   
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    int cnt = sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    if (cnt < 4){ 
	    fclose(f); 
	    return -1; 
    }
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    snap->total = total;
    snap->idle = idle + iowait;
    fclose(f);
    return 0;
}

int read_meminfo(unsigned long *total_kb, unsigned long *avail_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) 
	    return -1;
    
    char line[MAX_LINE];
    *total_kb = *avail_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lu kB", total_kb) == 1) 
		continue;
        if (sscanf(line, "MemAvailable: %lu kB", avail_kb) == 1) 
		continue;
    }
    fclose(f);
    if (*total_kb == 0) 
	    return -1;
    if (*avail_kb == 0) {
        return -1;
    }
    return 0;
}

unsigned long read_proc_time(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char buf[MAX_LINE];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Find the closing parenthesis of the command name
    char *rparen = strrchr(buf, ')');
    if (!rparen) return 0;

    unsigned long utime = 0, stime = 0;
    char *p = rparen + 2; // skip ") "
    // Skip fields 3 through 13 (11 fields) before utime (14) and stime (15)
    for (int i = 0; i < 11; i++) {
        p = strchr(p, ' ');
        if (!p) return 0;
        p++;
    }

    // Now read utime and stime
    if (sscanf(p, "%lu %lu", &utime, &stime) != 2)
        return 0;

    return utime + stime;
}


unsigned long read_proc_rss_pages(pid_t pid) {
    char path[256], buf[MAX_LINE];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    FILE *f = fopen(path, "r");
    if (!f) 
	    return 0;
    unsigned long size = 0, resident = 0;
    if (fscanf(f, "%lu %lu", &size, &resident) != 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return resident;
}

void update_prev_proc(pid_t pid, unsigned long proc_time) {
    for (size_t i = 0; i < prev_procs_len; ++i) {
        if (prev_procs[i].pid == pid) {
            prev_procs[i].prev_time = proc_time;
            return;
        }
    }

    ensure_prev_cap(prev_procs_len + 1);
    prev_procs[prev_procs_len].pid = pid;
    prev_procs[prev_procs_len].prev_time = proc_time;
    prev_procs_len++;
}

unsigned long get_prev_proc_time(pid_t pid) {
    for (size_t i = 0; i < prev_procs_len; ++i) {
        if (prev_procs[i].pid == pid) 
		return prev_procs[i].prev_time;
    }
    return 0;
}

int is_numeric(const char *s) {
    if (!s || !*s) 
	    return 0;
    for (; *s; ++s) 
	    if (!isdigit((unsigned char)*s)) 
		    return 0;
    return 1;
}

int cmp_proc_cpu(const void *a, const void *b) {
    const proc_info_t *pa = a;
    const proc_info_t *pb = b;
    if (pa->cpu_percent < pb->cpu_percent) 
	    return 1;
    if (pa->cpu_percent > pb->cpu_percent) 
	    return -1;
    return (pa->mem_kb < pb->mem_kb) ? 1 : -1;
}

void clear_screen() {
    printf("\033[2J\033[H");
}

int main(void) {
    cpu_snapshot_t prev_cpu = {0}, cur_cpu = {0};
    if (read_cpu_snapshot(&prev_cpu) != 0) {
        fprintf(stderr, "Failed to read /proc/stat\n");
        return 1;
    }

    unsigned long clk_tck = sysconf(_SC_CLK_TCK);
    unsigned long page_kb = get_pagesize_kb();

    while (1) {
        sleep(REFRESH_INTERVAL);

        if (read_cpu_snapshot(&cur_cpu) != 0) {
            fprintf(stderr, "Failed to read /proc/stat\n");
            break;
        }

        unsigned long long total_delta = cur_cpu.total - prev_cpu.total;
        unsigned long long idle_delta  = cur_cpu.idle  - prev_cpu.idle;
        double cpu_usage = 0.0;
        if (total_delta > 0) cpu_usage = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;

        unsigned long mem_total_kb=0, mem_avail_kb=0;
        if (read_meminfo(&mem_total_kb, &mem_avail_kb) != 0) {
            struct sysinfo si;
            if (sysinfo(&si) == 0) {
                mem_total_kb = si.totalram * si.mem_unit / 1024;
                mem_avail_kb = si.freeram * si.mem_unit / 1024;
            }
        }
        unsigned long mem_used_kb = (mem_total_kb > mem_avail_kb) ? (mem_total_kb - mem_avail_kb) : 0;
        double mem_usage_pct = mem_total_kb ? (100.0 * mem_used_kb / mem_total_kb) : 0.0;

        DIR *d = opendir("/proc");
        if (!d) { 
		perror("opendir /proc"); 
		break; 
	}
        struct dirent *de;
        proc_info_t *plist = malloc(INIT_PARR * sizeof(proc_info_t));
        size_t plist_len = 0, plist_cap = INIT_PARR;

        while ((de = readdir(d)) != NULL) {
            if (!is_numeric(de->d_name)) 
		    continue;
            pid_t pid = (pid_t)atoi(de->d_name);
            unsigned long cur_proc_time = read_proc_time(pid);
            if (cur_proc_time == 0) 
		    continue;
            unsigned long prev_time = get_prev_proc_time(pid);
            unsigned long delta_proc = 0;
            if (cur_proc_time >= prev_time) 
		    delta_proc = cur_proc_time - prev_time;
            double proc_cpu_pct = 0.0;
            if (total_delta > 0) {
                proc_cpu_pct = 100.0 * (double)delta_proc / (double)total_delta;
            }
            unsigned long rss_pages = read_proc_rss_pages(pid);
            unsigned long mem_kb = rss_pages * page_kb;

            char commpath[256], buf[MAX_LINE];
            snprintf(commpath, sizeof(commpath), "/proc/%d/comm", pid);
            FILE *cf = fopen(commpath, "r");
            char commname[256] = "?";
            if (cf) {
                if (fgets(buf, sizeof(buf), cf)) {
                    buf[strcspn(buf, "\n")] = 0;
                    strncpy(commname, buf, sizeof(commname)-1);
                    commname[sizeof(commname)-1] = 0;
                }
                fclose(cf);
            } else {
		    strncpy(commname, "?", sizeof(commname)-1);
            }

            if (plist_len >= plist_cap) {
                plist_cap *= 2;
                plist = realloc(plist, plist_cap * sizeof(proc_info_t));
            }
            plist[plist_len].pid = pid;
            plist[plist_len].proc_time = cur_proc_time;
            plist[plist_len].rss_pages = rss_pages;
            strncpy(plist[plist_len].comm, commname, sizeof(plist[plist_len].comm)-1);
            plist[plist_len].comm[sizeof(plist[plist_len].comm)-1] = 0;
            plist[plist_len].cpu_percent = proc_cpu_pct;
            plist[plist_len].mem_kb = mem_kb;
            plist_len++;

        }
        closedir(d);

        qsort(plist, plist_len, sizeof(proc_info_t), cmp_proc_cpu);

        for (size_t i = 0; i < plist_len; ++i) {
            update_prev_proc(plist[i].pid, plist[i].proc_time);
        }

        clear_screen();
        printf("Simple SysMon (reads /proc) — refresh every %d s\n", REFRESH_INTERVAL);
        printf("CPU Usage: %.2f%%   (Total jiffies delta: %llu)\n", cpu_usage, (unsigned long long)(cur_cpu.total - prev_cpu.total));
        printf("Memory: %lu kB total, %lu kB used (%.2f%%)\n", mem_total_kb, mem_used_kb, mem_usage_pct);
        printf("\n  PID     CPU%%    MEM(kB)   COMMAND\n");
        printf("-------------------------------------------------\n");

        size_t show = plist_len < TOP_N ? plist_len : TOP_N;
        for (size_t i = 0; i < show; ++i) {
            printf("%6d  %6.2f   %8lu   %s\n",
                   plist[i].pid, plist[i].cpu_percent, plist[i].mem_kb, plist[i].comm);
        }
        if (plist_len == 0) printf("No processes found.\n");

        free(plist);
        prev_cpu = cur_cpu;
        fflush(stdout);
    }

    return 0;
}

