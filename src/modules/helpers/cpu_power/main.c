#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <time.h>

#define UPDATE_INTERVAL  2
#define OUTPUT_PATH      "/opt/barny/modules/cpu_power"
#define OUTPUT_TMP_PATH  "/opt/barny/modules/cpu_power.tmp"
#define MAX_RAPL_DOMAINS 16

static volatile int running = 1;

struct rapl_domain {
	char            energy_path[512];
	char            name[256];
	uint64_t        max_energy;
	uint64_t        last_energy;
	struct timespec last_time;
};

static struct rapl_domain domains[MAX_RAPL_DOMAINS];
static int                domain_count = 0;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static int
read_uint64_file(const char *path, uint64_t *val)
{
	FILE *f;

	f = fopen(path, "r");
	if (!f)
		return -1;

	if (fscanf(f, "%lu", val) != 1) {
		fclose(f);
		return -1;
	}

	fclose(f);
	return 0;
}

static int
read_string_file(const char *path, char *buf, size_t size)
{
	FILE  *f;
	size_t len;

	f = fopen(path, "r");
	if (!f)
		return -1;

	if (!fgets(buf, size, f)) {
		fclose(f);
		return -1;
	}

	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	fclose(f);
	return 0;
}

static void
detect_rapl_domains(void)
{
	DIR                *dir;
	struct dirent      *entry;
	int                 colons;
	char               *p;
	char                energy_path[512];
	FILE               *f;
	struct rapl_domain *dom;
	char                name_path[512];
	char                max_path[512];
	int                 i;
	int                 psys_idx;

	dir = opendir("/sys/class/powercap");
	if (!dir) {
		fprintf(stderr, "RAPL not available (no /sys/class/powercap)\n");
		return;
	}

	while ((entry = readdir(dir)) != NULL && domain_count < MAX_RAPL_DOMAINS) {
		if (strncmp(entry->d_name, "intel-rapl:", 11) != 0
		    && strncmp(entry->d_name, "amd_rapl:", 9) != 0)
			continue;

		colons = 0;
		for (p = entry->d_name; *p; p++)
			if (*p == ':')
				colons++;
		if (colons > 1)
			continue;

		snprintf(energy_path, sizeof(energy_path),
		         "/sys/class/powercap/%s/energy_uj", entry->d_name);

		f = fopen(energy_path, "r");
		if (!f) {
			fprintf(stderr,
			        "Warning: %s not readable (try: sudo chmod o+r %s)\n",
			        energy_path, energy_path);
			continue;
		}
		fclose(f);

		dom = &domains[domain_count];
		snprintf(dom->energy_path, sizeof(dom->energy_path), "%s",
		         energy_path);

		snprintf(name_path, sizeof(name_path),
		         "/sys/class/powercap/%s/name", entry->d_name);
		if (read_string_file(name_path, dom->name, sizeof(dom->name)) != 0)
			snprintf(dom->name, sizeof(dom->name), "%s",
			         entry->d_name);

		snprintf(max_path, sizeof(max_path),
		         "/sys/class/powercap/%s/max_energy_range_uj",
		         entry->d_name);
		if (read_uint64_file(max_path, &dom->max_energy) != 0)
			dom->max_energy = UINT64_MAX;

		read_uint64_file(dom->energy_path, &dom->last_energy);
		clock_gettime(CLOCK_MONOTONIC, &dom->last_time);

		domain_count++;
		fprintf(stderr, "Found RAPL domain: %s\n", dom->name);
	}

	closedir(dir);

	psys_idx = -1;
	for (i = 0; i < domain_count; i++) {
		if (strcmp(domains[i].name, "psys") == 0) {
			psys_idx = i;
			break;
		}
	}
	if (psys_idx >= 0) {
		domains[0]   = domains[psys_idx];
		domain_count = 1;
		fprintf(stderr,
		        "Using psys (platform / total board power) domain only\n");
	}
}

static double
read_power(struct rapl_domain *dom)
{
	uint64_t        energy;
	struct timespec now;
	double          dt;
	uint64_t        de;

	if (read_uint64_file(dom->energy_path, &energy) != 0)
		return -1.0;
	clock_gettime(CLOCK_MONOTONIC, &now);

	dt = (now.tv_sec - dom->last_time.tv_sec)
	     + (now.tv_nsec - dom->last_time.tv_nsec) / 1e9;

	if (dt < 0.001)
		return -1.0;

	if (energy >= dom->last_energy) {
		de = energy - dom->last_energy;
	} else {
		de = (dom->max_energy - dom->last_energy) + energy;
	}

	dom->last_energy = energy;
	dom->last_time   = now;

	return de / (dt * 1e6);
}

static void
write_output(double total_power)
{
	FILE *f;

	f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		return;
	}

	fprintf(f, "PWR: %.1f\n", total_power);
	fclose(f);

	if (rename(OUTPUT_TMP_PATH, OUTPUT_PATH) != 0) {
		fprintf(stderr, "Failed to rename output file\n");
	}
}

int
main(void)
{
	double total_power;
	double power;
	int    i;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	detect_rapl_domains();

	if (domain_count == 0) {
		fprintf(stderr, "No readable RAPL domains found\n");
		fprintf(stderr,
		        "To fix: sudo chmod o+r /sys/class/powercap/intel-rapl:*/energy_uj\n");
		return 1;
	}

	sleep(1);

	while (running) {
		total_power = 0.0;

		for (i = 0; i < domain_count; i++) {
			power = read_power(&domains[i]);
			if (power >= 0)
				total_power += power;
		}

		write_output(total_power);

		sleep(UPDATE_INTERVAL);
	}

	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
