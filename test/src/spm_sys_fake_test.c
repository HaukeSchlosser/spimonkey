#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <linux/spi/spidev.h>

#include "spm_sys_fake.h"

#define TEST_COL 60

/* ====================================================== */
/* ================ Helpers/Assertions ================== */
/* ====================================================== */

static const char* basename_c(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void test_print_status_(const char* file, const char* func, const char* status)
{
    char label[256];
    snprintf(label, sizeof label, "%s:%s", basename_c(file), func);

    int pad = TEST_COL - (int)strlen(label);
    if (pad < 1) pad = 1;

    printf("%s%*s%s\n", label, pad, "", status);
}

#define TEST_PASS() test_print_status_(__FILE__, __func__, "PASSED")

static void expect_stats(uint64_t total, uint64_t rd, uint64_t wr, uint64_t msg, uint64_t fail)
{
    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == total);
    assert(s.rd    == rd);
    assert(s.wr    == wr);
    assert(s.msg   == msg);
    assert(s.fail  == fail);
}

/* ====================================================== */
/* ======================== Open ======================== */
/* ====================================================== */

static int open_dev_returns_valid_val(const spm_sys_ops_t *ops) 
{
    spm_sys_fake_reset();
    assert(ops && ops->open_);
    errno = 0;

    int fd = ops->open_("/dev/spidev0.0", 0);
    assert(fd >= 0);
    assert(errno == 0);

    TEST_PASS();
    return fd;
}

static void open_2_devs_returns_invalid_val(const spm_sys_ops_t *ops)
{
    assert(ops && ops->open_);
    errno = 0;

    int fd = ops->open_("/dev/spidev0.0", 0);
    assert(fd == -1);
    assert(errno == EBUSY);

    TEST_PASS();
}

static void open_with_fail_flag_returns_eacces(const spm_sys_ops_t *ops)
{
    spm_sys_fake_reset();
    spm_sys_fake_fail_open();

    errno = 0;
    int fd = ops->open_("/dev/spidev0.0", 0);
    assert(fd == -1);
    assert(errno == EACCES);

    TEST_PASS();
}

static void close_twice_returns_ebadf(const spm_sys_ops_t *ops, int fd)
{
    assert(ops && ops->close_);

    errno = 0;
    assert(ops->close_(fd) == 0);
    assert(errno == 0);

    errno = 0;
    assert(ops->close_(fd) == -1);
    assert(errno == EBADF);

    TEST_PASS();
}

/* ====================================================== */
/* ======================= IOCTLs ======================= */
/* ====================================================== */

static void ioctl_read_defaults_returns_valid_val(const spm_sys_ops_t *ops, int fd)
{
    assert(ops && ops->ioctl_);
    spm_sys_fake_reset_ioctl_stats();
    errno = 0;

    unsigned mode32 = 1234;
    unsigned hz     = 0;
    unsigned char bpw = 0;

    assert(ops->ioctl_(fd, SPI_IOC_RD_MODE32, &mode32) == 0);
    assert(mode32 == 0);

    assert(ops->ioctl_(fd, SPI_IOC_RD_BITS_PER_WORD, &bpw) == 0);
    assert(bpw == 8);

    assert(ops->ioctl_(fd, SPI_IOC_RD_MAX_SPEED_HZ, &hz) == 0);
    assert(hz == 1000000u);

    struct spi_ioc_transfer tr = {0};
    assert(ops->ioctl_(fd, SPI_IOC_MESSAGE(1), &tr) == 0);

    expect_stats(/*total*/4, /*rd*/3, /*wr*/0, /*msg*/1, /*fail*/0);
    TEST_PASS();
}

static void ioctl_write_read_roundtrip(const spm_sys_ops_t *ops, int fd)
{
    assert(ops && ops->ioctl_);
    spm_sys_fake_reset_ioctl_stats();

    unsigned mode32   = (SPI_CPOL | SPI_CPHA);
    unsigned hz       = 2000000u;
    unsigned char bpw = 16;

    assert(ops->ioctl_(fd, SPI_IOC_WR_MODE32, &mode32) == 0);
    assert(ops->ioctl_(fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) == 0);
    assert(ops->ioctl_(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) == 0);

    unsigned nmode32   = 0;
    unsigned nhz       = 0;
    unsigned char nbpw = 0;

    assert(ops->ioctl_(fd, SPI_IOC_RD_MODE32, &nmode32) == 0);
    assert(nmode32 == (SPI_CPOL | SPI_CPHA));
    assert(ops->ioctl_(fd, SPI_IOC_RD_MAX_SPEED_HZ, &nhz) == 0);
    assert(nhz == 2000000u);
    assert(ops->ioctl_(fd, SPI_IOC_RD_BITS_PER_WORD, &nbpw) == 0);
    assert(nbpw == 16);

    expect_stats(/*total*/6, /*rd*/3, /*wr*/3, /*msg*/0, /*fail*/0);
    TEST_PASS();
}

static void ioctl_empty_transfer_is_noop_counts_msg(const spm_sys_ops_t *ops, int fd)
{
    assert(ops && ops->ioctl_);
    spm_sys_fake_reset_ioctl_stats();

    struct spi_ioc_transfer tr = {0};
    assert(ops->ioctl_(fd, SPI_IOC_MESSAGE(1), &tr) == 0);

    expect_stats(/*total*/1, /*rd*/0, /*wr*/0, /*msg*/1, /*fail*/0);
    TEST_PASS();
}

static void ioctl_wrong_fd_sets_ebadf(const spm_sys_ops_t *ops, int fd_valid)
{
    assert(ops && ops->ioctl_);
    spm_sys_fake_reset_ioctl_stats();

    unsigned mode32 = (SPI_CPOL | SPI_CPHA);

    errno = 0;
    assert(ops->ioctl_(fd_valid + 1, SPI_IOC_RD_MODE32, &mode32) == -1);
    assert(errno == EBADF);

    expect_stats(/*total*/1, /*rd*/1, /*wr*/0, /*msg*/0, /*fail*/1);
    TEST_PASS();
}

static void ioctl_invalid_request_sets_einval(const spm_sys_ops_t *ops, int fd)
{
    spm_sys_fake_reset_ioctl_stats();

    errno = 0;
    assert(ops->ioctl_(fd, 0xDEADBEEF, NULL) == -1);
    assert(errno == EINVAL);

    expect_stats(/*total*/1, /*rd*/0, /*wr*/0, /*msg*/0, /*fail*/1);
    TEST_PASS();
}

static void ioctl_8bit_mode_read_write(const spm_sys_ops_t *ops, int fd)
{
    spm_sys_fake_reset_ioctl_stats();

    unsigned char mode8 = SPI_CPHA;
    assert(ops->ioctl_(fd, SPI_IOC_WR_MODE, &mode8) == 0);

    unsigned char rd8 = 0;
    assert(ops->ioctl_(fd, SPI_IOC_RD_MODE, &rd8) == 0);
    assert(rd8 == SPI_CPHA);

    expect_stats(/*total*/2, /*rd*/1, /*wr*/1, /*msg*/0, /*fail*/0);
    TEST_PASS();
}

/* ====================================================== */
/* ================== Defaults/Resets =================== */
/* ====================================================== */

static void set_defaults_are_reflected_in_reads(const spm_sys_ops_t *ops)
{
    spm_sys_fake_reset();
    (void)ops; // ops unverändert
    const unsigned new_mode = (SPI_CPOL | SPI_CPHA);
    const unsigned new_hz   = 500000u;
    const unsigned char new_bpw = 12;

    spm_sys_fake_set_defaults(new_mode, new_bpw, new_hz);

    spm_sys_fake_reset_ioctl_stats();

    unsigned mode32 = 0, hz = 0;
    unsigned char bpw = 0;
    int nfd = SPM_SYS_F_DEFAULT.open_("/dev/spidev0.0", 0);
    assert(nfd >= 0);

    assert(SPM_SYS_F_DEFAULT.ioctl_(nfd, SPI_IOC_RD_MODE32, &mode32) == 0);
    assert(mode32 == new_mode);
    assert(SPM_SYS_F_DEFAULT.ioctl_(nfd, SPI_IOC_RD_BITS_PER_WORD, &bpw) == 0);
    assert(bpw == new_bpw);
    assert(SPM_SYS_F_DEFAULT.ioctl_(nfd, SPI_IOC_RD_MAX_SPEED_HZ, &hz) == 0);
    assert(hz == new_hz);

    expect_stats(/*total*/3, /*rd*/3, /*wr*/0, /*msg*/0, /*fail*/0);
    assert(SPM_SYS_F_DEFAULT.close_(nfd) == 0);

    TEST_PASS();
}

static void reset_clears_stats_and_resets_device(const spm_sys_ops_t *ops)
{
    spm_sys_fake_reset();
    int fd = ops->open_("/dev/spidev0.0", 0);
    assert(fd >= 0);

    unsigned mode32 = (SPI_CPOL | SPI_CPHA);
    unsigned hz     = 250000u;
    unsigned char bpw = 9;

    assert(ops->ioctl_(fd, SPI_IOC_WR_MODE32, &mode32) == 0);
    assert(ops->ioctl_(fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) == 0);
    assert(ops->ioctl_(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) == 0);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_reset();

    int nfd = ops->open_("/dev/spidev0.0", 0);
    assert(nfd >= 0);

    unsigned rmode32 = 99, rhz = 1;
    unsigned char rbpw = 1;
    assert(ops->ioctl_(nfd, SPI_IOC_RD_MODE32, &rmode32) == 0);
    assert(rmode32 == 0);
    assert(ops->ioctl_(nfd, SPI_IOC_RD_BITS_PER_WORD, &rbpw) == 0);
    assert(rbpw == 8);
    assert(ops->ioctl_(nfd, SPI_IOC_RD_MAX_SPEED_HZ, &rhz) == 0);
    assert(rhz == 1000000u);

    expect_stats(/*total*/3, /*rd*/3, /*wr*/0, /*msg*/0, /*fail*/0);

    assert(ops->close_(nfd) == 0);
    TEST_PASS();
}

/* ====================================================== */
/* ============== Fail Injection (repeat) =============== */
/* ====================================================== */

static void repeat_fail_all_ioctls(const spm_sys_ops_t *ops)
{
    spm_sys_fake_reset();
    int fd = ops->open_("/dev/spidev0.0", 0);
    assert(fd >= 0);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    unsigned mode32 = 0, hz = 0;
    unsigned char bpw = 0;

    errno = 0;
    assert(ops->ioctl_(fd, SPI_IOC_RD_MODE32, &mode32) == -1);
    assert(errno == EIO);

    errno = 0;
    assert(ops->ioctl_(fd, SPI_IOC_RD_BITS_PER_WORD, &bpw) == -1);
    assert(errno == EIO);

    errno = 0;
    assert(ops->ioctl_(fd, SPI_IOC_RD_MAX_SPEED_HZ, &hz) == -1);
    assert(errno == EIO);

    errno = 0;
    assert(ops->ioctl_(fd, SPI_IOC_MESSAGE(1), &(struct spi_ioc_transfer){0}) == -1);
    assert(errno == EIO);

    expect_stats(/*total*/4, /*rd*/3, /*wr*/0, /*msg*/1, /*fail*/4);

    assert(ops->close_(fd) == 0);
    TEST_PASS();
}

/* ====================================================== */
/* =========================== Main ===================== */
/* ====================================================== */

int main(void) 
{
    const spm_sys_ops_t *ops = &SPM_SYS_F_DEFAULT;
    assert(ops && ops->open_ && ops->close_ && ops->ioctl_);

    int fd = open_dev_returns_valid_val(ops);
    open_2_devs_returns_invalid_val(ops);

    ioctl_read_defaults_returns_valid_val(ops, fd);
    ioctl_write_read_roundtrip(ops, fd);
    ioctl_empty_transfer_is_noop_counts_msg(ops, fd);
    ioctl_wrong_fd_sets_ebadf(ops, fd);
    ioctl_invalid_request_sets_einval(ops, fd);
    ioctl_8bit_mode_read_write(ops, fd);

    close_twice_returns_ebadf(ops, fd);
    open_with_fail_flag_returns_eacces(ops);

    set_defaults_are_reflected_in_reads(ops);
    reset_clears_stats_and_resets_device(ops);
    repeat_fail_all_ioctls(ops);

    TEST_PASS();
    return 0;
}