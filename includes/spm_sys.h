#ifndef SPMSYS_H
#define SPMSYS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spm_sys_ops {
    int  (*open_)(const char *path, int flags);
    int  (*close_)(int fd);
    int  (*ioctl_)(int fd, unsigned long req, void *arg);
} spm_sys_ops_t;

extern const spm_sys_ops_t SPM_SYS_DEFAULT;

#ifdef __cplusplus
} /* extern "C" */

#endif
#endif /* SPMSYS_H */
