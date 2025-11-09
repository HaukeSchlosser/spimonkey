#ifndef SPMSYSFAKE_H
#define SPMSYSFAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "spm_sys.h"

typedef struct spm_sys_fake_ioctl_stats {
    uint64_t total, rd, wr, msg, fail;
} spm_sys_fake_ioctl_stats;

extern const spm_sys_ops_t SPM_SYS_F_DEFAULT;

/* State mgmt */
void spm_sys_fake_reset(void);
void spm_sys_fake_reset_ioctl_stats(void);
spm_sys_fake_ioctl_stats spm_sys_fake_get_ioctl_stats(void);
void spm_sys_fake_set_defaults(uint32_t mode, uint8_t bpw, uint32_t max_hz);

/* Fail injection */
void spm_sys_fake_fail_open(void);                  /* compatibility */
void spm_sys_fake_set_fail_open(bool v);            /* toggle */
void spm_sys_fake_fail_ioctl(void);                 /* compatibility: sticky fail all ioctls */

#ifdef __cplusplus
}
#endif
#endif /* SPMSYSFAKE_H */