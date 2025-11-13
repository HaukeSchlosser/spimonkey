#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "spi_monkey.h"
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

/* ====================================================== */
/* ======================== Open ======================== */
/* ====================================================== */

static void open_device_succeeds_with_valid_sys(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_dev_close(dev);
    TEST_PASS();
}

static void open_device_fails_if_open_fails(void)
{
    spm_sys_fake_reset();
    spm_sys_fake_fail_open();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc != SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

static void open_device_fails_with_null_dev(void)
{
    assert(spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, NULL) != SPM_OK);
    TEST_PASS();
}

static void open_device_fails_with_invalid_bus(void)
{
    uint8_t bus = 20;
    uint8_t cs = 5;

    spm_device_t *dev = NULL;
    assert(spm_dev_open_sys_ops(bus, 0, NULL, &SPM_SYS_F_DEFAULT, &dev) != SPM_OK);
    assert(spm_dev_open_sys_ops(0, cs, NULL, &SPM_SYS_F_DEFAULT, &dev) != SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

static void open_device_fails_if_ioctl_fails(void)
{
    spm_sys_fake_reset();
    spm_sys_fake_fail_open();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc != SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

static void open_device_without_ops_does_succeeds(void)
{
    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open(0, 0, NULL, &dev);
    assert(rc == SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ====================== Transfer ====================== */
/* ====================================================== */

static void transfer_fails_with_null_dev(void)
{
    spm_sys_fake_reset();
    spm_sys_fake_reset_ioctl_stats();

    spm_ecode_t rc = spm_transfer(NULL, (void*)1, NULL, 1);
    assert(rc == SPM_ESTATE);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);

    TEST_PASS();
}

static void transfer_fails_with_both_buf_null(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_transfer(dev, NULL, NULL, 1);
    assert(rc == SPM_EPARAM);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void transfer_fails_with_wrong_len(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_transfer(dev, (void*)1, NULL, 0);
    assert(rc == SPM_EPARAM);

    rc = spm_transfer(dev, (void*)1, NULL, UINT32_MAX+1);
    assert(rc == SPM_EPARAM);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void transfer_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_transfer(dev, (void*)1, NULL, 1);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.fail == 1);
    assert(s.msg == 1);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void transfer_succeeds_tx(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_transfer(dev, (void*)1, NULL, 1);
    assert(rc == SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.msg   == 1);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void transfer_succeeds_rx(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_transfer(dev, NULL, (void*)1, 1);
    assert(rc == SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.msg   == 1);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void transfer_succeeds_dual(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_transfer(dev, (void*)1, (void*)1, 1);
    assert(rc == SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.msg   == 1);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ======================== Write ======================= */
/* ====================================================== */
static void write_succeeds_valid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_write(dev, (void*)1, 1);
    assert(rc == SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.msg   == 1);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void write_fails_invalid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_write(dev, (void*)1, 0);
    assert(rc != SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.msg   == 0);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_write(dev, NULL, 1);
    assert(rc != SPM_OK);

    s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.msg   == 0);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void write_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_write(dev, (void*)1, 1);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.fail == 1);
    assert(s.msg == 1);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ======================== Read ======================== */
/* ====================================================== */
static void read_succeeds_valid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_read(dev, (void*)1, 1);
    assert(rc == SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.msg   == 1);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS();
}


static void read_fails_invalid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_read(dev, (void*)1, 0);
    assert(rc != SPM_OK);

    spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.msg   == 0);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_read(dev, NULL, 1);
    assert(rc != SPM_OK);

    s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.msg   == 0);
    assert(s.rd    == 0);
    assert(s.wr    == 0);
    assert(s.fail  == 0);

    spm_dev_close(dev);
    TEST_PASS(); 
}

static void read_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_read(dev, (void*)1, 1);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 1);
    assert(s.fail == 1);
    assert(s.msg == 1);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ====================== get_cfg ======================= */
/* ====================================================== */

static void get_cfg_succeeds_valid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_cfg_t cfg = {0};
    rc = spm_dev_get_cfg(dev, &cfg);
    assert(rc == SPM_OK);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 3);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void get_cfg_fails_invalid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_cfg_t cfg = {0};
    rc = spm_dev_get_cfg(NULL, &cfg);
    assert(rc == SPM_ESTATE);

    rc = spm_dev_get_cfg(dev, NULL);
    assert(rc == SPM_EPARAM);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void get_cfg_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    spm_cfg_t cfg = {0};
    rc = spm_dev_get_cfg(dev, &cfg);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 2);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ====================== set_cfg ======================= */
/* ====================================================== */

static void set_cfg_succeeds_valid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_cfg_t cfg = {0};
    rc = spm_dev_set_cfg(dev, &cfg);
    assert(rc == SPM_OK);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 6);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 3);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_cfg_fails_invalid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_cfg_t cfg = {0};
    rc = spm_dev_set_cfg(NULL, &cfg);
    assert(rc == SPM_ESTATE);

    rc = spm_dev_set_cfg(dev, NULL);
    assert(rc == SPM_EPARAM);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_cfg_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    spm_cfg_t cfg = {0};
    rc = spm_dev_set_cfg(dev, &cfg);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 2);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ================ spm_dev_refresh_cfg ================= */
/* ====================================================== */

static void refresh_cfg_succeeds_valid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_refresh_cfg(dev);
    assert(rc == SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 3);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void refresh_cfg_fails_invalid_input()
{
    spm_sys_fake_reset();

    spm_sys_fake_reset_ioctl_stats();
    spm_ecode_t rc = spm_dev_refresh_cfg(NULL);
    assert(rc == SPM_ESTATE);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    TEST_PASS();
}

static void refresh_cfg_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_dev_refresh_cfg(dev);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 2);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ================== spm_dev_set_speed ================= */
/* ====================================================== */

static void set_speed_succeeds_valid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_speed(dev, 100000u);
    assert(rc == SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 6);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 3);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_speed_fails_invalid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_speed(NULL, 100000u);
    assert(rc == SPM_ESTATE);

    rc = spm_dev_set_speed(dev, 0);
    assert(rc == SPM_EPARAM);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_speed_cfg_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_dev_set_speed(dev, 100000u);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 2);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ================== spm_dev_set_mode ================== */
/* ====================================================== */

static void set_mode_succeeds_valid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_mode(dev, SPM_MODE3);
    assert(rc == SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 6);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 3);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_mode_fails_invalid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_mode(NULL, SPM_MODE1);
    assert(rc == SPM_ESTATE);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_mode_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_dev_set_mode(dev, SPM_MODE2);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 2);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ==================== spm_dev_set_bpw ================= */
/* ====================================================== */

static void set_bpw_succeeds_valid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_bpw(dev, 8);
    assert(rc == SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 6);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 3);
    assert(s.wr == 3);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_bpw_fails_invalid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    rc = spm_dev_set_bpw(NULL, 8);
    assert(rc == SPM_ESTATE);

    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 0);
    assert(s.fail == 0);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 0);

    // bpw gets sanitized
    rc = spm_dev_set_bpw(dev, 0);
    assert(rc == SPM_OK);
    rc = spm_dev_set_bpw(dev, 33);
    assert(rc == SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

static void set_bpw_fails_when_ioctl_fails(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    spm_sys_fake_reset_ioctl_stats();
    spm_sys_fake_fail_ioctl();

    rc = spm_dev_set_bpw(dev, 16);
    assert(rc != SPM_OK);
    struct spm_sys_fake_ioctl_stats s = spm_sys_fake_get_ioctl_stats();
    assert(s.total == 2);
    assert(s.fail == 2);
    assert(s.msg == 0);
    assert(s.rd == 0);
    assert(s.wr == 2);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* =================== spm_dev_get_path ================= */
/* ====================================================== */

static void get_path_succeeds_valid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    char path[124];
    rc = spm_dev_get_path(dev, path, sizeof(path));
    assert(rc == SPM_OK);
    assert(strcmp(path, "/dev/spidev0.0") == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

static void get_path_fails_invalid_input(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    char path[124];
    
    rc = spm_dev_get_path(NULL, path, sizeof(path));
    assert(rc == SPM_ESTATE);

    rc = spm_dev_get_path(dev, NULL, sizeof(path));
    assert(rc == SPM_EPARAM);

    rc = spm_dev_get_path(dev, path, 0);
    assert(rc == SPM_EPARAM);

    char small[5];
    rc = spm_dev_get_path(dev, small, sizeof(small));
    assert(rc == SPM_EPARAM);

    spm_dev_close(dev);
    TEST_PASS();
}

static void get_path_correct_path_different_bus(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(1, 2, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    char path[124];
    rc = spm_dev_get_path(dev, path, sizeof(path));
    assert(rc == SPM_OK);
    assert(strcmp(path, "/dev/spidev1.2") == 0);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ==================== spm_dev_get_fd ================== */
/* ====================================================== */

static void get_fd_succeeds_valid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    int fd = -1;
    rc = spm_dev_get_fd(dev, &fd);
    assert(rc == SPM_OK);
    assert(fd == 1);

    spm_dev_close(dev);
    TEST_PASS();
}

static void get_fd_fails_invalid_input()
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);
    assert(dev);

    int fd = -1;
    rc = spm_dev_get_fd(NULL, &fd);
    assert(rc == SPM_ESTATE);

    rc = spm_dev_get_fd(dev, NULL);
    assert(rc == SPM_EPARAM);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* ======================= spm_batch ==================== */
/* ====================================================== */
static void batch_transfer_succeeds_multiple_xfers(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);

    uint8_t tx1[] = {0x01, 0x02};
    uint8_t tx2[] = {0x03, 0x04, 0x05};
    uint8_t rx1[2] = {0};
    uint8_t rx2[3] = {0};

    spm_batch_xfer_t xfers[] = {
        { .tx = tx1, .rx = rx1, .len = sizeof(tx1), .speed_hz = 0, .bits_per_word = 0, .delay_usecs = 0, .cs_change = false },
        { .tx = tx2, .rx = rx2, .len = sizeof(tx2), .speed_hz = 0, .bits_per_word = 0, .delay_usecs = 0, .cs_change = false },
    };

    rc = spm_batch(dev, xfers, 2);
    assert(rc == SPM_OK);

    spm_dev_close(dev);
    TEST_PASS();
}

static void batch_transfer_fails_invalid_params(void)
{
    spm_sys_fake_reset();

    spm_device_t *dev = NULL;
    spm_ecode_t rc = spm_dev_open_sys_ops(0, 0, NULL, &SPM_SYS_F_DEFAULT, &dev);
    assert(rc == SPM_OK);

    uint8_t tx[] = {0x01};
    spm_batch_xfer_t xfer = { .tx = tx, .rx = NULL, .len = 1, .speed_hz = 0, .bits_per_word = 0, .delay_usecs = 0, .cs_change = false };

    rc = spm_batch(dev, NULL, 1);
    assert(rc == SPM_EPARAM);

    rc = spm_batch(dev, &xfer, 0);
    assert(rc == SPM_EPARAM);

    rc = spm_batch(dev, &xfer, SPM_MAX_BATCH_XFERS + 1);
    assert(rc == SPM_EPARAM);

    spm_batch_xfer_t bad = { .tx = NULL, .rx = NULL, .len = 1, .speed_hz = 0, .bits_per_word = 0, .delay_usecs = 0, .cs_change = false };
    rc = spm_batch(dev, &bad, 1);
    assert(rc == SPM_EPARAM);

    spm_batch_xfer_t bad_len = { .tx = tx, .rx = NULL, .len = 0, .speed_hz = 0, .bits_per_word = 0, .delay_usecs = 0, .cs_change = false };
    rc = spm_batch(dev, &bad_len, 1);
    assert(rc == SPM_EPARAM);

    spm_dev_close(dev);
    TEST_PASS();
}

/* ====================================================== */
/* =========================== Main ===================== */
/* ====================================================== */

int main(void)
{
    const spm_sys_ops_t *sys = &SPM_SYS_F_DEFAULT;
    assert(sys);

    // Open
    open_device_succeeds_with_valid_sys();
    open_device_fails_if_open_fails();
    open_device_fails_with_null_dev();
    open_device_fails_with_invalid_bus();
    open_device_fails_if_ioctl_fails();
    open_device_without_ops_does_succeeds();
    // Transfer
    transfer_fails_with_null_dev();
    transfer_fails_with_both_buf_null();
    transfer_fails_with_wrong_len();
    transfer_fails_when_ioctl_fails();
    //transfer_fails_with_invalid_fd();
    transfer_fails_when_ioctl_fails();
    transfer_succeeds_tx();
    transfer_succeeds_rx();
    transfer_succeeds_dual();
    // Write
    write_succeeds_valid_input();
    write_fails_invalid_input();
    write_fails_when_ioctl_fails();
    // Read
    read_succeeds_valid_input();
    read_fails_invalid_input();
    read_fails_when_ioctl_fails();
    // get_cfg
    get_cfg_succeeds_valid_input();
    get_cfg_fails_invalid_input();
    get_cfg_fails_when_ioctl_fails();
    // set_cfg
    set_cfg_succeeds_valid_input();
    set_cfg_fails_invalid_input();
    set_cfg_fails_when_ioctl_fails();
    // refresh_cfg
    refresh_cfg_succeeds_valid_input();
    refresh_cfg_fails_invalid_input();
    refresh_cfg_fails_when_ioctl_fails();
    // set_speed
    set_speed_succeeds_valid_input();
    set_speed_fails_invalid_input();
    set_speed_cfg_fails_when_ioctl_fails();
    // set_mode
    set_mode_succeeds_valid_input();
    set_mode_fails_invalid_input();
    set_mode_fails_when_ioctl_fails();
    // set_bpw
    set_bpw_succeeds_valid_input();
    set_bpw_fails_invalid_input();
    set_bpw_fails_when_ioctl_fails();
    // get_path
    get_path_succeeds_valid_input();
    get_path_fails_invalid_input();
    get_path_correct_path_different_bus();
    // get_fd
    get_fd_succeeds_valid_input();
    get_fd_fails_invalid_input();
    // batch
    batch_transfer_succeeds_multiple_xfers();
    batch_transfer_fails_invalid_params();

    TEST_PASS();
    return 0;
} 
