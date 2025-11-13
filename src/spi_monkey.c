#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spm_sys.h"
#include "spi_monkey.h"

/**
 * @brief SPIMonkey device
 */
struct spm_device {
    int                 fd;
    spm_cfg_t           cfg;
    char                path[32];
    spm_error_t         err;
    const spm_sys_ops_t *sys;
};

#define SPM_MIN_BPW_VALUE         8
#define SPM_MAX_BPW_VALUE         32  
#define SPM_BATCH_STACK_THRESHOLD 32

/* ====================================================== */
/* ====================== Validation ==================== */
/* ====================================================== */

static bool v_sys_is_valid(const spm_sys_ops_t *s) 
{
    return s && s->open_ && s->close_ && s->ioctl_;
}

static bool v_fd_is_valid(int fd)
{
    return fd >= 0;
}

static bool v_dev_is_valid(const spm_device_t *dev)
{
    if (!dev)                        return false;
    if (!v_sys_is_valid(dev->sys)) return false;
    if (!v_fd_is_valid(dev->fd))   return false;

    return true;
}

static spm_ecode_t validate_open_parameters(const spm_sys_ops_t **sys, spm_device_t **out_dev)
{
    if (!out_dev)              return SPM_EPARAM;
    if (!*sys)                 *sys = &SPM_SYS_DEFAULT;
    if (!v_sys_is_valid(*sys)) return SPM_EPARAM;
    return SPM_OK;
}

#define VALIDATE_PARAM(cond, dev) \
    do { \
        if (!(cond)) { \
            SPM_ERROR(&(dev)->err, SPM_EPARAM); \
            return (dev)->err.code; \
        } \
    } while(0)

/* ====================================================== */
/* ============ Low Level Config Helpers ================ */
/* ====================================================== */

static spm_cfg_t get_default_cfg(void) 
{
    return (spm_cfg_t){
        .mode           = SPM_MODE0,
        .speed_hz       = 1000000,
        .bits_per_word  = 8,
        .lsb_first      = false,
        .cs_active_high = false,
        .delay_usecs    = 0,
        .cs_change      = 0
    };
}

static void sanitize_cfg(spm_cfg_t *cfg) 
{
    if (cfg->mode > SPM_MODE3) cfg->mode = SPM_MODE0;   
    if (cfg->bits_per_word < SPM_MIN_BPW_VALUE) cfg->bits_per_word = SPM_MIN_BPW_VALUE;
    if (cfg->bits_per_word > SPM_MAX_BPW_VALUE) cfg->bits_per_word = SPM_MAX_BPW_VALUE;
    if (cfg->speed_hz == 0) cfg->speed_hz = SPM_DEFAULT_SPEED_HZ;
    cfg->lsb_first = !!cfg->lsb_first;
    cfg->cs_active_high = !!cfg->cs_active_high;
    cfg->cs_change = !!cfg->cs_change;
}

static uint32_t mode_to_mask(spm_mode_t mode) 
{
    static const uint32_t lut[4] = {0, SPI_CPHA, SPI_CPOL, SPI_CPOL | SPI_CPHA};
    return (mode <= SPM_MODE3) ? lut[mode] : 0;
}

static spm_mode_t mask_to_mode(uint32_t mask) 
{
    static const spm_mode_t lut[4] = { SPM_MODE0, SPM_MODE1, SPM_MODE2, SPM_MODE3 };
    unsigned cpha = (mask & SPI_CPHA) ? 1u : 0u;
    unsigned cpol = (mask & SPI_CPOL) ? 2u : 0u;
    unsigned idx  = cpol | cpha;
    return lut[idx];
}

static uint32_t cfg_to_mode_mask(const spm_cfg_t *cfg) 
{
    return mode_to_mask(cfg->mode)
         | (cfg->cs_active_high ? SPI_CS_HIGH   : 0)
         | (cfg->lsb_first      ? SPI_LSB_FIRST : 0);
}

/* ====================================================== */
/* ================= Low-Level IOCTL Ops ================ */
/* ====================================================== */

static int ioctl_read_bpw(const spm_device_t *dev, uint8_t *bpw) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_RD_BITS_PER_WORD, bpw);
}

static int ioctl_write_bpw(const spm_device_t *dev, uint8_t bpw) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);
}

static void clamp_bpw(uint8_t *bpw) 
{
    if (*bpw < SPM_MIN_BPW_VALUE) *bpw = SPM_MIN_BPW_VALUE;
    if (*bpw > SPM_MAX_BPW_VALUE) *bpw = SPM_MAX_BPW_VALUE;
}

static int ioctl_read_speed(const spm_device_t *dev, uint32_t *hz) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MAX_SPEED_HZ, hz);
}

static int ioctl_write_speed(const spm_device_t *dev, uint32_t hz) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz);
}

static int ioctl_read_mode32(const spm_device_t *dev, uint32_t *mode) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MODE32, mode);
}

static int ioctl_read_mode8(const spm_device_t *dev, uint8_t *mode) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MODE, mode);
}

static int ioctl_write_mode32(const spm_device_t *dev, uint32_t mode) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MODE32, &mode);
}

static int ioctl_write_mode8(const spm_device_t *dev, uint8_t mode) 
{
    return dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MODE, &mode);
}

/* ====================================================== */
/* ================ High-Level IOCTL Ops ================ */
/* ====================================================== */

static int ioctl_read_mode(const spm_device_t *dev, uint32_t *mode) 
{
    int rc = ioctl_read_mode32(dev, mode);
    if (rc == 0) return rc;

    uint8_t mode8;
    rc = ioctl_read_mode8(dev, &mode8);
    if (rc < 0) return rc;

    *mode = mode8;
    return 0;
}

static int ioctl_write_mode(const spm_device_t *dev, uint32_t mode) 
{
    int rc = ioctl_write_mode32(dev, mode);
    if (rc == 0) return rc;

    uint8_t mode8 = (uint8_t)mode;
    return ioctl_write_mode8(dev, mode8);
}

static int ioctl_read_config(const spm_device_t *dev, uint32_t *mode, uint8_t *bpw, uint32_t *hz) 
{
    if (ioctl_read_mode(dev, mode) < 0) return -1;
    if (ioctl_read_bpw(dev, bpw) < 0)   return -1;
    if (ioctl_read_speed(dev, hz) < 0)  return -1;
    clamp_bpw(bpw);
    return 0;
}

static int ioctl_write_config(const spm_device_t *dev, const spm_cfg_t *cfg) 
{
    uint32_t mode = cfg_to_mode_mask(cfg);
    if (ioctl_write_mode(dev, mode) < 0) return -1;
    
    uint32_t hz = cfg->speed_hz;
    if (ioctl_write_speed(dev, hz) < 0) return -1;

    uint8_t bpw = cfg->bits_per_word;
    if (ioctl_write_bpw(dev, bpw) < 0) return -1;

    return 0;
}

static spm_ecode_t ioctl_build_kernel_transfers(spm_device_t *dev,
                                          const spm_batch_xfer_t *xfers,
                                          size_t count,
                                          struct spi_ioc_transfer *trs)
{
    for (size_t i = 0; i < count; i++) {
        const spm_batch_xfer_t *x = &xfers[i];

        if (!x->tx && !x->rx) {
            SPM_ERROR(&dev->err, SPM_EPARAM);
            return dev->err.code;
        }

        if (x->len == 0 || x->len > UINT32_MAX) {
            SPM_ERROR(&dev->err, SPM_EPARAM);
            return dev->err.code;
        }

        trs[i] = (struct spi_ioc_transfer){
            .tx_buf        = (uintptr_t)x->tx,
            .rx_buf        = (uintptr_t)x->rx,
            .len           = (uint32_t)x->len,
            .speed_hz      = x->speed_hz
                             ? x->speed_hz
                             : dev->cfg.speed_hz,
            .bits_per_word = x->bits_per_word
                             ? x->bits_per_word
                             : dev->cfg.bits_per_word,
            .delay_usecs   = x->delay_usecs,
            .cs_change     = x->cs_change ? 1 : 0,
        };
    }

    return SPM_OK;
}

/* ====================================================== */
/* ============= High Level Config Helpers ============== */
/* ====================================================== */

static void fill_config(spm_cfg_t *cfg, uint32_t mode_mask, uint8_t bpw, uint32_t hz) 
{
    cfg->mode           = mask_to_mode(mode_mask);
    cfg->cs_active_high = !!(mode_mask & SPI_CS_HIGH);
    cfg->lsb_first      = !!(mode_mask & SPI_LSB_FIRST);
    cfg->bits_per_word  = bpw;
    cfg->speed_hz       = hz;
}

static spm_ecode_t read_device_config(const spm_device_t *dev, spm_cfg_t *cfg) 
{
    uint8_t  bpw;
    uint32_t hz;
    uint32_t mode_mask;
    
    if (ioctl_read_config(dev, &mode_mask, &bpw, &hz) < 0) return spm_map_errno();
    fill_config(cfg, mode_mask, bpw, hz);
    
    return SPM_OK;
}

static spm_ecode_t write_device_config(const spm_device_t *dev, spm_cfg_t *cfg) 
{
    if (ioctl_write_config(dev, cfg) < 0) {
        return spm_map_errno();
    }
    
    spm_cfg_t actual_cfg;
    spm_ecode_t rc = read_device_config(dev, &actual_cfg);
    if (rc != SPM_OK) {
        return rc;
    }

    *cfg = actual_cfg;
    return SPM_OK;
}

/* ====================================================== */
/* ===================== Public API ===================== */
/* ====================================================== */

spm_ecode_t spm_dev_open_sys_ops(uint8_t bus, uint8_t cs, const spm_cfg_t *cfg,
                                  const spm_sys_ops_t *sys, spm_device_t **out_dev)
{
    spm_ecode_t rc = validate_open_parameters(&sys, out_dev);
    if (rc != SPM_OK) return rc;

    char path[32];
    snprintf(path, sizeof(path), "/dev/spidev%u.%u", bus, cs);

    int fd = sys->open_(path, O_RDWR);
    if (fd < 0) return spm_map_errno();

    spm_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        rc = SPM_ENOMEM;
        goto fail;
    }

    dev->fd = fd;
    dev->sys = sys;
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->cfg = cfg ? *cfg : get_default_cfg();
    sanitize_cfg(&dev->cfg);

    rc = write_device_config(dev, &dev->cfg);
    if (rc != SPM_OK) goto fail;

    *out_dev = dev;
    return SPM_OK;

fail:
    if (fd >= 0) sys->close_(fd);
    free(dev);
    if (out_dev) *out_dev = NULL;
    return rc;
}

spm_ecode_t spm_dev_open(uint8_t bus, uint8_t cs, const spm_cfg_t *cfg, spm_device_t **out_dev) {
    return spm_dev_open_sys_ops(bus, cs, cfg, &SPM_SYS_DEFAULT, out_dev);
}

spm_ecode_t spm_dev_close(spm_device_t *dev) {
    if (!dev) return SPM_EPARAM;
    
    spm_ecode_t rc = SPM_OK;
    if (v_sys_is_valid(dev->sys) && dev->fd >= 0) {
        if (dev->sys->close_(dev->fd) < 0) {
            rc = spm_map_errno();
            SPM_ERROR(&dev->err, rc);
        }
    }
    
    free(dev);
    return rc;
}

spm_ecode_t spm_transfer(spm_device_t *dev, const void *tx, void *rx, size_t len) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(tx || rx, dev);
    VALIDATE_PARAM(len > 0 && len <= UINT32_MAX, dev);

    struct spi_ioc_transfer tr = {
        .tx_buf        = (uintptr_t)tx,
        .rx_buf        = (uintptr_t)rx,
        .len           = (uint32_t)len,
        .speed_hz      = dev->cfg.speed_hz,
        .bits_per_word = dev->cfg.bits_per_word,
        .cs_change     = dev->cfg.cs_change,
        .delay_usecs   = dev->cfg.delay_usecs
    };

    if (dev->sys->ioctl_(dev->fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        SPM_ERROR(&dev->err, spm_map_errno());
        return dev->err.code;
    }

    return SPM_OK;
}

spm_ecode_t spm_batch(spm_device_t *dev, const spm_batch_xfer_t *xfers, size_t count) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(xfers && count > 0 && count <= SPM_MAX_BATCH_XFERS, dev);

    struct spi_ioc_transfer *trs = NULL;
    struct spi_ioc_transfer *heap_trs = NULL;

    /* Allocate kernel transfer array */
    if (count <= SPM_BATCH_STACK_THRESHOLD) {
        trs = alloca(count * sizeof(*trs));
    } else {
        heap_trs = calloc(count, sizeof(*trs));
        if (!heap_trs) {
            SPM_ERROR(&dev->err, SPM_ENOMEM);
            return SPM_ENOMEM;
        }
        trs = heap_trs;
    }

    spm_ecode_t rc = ioctl_build_kernel_transfers(dev, xfers, count, trs);
    if (rc != SPM_OK) {
        if (heap_trs) free(heap_trs);
        return rc;
    }

    int ret = dev->sys->ioctl_(dev->fd, SPI_IOC_MESSAGE(count), trs);
    
    if (heap_trs) free(heap_trs);
    if (ret < 0) {
        SPM_ERROR(&dev->err, spm_map_errno());
        return dev->err.code;
    }

    return SPM_OK;
}

spm_ecode_t spm_write(spm_device_t *dev, const void *tx, size_t len) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(tx && len > 0, dev);
    return spm_transfer(dev, tx, NULL, len);
}

spm_ecode_t spm_read(spm_device_t *dev, void *rx, size_t len) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(rx && len > 0, dev);
    return spm_transfer(dev, NULL, rx, len);
}

spm_ecode_t spm_dev_get_cfg(spm_device_t *dev, spm_cfg_t *out_cfg) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(out_cfg, dev);
    
    spm_ecode_t rc = read_device_config(dev, out_cfg);
    if (rc != SPM_OK) SPM_ERROR(&dev->err, rc);
    return rc;
}

spm_ecode_t spm_dev_set_cfg(spm_device_t *dev, const spm_cfg_t *cfg) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(cfg, dev);

    spm_cfg_t tmp = *cfg;
    sanitize_cfg(&tmp);

    spm_ecode_t rc = write_device_config(dev, &tmp);
    if (rc != SPM_OK) {
        SPM_ERROR(&dev->err, rc);
        return rc;
    }

    dev->cfg = tmp;
    return SPM_OK;
}

spm_ecode_t spm_dev_refresh_cfg(spm_device_t *dev) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    
    spm_ecode_t rc = read_device_config(dev, &dev->cfg);
    if (rc != SPM_OK) SPM_ERROR(&dev->err, rc);
    return rc;
}

spm_ecode_t spm_dev_set_speed(spm_device_t *dev, uint32_t speed_hz) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    VALIDATE_PARAM(speed_hz > 0, dev);
    
    spm_cfg_t cfg = dev->cfg;
    cfg.speed_hz = speed_hz;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_set_mode(spm_device_t *dev, spm_mode_t mode) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    
    spm_cfg_t cfg = dev->cfg;
    cfg.mode = mode;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_set_bpw(spm_device_t *dev, uint8_t bpw) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    
    spm_cfg_t cfg = dev->cfg;
    cfg.bits_per_word = bpw;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_get_path(const spm_device_t *dev, char *out_path, size_t size) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    if (!out_path || size == 0) return SPM_EPARAM;
    
    size_t len = strlen(dev->path);
    if (len >= size) return SPM_EPARAM;
    
    strncpy(out_path, dev->path, size - 1);
    out_path[size - 1] = '\0';
    return SPM_OK;
}

spm_ecode_t spm_dev_get_fd(const spm_device_t *dev, int *out_fd) {
    if (!v_dev_is_valid(dev)) return SPM_ESTATE;
    if (!out_fd) return SPM_EPARAM;
    *out_fd = dev->fd;
    return SPM_OK;
}