#ifndef _IVA_RAMDUMP_H_
#define _IVA_RAMDUMP_H_

#include "iva_ctrl.h"

static inline void	iva_ram_dump_copy_vcm_dbg(struct iva_dev_data *iva,
			void *dst_buf, uint32_t dst_buf_sz) {}
static inline  int32_t	iva_ram_dump_copy_mcu_sram(struct iva_dev_data *iva,
			void *dst_buf, uint32_t dst_buf_sz) { return 0; }
static inline int32_t	iva_ram_dump_copy_iva_sfrs(struct iva_proc *proc, int shared_fd) { return 0; }

static inline int32_t	iva_ram_dump_init(struct iva_dev_data *iva) { return 0; }
static inline int32_t	iva_ram_dump_deinit(struct iva_dev_data *iva) { return 0; }

#endif /* _IVA_RAMDUMP_H_ */
