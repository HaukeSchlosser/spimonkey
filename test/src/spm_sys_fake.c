#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <linux/spi/spidev.h>

#include "spm_sys_fake.h"

/* ---- Defaults/FDs ---- */
enum { FAKE_FD = 1, INVALID_FD = -1 };
enum { DEFAULT_MODE = 0, DEFAULT_BPW = 8, DEFAULT_MAX_HZ = 1000000U };

/* ---- Fake State ---- */
typedef struct {
    int       fd;
    uint32_t  mode;
    uint8_t   bits_per_word;
    uint32_t  max_hz;
    struct {
        bool open;              /* fail open() */
        bool next_rd;           /* next RD ioctl fails */
        bool next_wr;           /* next WR ioctl fails */
        bool repeat;            /* all ioctls fail */
    } inject;
    struct {
        uint64_t total, rd, wr, msg, fail;
    } stats;
    bool inited;
} SpiDevice;

static SpiDevice g;

/* ====================================================== */
/* ================ Helpers/Assertions ================== */
/* ====================================================== */

static void init_once(void) {
    if (!g.inited) {
        memset(&g, 0, sizeof g);
        g.fd = INVALID_FD;
        g.mode = DEFAULT_MODE;
        g.bits_per_word = DEFAULT_BPW;
        g.max_hz = DEFAULT_MAX_HZ;
        g.inited = true;
    }
}

static void count_cat(unsigned long req, int *cat_rd, int *cat_wr, int *cat_msg) {
    *cat_rd = *cat_wr = *cat_msg = 0;
    switch (req) {
        case SPI_IOC_RD_MODE32:
        case SPI_IOC_RD_MODE:
        case SPI_IOC_RD_BITS_PER_WORD:
        case SPI_IOC_RD_MAX_SPEED_HZ:
            *cat_rd = 1; break;
        case SPI_IOC_WR_MODE32:
        case SPI_IOC_WR_MODE:
        case SPI_IOC_WR_BITS_PER_WORD:
        case SPI_IOC_WR_MAX_SPEED_HZ:
            *cat_wr = 1; break;
        case SPI_IOC_MESSAGE(1):
            *cat_msg = 1; break;
        default:
            break;
    }
}

static int should_fail_ioctl(int cat_rd, int cat_wr) {
    if (g.inject.repeat) return 1;
    if (cat_rd && g.inject.next_rd) { g.inject.next_rd = false; return 1; }
    if (cat_wr && g.inject.next_wr) { g.inject.next_wr = false; return 1; }
    return 0;
}

static inline int is_spi_ioc_message(unsigned long req) {
    return _IOC_TYPE(req) == SPI_IOC_MAGIC &&
           _IOC_NR(req)   == 0 &&
           (_IOC_DIR(req) & _IOC_WRITE);
}

/* ====================================================== */
/* =================== Public Functions ================= */
/* ====================================================== */

spm_sys_fake_ioctl_stats spm_sys_fake_get_ioctl_stats(void) {
    init_once();
    spm_sys_fake_ioctl_stats s = {
        .total = g.stats.total,
        .rd    = g.stats.rd,
        .wr    = g.stats.wr,
        .msg   = g.stats.msg,
        .fail  = g.stats.fail,
    };
    return s;
}

void spm_sys_fake_reset_ioctl_stats(void) {
    init_once();
    memset(&g.stats, 0, sizeof g.stats);
}

void spm_sys_fake_reset(void) {
    memset(&g, 0, sizeof g);
    g.fd = INVALID_FD;
    g.mode = DEFAULT_MODE;
    g.bits_per_word = DEFAULT_BPW;
    g.max_hz = DEFAULT_MAX_HZ;
    g.inited = true;
}

void spm_sys_fake_set_defaults(uint32_t mode, uint8_t bpw, uint32_t max_hz) {
    init_once();
    g.mode = mode;
    g.bits_per_word = bpw;
    g.max_hz = max_hz;
}

/* Fail toggles */
void spm_sys_fake_fail_open(void)                 { init_once(); g.inject.open = true; }
void spm_sys_fake_fail_ioctl(void)                { init_once(); g.inject.repeat = true; }

/* ====================================================== */
/* ================== Private Functions ================= */
/* ====================================================== */

static int f_open_(const char* path, int flags) {
    (void)path; (void)flags;
    init_once();

    if (g.inject.open) { errno = EACCES; return -1; }
    if (g.fd != INVALID_FD) { errno = EBUSY; return -1; }

    g.fd = FAKE_FD;
    return g.fd;
}

static int f_close_(int fd) {
    init_once();
    if (fd != g.fd || g.fd == INVALID_FD) { errno = EBADF; return -1; }
    g.fd = INVALID_FD;
    return 0;
}

static int f_ioctl_(int fd, unsigned long req, void *arg)
{
    init_once();
    int cat_rd, cat_wr, cat_msg;
    count_cat(req, &cat_rd, &cat_wr, &cat_msg);

    g.stats.total++;
    if (cat_rd)  g.stats.rd++;
    if (cat_wr)  g.stats.wr++;
    if (cat_msg) g.stats.msg++;

    if (fd != g.fd || g.fd == INVALID_FD) {
        errno = EBADF; g.stats.fail++; return -1;
    }
    if (should_fail_ioctl(cat_rd, cat_wr)) {
        errno = EIO; g.stats.fail++; return -1;
    }

    if (is_spi_ioc_message(req)) {
        size_t sz = _IOC_SIZE(req);
        if (sz % sizeof(struct spi_ioc_transfer) != 0) {
            errno = EINVAL; g.stats.fail++; return -1;
        }
        size_t n = sz / sizeof(struct spi_ioc_transfer);
        g.stats.msg += (n > 0 ? (n - 1) : 0);
        (void)arg;
        return 0;
    }

    switch (req) {
        case SPI_IOC_RD_MODE32:        *(uint32_t*)arg = g.mode;          return 0;
        case SPI_IOC_RD_MODE:          *(uint8_t*) arg = (uint8_t)g.mode; return 0;
        case SPI_IOC_RD_BITS_PER_WORD: *(uint8_t*) arg = g.bits_per_word; return 0;
        case SPI_IOC_RD_MAX_SPEED_HZ:  *(uint32_t*)arg = g.max_hz;        return 0;

        case SPI_IOC_WR_MODE32:        g.mode = *(uint32_t*)arg;          return 0;
        case SPI_IOC_WR_MODE:          g.mode = (uint32_t)*(uint8_t*)arg; return 0;
        case SPI_IOC_WR_BITS_PER_WORD: g.bits_per_word = *(uint8_t*)arg;  return 0;
        case SPI_IOC_WR_MAX_SPEED_HZ:  g.max_hz = *(uint32_t*)arg;        return 0;

        default:
            errno = EINVAL; g.stats.fail++; return -1;
    }
}

const spm_sys_ops_t SPM_SYS_F_DEFAULT = {
    .open_  = f_open_,
    .close_ = f_close_,
    .ioctl_ = f_ioctl_,
};