#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "spm_sys.h"

static int spm_open_(const char* p, int f) 
{ 
    return open(p, f); 
}

static int spm_close_(int fd)
{ 
    return close(fd); 
}

static int spm_ioctl_(int fd, unsigned long r, void *a) 
{ 
    return ioctl(fd, r, a); 
}

// Definition of in header declared variable
const spm_sys_ops_t SPM_SYS_DEFAULT = {
    .open_  = spm_open_,
    .close_ = spm_close_,
    .ioctl_ = spm_ioctl_,
};