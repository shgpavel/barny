#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#define UPDATE_INTERVAL 2
#define OUTPUT_PATH     "/opt/barny/modules/cpu_freq"
#define OUTPUT_TMP_PATH "/opt/barny/modules/cpu_freq.tmp"
#define CONFIG_PATH     "/etc/barny/barny.conf"
#define MAX_CPUS        256

static volatile int running = 1;

struct cpu_info {
	int id;
	int is_p_core;
};

static struct cpu_info cpus[MAX_CPUS];
static int             cpu_count    = 0;
static int             p_core_count = 0;
static int             e_core_count = 0;

static int             cfg_p_cores  = 0;
static int             cfg_e_cores  = 0;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static char *
trim(char *str)
{
	char *end;

	while (isspace((unsigned char)*str))
		str++;
	if (*str == 0)
		return str;

	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end))
		end--;
	end[1] = '\0';

	return str;
}

static void
read_config(void)
{
	char        user_path[512];
	const char *home;
	FILE       *f;
	char        line[256];
	char       *trimmed;
	char       *eq;
	char       *key;
	char       *value;

	home = getenv("HOME");
	f    = NULL;

	if (home) {
		snprintf(user_path, sizeof(user_path),
		         "%s/.config/barny/barny.conf", home);
		f = fopen(user_path, "r");
	}

	if (!f)
		f = fopen(CONFIG_PATH, "r");

	if (!f)
		return;

	while (fgets(line, sizeof(line), f)) {
		trimmed = trim(line);

		if (*trimmed == '#' || *trimmed == '\0')
			continue;

		eq = strchr(trimmed, '=');
		if (!eq)
			continue;

		*eq   = '\0';
		key   = trim(trimmed);
		value = trim(eq + 1);

		if (strcmp(key, "sysinfo_p_cores") == 0) {
			cfg_p_cores = atoi(value);
			if (cfg_p_cores < 0)
				cfg_p_cores = 0;
		} else if (strcmp(key, "sysinfo_e_cores") == 0) {
			cfg_e_cores = atoi(value);
			if (cfg_e_cores < 0)
				cfg_e_cores = 0;
		}
	}

	fclose(f);
}

static int
read_int_file(const char *path, int *out)
{
	FILE *f;
	int   val;
	int   rc;

	f = fopen(path, "r");
	if (!f)
		return -1;

	rc = (fscanf(f, "%d", &val) == 1) ? 0 : -1;

	fclose(f);

	if (rc == 0 && out)
		*out = val;
	return rc;
}

static void
detect_cpus(void)
{
	DIR           *dir;
	int            max_freqs[MAX_CPUS];
	int            cpu_ids[MAX_CPUS];
	int            highest_freq;
	struct dirent *entry;
	int            cpu_id;
	char           path[256];
	int            max_freq;
	int            i;
	int            j;
	int            tmp_id;
	int            tmp_freq;
	int            configured_p;
	int            configured_e;
	int            lowest_freq;
	int            gap;
	int            threshold;

	dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return;

	highest_freq = 0;

	while ((entry = readdir(dir)) != NULL && cpu_count < MAX_CPUS) {
		if (strncmp(entry->d_name, "cpu", 3) != 0)
			continue;
		if (!isdigit(entry->d_name[3]))
			continue;

		cpu_id = atoi(entry->d_name + 3);

		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",
		         cpu_id);

		if (access(path, R_OK) != 0)
			continue;

		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
		         cpu_id);
		max_freq = -1;
		if (read_int_file(path, &max_freq) != 0)
			max_freq = -1;

		cpu_ids[cpu_count]   = cpu_id;
		max_freqs[cpu_count] = max_freq;

		if (max_freq > highest_freq)
			highest_freq = max_freq;

		cpu_count++;
	}

	closedir(dir);

	for (i = 0; i < cpu_count - 1; i++) {
		for (j = i + 1; j < cpu_count; j++) {
			if (cpu_ids[j] < cpu_ids[i]) {
				tmp_id       = cpu_ids[i];
				cpu_ids[i]   = cpu_ids[j];
				cpu_ids[j]   = tmp_id;

				tmp_freq     = max_freqs[i];
				max_freqs[i] = max_freqs[j];
				max_freqs[j] = tmp_freq;
			}
		}
	}

	for (i = 0; i < cpu_count; i++) {
		cpus[i].id = cpu_ids[i];
	}

	if (cfg_p_cores > 0 || cfg_e_cores > 0) {
		configured_p = (cfg_p_cores > 0) ? cfg_p_cores : 0;
		configured_e = (cfg_e_cores > 0) ? cfg_e_cores : 0;

		if (configured_p + configured_e > cpu_count) {
			fprintf(stderr,
			        "Warning: configured P+E cores (%d+%d) exceeds detected CPUs (%d)\n",
			        configured_p, configured_e, cpu_count);

			configured_p = 0;
			configured_e = 0;
		}

		if (configured_p > 0 || configured_e > 0) {
			for (i = 0; i < cpu_count; i++) {
				cpus[i].is_p_core = (i < configured_p) ? 1 : 0;
				if (cpus[i].is_p_core)
					p_core_count++;
				else
					e_core_count++;
			}
			return;
		}
	}

	lowest_freq = highest_freq;
	for (i = 0; i < cpu_count; i++) {
		if (max_freqs[i] < lowest_freq)
			lowest_freq = max_freqs[i];
	}

	gap = highest_freq - lowest_freq;

	if (gap > 100000) {
		threshold = lowest_freq + 100000;
	} else {
		threshold = 0;
	}

	for (i = 0; i < cpu_count; i++) {
		cpus[i].is_p_core = (max_freqs[i] >= threshold) ? 1 : 0;

		if (cpus[i].is_p_core)
			p_core_count++;
		else
			e_core_count++;
	}
}

static double
read_cpu_freq(int cpu_id)
{
	char path[256];
	int  freq_khz;

	snprintf(path, sizeof(path),
	         "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu_id);

	freq_khz = 0;
	if (read_int_file(path, &freq_khz) != 0)
		return 0.0;

	return freq_khz / 1000000.0;
}

static void
write_output(double p_avg, double e_avg)
{
	FILE  *f;
	double avg;

	f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		return;
	}

	if (e_core_count > 0 && p_core_count > 0) {
		fprintf(f, "P: %.2f E: %.2f\n", p_avg, e_avg);
	} else {
		avg = (p_core_count > 0) ? p_avg : e_avg;
		fprintf(f, "%.2f\n", avg);
	}

	fclose(f);

	if (rename(OUTPUT_TMP_PATH, OUTPUT_PATH) != 0) {
		fprintf(stderr, "Failed to rename output file\n");
	}
}

int
main(void)
{
	double p_sum;
	double e_sum;
	double freq;
	double p_avg;
	double e_avg;
	int    i;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	read_config();
	detect_cpus();

	if (cpu_count == 0) {
		fprintf(stderr, "No CPUs with frequency scaling found\n");
		return 1;
	}

	fprintf(stderr, "Detected %d CPUs (%d P-cores, %d E-cores)\n", cpu_count,
	        p_core_count, e_core_count);

	while (running) {
		p_sum = 0.0;
		e_sum = 0.0;

		for (i = 0; i < cpu_count; i++) {
			freq = read_cpu_freq(cpus[i].id);
			if (cpus[i].is_p_core)
				p_sum += freq;
			else
				e_sum += freq;
		}

		p_avg = (p_core_count > 0) ? p_sum / p_core_count : 0.0;
		e_avg = (e_core_count > 0) ? e_sum / e_core_count : 0.0;

		write_output(p_avg, e_avg);

		sleep(UPDATE_INTERVAL);
	}

	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
