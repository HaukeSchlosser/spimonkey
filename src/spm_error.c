#include "spm_error.h"

spm_ecode_t spm_map_errno(void) 
{
    switch (errno) {
        case 0:           return SPM_OK;
        case EINVAL:      return SPM_ECONFIG;
        case ENOTDIR:     return SPM_ECONFIG;
        case EISDIR:      return SPM_ECONFIG;
        case ENOSYS:      return SPM_ENOTSUP;
        case ENOTTY:      return SPM_ENOTSUP;
        case EOPNOTSUPP:  return SPM_ENOTSUP;
        case ENODEV:      return SPM_ENODEV;
        case ENXIO:       return SPM_ENODEV;
        case ETIMEDOUT:   return SPM_ETIMEOUT;
        case EAGAIN:      return SPM_EAGAIN;
        case EINTR:       return SPM_EAGAIN;
        case EBUSY:       return SPM_EAGAIN;
        case EIO:         return SPM_EIO;
        case EFAULT:      return SPM_EIO;
        case ENOMEM:      return SPM_ENOMEM;
        case EACCES:      return SPM_ESTATE;
        case EPERM:       return SPM_ESTATE;
        case EBADF:       return SPM_ESTATE;
        case EPROTO:      return SPM_EBUS;
        default:          return SPM_EBUS;
    }
}

void spm_set_error(spm_error_t* err, spm_ecode_t code, int sys_errno,
                const char* file, const char* func, int line) 
{
    if (!err) return;
    err->code = code;
    err->sys_errno = sys_errno;
    err->file = file;
    err->func = func;
    err->line = line;
}

void spm_fail(spm_error_t *err, spm_ecode_t code)
{
    int e = errno;
    spm_set_error(err, code, e, __FILE__, __func__, __LINE__);
}


