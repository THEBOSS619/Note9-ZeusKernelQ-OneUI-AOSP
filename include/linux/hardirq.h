#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/context_tracking_state.h>
#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/ftrace_irq.h>
#include <linux/vtime.h>
#include <asm/hardirq.h>

extern void synchronize_irq(unsigned int irq);
extern bool synchronize_hardirq(unsigned int irq);

#ifdef CONFIG_NO_HZ_FULL
void __rcu_irq_enter_check_tick(void);
#else
static inline void __rcu_irq_enter_check_tick(void) { }
#endif

static __always_inline void rcu_irq_enter_check_tick(void)
{
	if (context_tracking_enabled())
		__rcu_irq_enter_check_tick();
}

/*
 * It is safe to do non-atomic ops on ->hardirq_context,
 * because NMI handlers may not preempt and the ops are
 * always balanced, so the interrupted value of ->hardirq_context
 * will always be restored.
 */
#define __irq_enter()					\
	do {						\
		account_irq_enter_time(current);	\
		preempt_count_add(HARDIRQ_OFFSET);	\
		trace_hardirq_enter();			\
	} while (0)

/*
 * Enter irq context (on NO_HZ, update jiffies):
 */
extern void irq_enter(void);

/*
 * Exit irq context without processing softirqs:
 */
#define __irq_exit()					\
	do {						\
		trace_hardirq_exit();			\
		account_irq_exit_time(current);		\
		preempt_count_sub(HARDIRQ_OFFSET);	\
	} while (0)

/*
 * Exit irq context and process softirqs if needed:
 */
extern void irq_exit(void);

#ifdef CONFIG_TINY_RCU
static inline void rcu_nmi_enter(void) { }
static inline void rcu_nmi_exit(void) { }
#else
extern void rcu_nmi_enter(void);
extern void rcu_nmi_exit(void);
#endif

/*
 * NMI vs Tracing
 * --------------
 *
 * We must not land in a tracer until (or after) we've changed preempt_count
 * such that in_nmi() becomes true. To that effect all NMI C entry points must
 * be marked 'notrace' and call nmi_enter() as soon as possible.
 */

/*
 * nmi_enter() can nest up to 15 times; see NMI_BITS.
 */
#define nmi_enter()						\
	do {							\
		printk_nmi_enter();				\
		lockdep_off();					\
		ftrace_nmi_enter();				\
		BUG_ON(in_nmi());				\
		preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET);	\
		rcu_nmi_enter();				\
		trace_hardirq_enter();				\
	} while (0)

#define nmi_exit()						\
	do {							\
		trace_hardirq_exit();				\
		rcu_nmi_exit();					\
		BUG_ON(!in_nmi());				\
		preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET);	\
		ftrace_nmi_exit();				\
		lockdep_on();					\
		printk_nmi_exit();				\
	} while (0)

#endif /* LINUX_HARDIRQ_H */
