#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#define UPDATE_INTERVAL 2
#define OUTPUT_PATH "/opt/barny/modules/cpu_freq"
#define OUTPUT_TMP_PATH "/opt/barny/modules/cpu_freq.tmp"
#define CONFIG_PATH "/etc/barny/barny.conf"
#define CONFIG_PATH_USER "/.config/barny/barny.conf"
#define MAX_CPUS 256

static volatile int running = 1;

struct cpu_info {
	int id;
	int is_p_core; /* 1 = P-core (performance), 0 = E-core (efficiency) */
};

static struct cpu_info cpus[MAX_CPUS];
static int cpu_count = 0;
static int p_core_count = 0;
static int e_core_count = 0;

/* Config values */
static int cfg_p_cores = 0;      /* 0 = auto-detect */
static int cfg_e_cores = 0;      /* 0 = auto-detect */
static int cfg_freq_decimals = 2;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static char *
trim(char *str)
{
	while (isspace(*str))
		str++;
	if (*str == 0)
		return str;

	char *end = str + strlen(str) - 1;
	while (end > str && isspace(*end))
		end--;
	end[1] = '\0';

	return str;
}

static void
read_config(void)
{
	/* Try user config first, then system config */
	char user_path[512];
	const char *home = getenv("HOME");
	FILE *f = NULL;

	if (home) {
		snprintf(user_path, sizeof(user_path), "%s/.config/barny/barny.conf", home);
		f = fopen(user_path, "r");
	}

	if (!f)
		f = fopen(CONFIG_PATH, "r");

	if (!f)
		return;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *trimmed = trim(line);

		if (*trimmed == '#' || *trimmed == '\0')
			continue;

		char *eq = strchr(trimmed, '=');
		if (!eq)
			continue;

		*eq = '\0';
		char *key = trim(trimmed);
		char *value = trim(eq + 1);

		if (strcmp(key, "sysinfo_p_cores") == 0) {
			cfg_p_cores = atoi(value);
			if (cfg_p_cores < 0) cfg_p_cores = 0;
		} else if (strcmp(key, "sysinfo_e_cores") == 0) {
			cfg_e_cores = atoi(value);
			if (cfg_e_cores < 0) cfg_e_cores = 0;
		} else if (strcmp(key, "sysinfo_freq_decimals") == 0) {
			cfg_freq_decimals = atoi(value);
			if (cfg_freq_decimals < 0) cfg_freq_decimals = 0;
			if (cfg_freq_decimals > 2) cfg_freq_decimals = 2;
		}
	}

	fclose(f);
}

static int
read_int_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	int val;
	if (fscanf(f, "%d", &val) != 1)
		val = -1;

	fclose(f);
	return val;
}

static void
detect_cpus(void)
{
	DIR *dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return;

	int max_freqs[MAX_CPUS];
	int cpu_ids[MAX_CPUS];
	int highest_freq = 0;

	/* First pass: collect all CPUs and their max frequencies */
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && cpu_count < MAX_CPUS) {
		/* Look for cpu0, cpu1, etc. */
		if (strncmp(entry->d_name, "cpu", 3) != 0)
			continue;
		if (!isdigit(entry->d_name[3]))
			continue;

		int cpu_id = atoi(entry->d_name + 3);

		/* Check if this CPU has frequency scaling */
		char path[256];
		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu_id);

		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		fclose(f);

		/* Read max frequency */
		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu_id);
		int max_freq = read_int_file(path);

		cpu_ids[cpu_count] = cpu_id;
		max_freqs[cpu_count] = max_freq;

		if (max_freq > highest_freq)
			highest_freq = max_freq;

		cpu_count++;
	}

	closedir(dir);

	/* Sort CPUs by ID (readdir order is not guaranteed) */
	for (int i = 0; i < cpu_count - 1; i++) {
		for (int j = i + 1; j < cpu_count; j++) {
			if (cpu_ids[j] < cpu_ids[i]) {
				int tmp_id = cpu_ids[i];
				cpu_ids[i] = cpu_ids[j];
				cpu_ids[j] = tmp_id;

				int tmp_freq = max_freqs[i];
				max_freqs[i] = max_freqs[j];
				max_freqs[j] = tmp_freq;
			}
		}
	}

	/* Copy sorted IDs to cpus array */
	for (int i = 0; i < cpu_count; i++) {
		cpus[i].id = cpu_ids[i];
	}

	/* Check if manual P/E core counts are configured */
	if (cfg_p_cores > 0 || cfg_e_cores > 0) {
		/* Use configured counts - P-cores are first N cores by ID */
		int configured_p = (cfg_p_cores > 0) ? cfg_p_cores : 0;
		int configured_e = (cfg_e_cores > 0) ? cfg_e_cores : 0;

		/* Validate: don't exceed actual CPU count */
		if (configured_p + configured_e > cpu_count) {
			fprintf(stderr, "Warning: configured P+E cores (%d+%d) exceeds detected CPUs (%d)\n",
			        configured_p, configured_e, cpu_count);
			/* Fall back to auto-detect */
			configured_p = 0;
			configured_e = 0;
		}

		if (configured_p > 0 || configured_e > 0) {
			/* Assign by CPU ID order: first cfg_p_cores are P, rest are E */
			for (int i = 0; i < cpu_count; i++) {
				cpus[i].is_p_core = (i < configured_p) ? 1 : 0;
				if (cpus[i].is_p_core)
					p_core_count++;
				else
					e_core_count++;
			}
			return;
		}
	}

	/* Auto-detect: classify P vs E cores based on max frequency */
	int lowest_freq = highest_freq;
	for (int i = 0; i < cpu_count; i++) {
		if (max_freqs[i] < lowest_freq)
			lowest_freq = max_freqs[i];
	}

	/* If there's a meaningful gap (>100MHz), use midpoint as threshold
	 * Otherwise treat all as same type */
	int gap = highest_freq - lowest_freq;
	int threshold;

	if (gap > 100000) {
		/* Hybrid system: threshold is just above lowest freq */
		threshold = lowest_freq + 100000; /* 100 MHz above E-core max */
	} else {
		/* Homogeneous system: all cores same type */
		threshold = 0;
	}

	for (int i = 0; i < cpu_count; i++) {
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
	snprintf(path, sizeof(path),
	         "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu_id);

	int freq_khz = read_int_file(path);
	if (freq_khz < 0)
		return 0.0;

	return freq_khz / 1000000.0; /* Convert kHz to GHz */
}

static void
write_output(double p_avg, double e_avg)
{
	FILE *f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		return;
	}

	if (e_core_count > 0 && p_core_count > 0) {
		/* Hybrid CPU: show both P and E core averages */
		fprintf(f, "P: %.2f E: %.2f\n", p_avg, e_avg);
	} else {
		/* Non-hybrid: show single average */
		double avg = (p_core_count > 0) ? p_avg : e_avg;
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
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	detect_cpus();

	if (cpu_count == 0) {
		fprintf(stderr, "No CPUs with frequency scaling found\n");
		return 1;
	}

	fprintf(stderr, "Detected %d CPUs (%d P-cores, %d E-cores)\n",
	        cpu_count, p_core_count, e_core_count);

	while (running) {
		double p_sum = 0.0, e_sum = 0.0;

		for (int i = 0; i < cpu_count; i++) {
			double freq = read_cpu_freq(cpus[i].id);
			if (cpus[i].is_p_core)
				p_sum += freq;
			else
				e_sum += freq;
		}

		double p_avg = (p_core_count > 0) ? p_sum / p_core_count : 0.0;
		double e_avg = (e_core_count > 0) ? e_sum / e_core_count : 0.0;

		write_output(p_avg, e_avg);

		sleep(UPDATE_INTERVAL);
	}

	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
