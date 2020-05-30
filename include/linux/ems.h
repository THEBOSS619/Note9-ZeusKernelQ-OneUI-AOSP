/*
 * Copyright (c) 2017 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/plist.h>
#include <linux/sched.h>

#ifdef CONFIG_SCHED_TUNE
enum stune_group {
	STUNE_ROOT,
	STUNE_FOREGROUND,
	STUNE_BACKGROUND,
	STUNE_TOPAPP,
	STUNE_GROUP_COUNT,
};
#endif

struct gb_qos_request {
	struct plist_node node;
	char *name;
	bool active;
};

#ifdef CONFIG_SCHED_EMS
extern struct sched_group *exynos_fit_idlest_group(struct sched_domain *sd,
		struct task_struct *p);
extern void exynos_init_entity_util_avg(struct sched_entity *se);
extern int exynos_need_active_balance(enum cpu_idle_type idle,
		struct sched_domain *sd, int src_cpu, int dst_cpu);

extern int
exynos_wakeup_balance(struct task_struct *p, int prev_cpu, int sd_flag, int sync);

extern void ontime_migration(void);
extern int ontime_can_migration(struct task_struct *p, int cpu);
extern void ontime_update_load_avg(u64 delta, int cpu, unsigned long weight,
						struct sched_avg *sa);
extern void ontime_new_entity_load(struct task_struct *parent,
					struct sched_entity *se);
extern void ontime_trace_task_info(struct task_struct *p);

extern bool lbt_overutilized(int cpu, int level);
extern void update_lbt_overutil(int cpu, unsigned long capacity);

extern void gb_qos_update_request(struct gb_qos_request *req, u32 new_value);

extern void request_kernel_prefer_perf(int grp_idx, int enable);

#else
static inline struct sched_group *exynos_fit_idlest_group(struct sched_domain *sd,
		struct task_struct *p) { return NULL; }
static inline void exynos_init_entity_util_avg(struct sched_entity *se) { }
static inline int exynos_need_active_balance(enum cpu_idle_type idle,
		struct sched_domain *sd, int src_cpu, int dst_cpu) { return 0; }

static inline int
exynos_wakeup_balance(struct task_struct *p, int prev_cpu, int sd_flag, int sync)
{
	return -1;
}

static inline void ontime_migration(void) { }
static inline int ontime_can_migration(struct task_struct *p, int cpu) { return 1; }
static inline void ontime_update_load_avg(u64 delta, int cpu, unsigned long weight,
							struct sched_avg *sa) { }
static inline void ontime_new_entity_load(struct task_struct *p,
					struct sched_entity *se) { }
static inline void ontime_trace_task_info(struct task_struct *p) { }

static inline bool lbt_overutilized(int cpu, int level) { return false; }
static inline void update_lbt_overutil(int cpu, unsigned long capacity) { }

static inline void gb_qos_update_request(struct gb_qos_request *req, u32 new_value) { }

static inline void request_kernel_prefer_perf(int grp_idx, int enable) { }
#endif /* CONFIG_SCHED_EMS */

#ifdef CONFIG_SIMPLIFIED_ENERGY_MODEL
extern void init_sched_energy_table(struct cpumask *cpus, int table_size,
				unsigned long *f_table, unsigned int *v_table,
				int max_f, int min_f);
#else
static inline void init_sched_energy_table(struct cpumask *cpus, int table_size,
				unsigned long *f_table, unsigned int *v_table,
				int max_f, int min_f) { }
#endif
