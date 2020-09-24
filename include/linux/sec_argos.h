#ifndef _SEC_ARGOS_H
#define _SEC_ARGOS_H

#ifdef CONFIG_ARGOS
extern int sec_argos_register_notifier(struct notifier_block *n, char *label);
extern int sec_argos_unregister_notifier(struct notifier_block *n, char *label);
#else
static inline int sec_argos_register_notifier(struct notifier_block *n, char *label) { return 0; }
static inline int sec_argos_unregister_notifier(struct notifier_block *n, char *label) { return 0; }
#endif

#endif
