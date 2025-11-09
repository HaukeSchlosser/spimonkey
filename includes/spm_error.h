#ifndef SPMERROR_H
#define SPMERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>

/*
 * See README.md for further information on 
 * the provided error codes.
 */
typedef enum {
    SPM_OK          =  0,
    SPM_EPARAM      = -1,   
    SPM_ENOTSUP     = -2,
    SPM_ENODEV      = -3,
    SPM_EBUS        = -4,
    SPM_ETIMEOUT    = -5,
    SPM_EIO         = -6,
    SPM_ESTATE      = -7,
    SPM_ECONFIG     = -8,
    SPM_ENOMEM      = -9,
    SPM_ECRC        = -10,
    SPM_EAGAIN      = -11
} spm_ecode_t;

typedef struct {
    int         code;
    int         sys_errno;
    const char* file;
    const char* func;
    int         line;
} spm_error_t;

void spm_set_error(
    spm_error_t* err, 
    spm_ecode_t code, 
    int sys_errno,
    const char* file, 
    const char* func, int line
);

void spm_fail(
    spm_error_t *err, 
    spm_ecode_t code
);

spm_ecode_t spm_map_errno(
    void
);

#define SPM_ERROR(errptr, code) \
    spm_set_error((errptr), (code), errno, __FILE__, __func__, __LINE__)
    
#ifdef __cplusplus
} /* extern "C" */

#endif
#endif /* SPMERROR_H */