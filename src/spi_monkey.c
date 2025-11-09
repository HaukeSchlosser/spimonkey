#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spi_monkey.h"
#include "spm_sys.h"

/**
 * @brief SPIMonkey device
 */
struct spm_device {
    int                 fd;
    spm_cfg_t           cfg;
    spm_cap_t           caps;
    char                path[32];
    spm_error_t         err;
    const spm_sys_ops_t *sys;
};

/* ====================================================== */
/* ================= Validation Macros ================== */
/* ====================================================== */

#define VALIDATE_DEV(dev) \
    do { if (!(dev)) return SPM_EPARAM; } while(0)

#define VALIDATE_DEV_STATE(dev) \
    do { \
        VALIDATE_DEV(dev); \
        if (!spm_sys_valid((dev)->sys)) { \
            SPM_ERROR(&(dev)->err, SPM_ESTATE); \
            return (dev)->err.code; \
        } \
        if ((dev)->fd < 0) { \
            SPM_ERROR(&(dev)->err, SPM_ESTATE); \
            return (dev)->err.code; \
        } \
    } while(0)

#define VALIDATE_PARAM(cond, dev) \
    do { \
        if (!(cond)) { \
            SPM_ERROR(&(dev)->err, SPM_EPARAM); \
            return (dev)->err.code; \
        } \
    } while(0)

/* ====================================================== */
/* =================== Helper Functions ================= */
/* ====================================================== */

static inline int spm_sys_valid(const spm_sys_ops_t *s) {
    return s && s->open_ && s->close_ && s->ioctl_;
}

static inline spm_cfg_t get_default_cfg(void) {
    return (spm_cfg_t){
        .mode           = SPM_MODE0,
        .speed_hz       = 1000000,
        .bits_per_word  = 8,
        .lsb_first      = false,
        .cs_active_high = false,
        .timeout_ms     = 0,
        .delay_usecs    = 0,
        .cs_change      = 0
    };
}

static inline void sanitize_cfg(spm_cfg_t *cfg) {
    if (cfg->mode > SPM_MODE3) cfg->mode = SPM_MODE0;
    if (cfg->bits_per_word < SPM_MIN_BPW_VALUE) cfg->bits_per_word = SPM_MIN_BPW_VALUE;
    if (cfg->bits_per_word > SPM_MAX_BPW_VALUE) cfg->bits_per_word = SPM_MAX_BPW_VALUE;
    if (cfg->speed_hz == 0) cfg->speed_hz = SPM_DEFAULT_SPEED_HZ;
    cfg->lsb_first = !!cfg->lsb_first;
    cfg->cs_active_high = !!cfg->cs_active_high;
    cfg->cs_change = !!cfg->cs_change;
}

/* ====================================================== */
/* =============== Mode Conversion Helpers ============== */
/* ====================================================== */

static inline uint32_t mode_to_mask(spm_mode_t mode) {
    static const uint32_t lut[4] = {0, SPI_CPHA, SPI_CPOL, SPI_CPOL | SPI_CPHA};
    return (mode <= SPM_MODE3) ? lut[mode] : 0;
}

static inline spm_mode_t mask_to_mode(uint32_t mask) {
    static const spm_mode_t lut[4] = {SPM_MODE0, SPM_MODE1, SPM_MODE2, SPM_MODE3};
    return lut[((mask & SPI_CPOL) >> 1) | (mask & SPI_CPHA)];
}

static inline uint32_t cfg_to_mode_mask(const spm_cfg_t *cfg) {
    return mode_to_mask(cfg->mode)
         | (cfg->cs_active_high ? SPI_CS_HIGH   : 0)
         | (cfg->lsb_first      ? SPI_LSB_FIRST : 0);
}

/* ====================================================== */
/* ================ Low-Level IOCTL Ops ================= */
/* ====================================================== */

static int ioctl_read_mode(const spm_device_t *dev, uint32_t *mode) {
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MODE32, mode) == 0) return 0;
    uint8_t mode8;
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MODE, &mode8) < 0) return -1;
    *mode = mode8;
    return 0;
}

static int ioctl_write_mode(const spm_device_t *dev, uint32_t mode) {
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MODE32, &mode) == 0) return 0;
    uint8_t mode8 = (uint8_t)mode;
    return dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MODE, &mode8);
}

static int ioctl_read_config(const spm_device_t *dev, uint32_t *mode, uint8_t *bpw, uint32_t *hz) {
    if (ioctl_read_mode(dev, mode) < 0) return -1;
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_RD_BITS_PER_WORD, bpw) < 0) return -1;
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_RD_MAX_SPEED_HZ, hz) < 0) return -1;
    
    if (*bpw < SPM_MIN_BPW_VALUE) *bpw = SPM_MIN_BPW_VALUE;
    if (*bpw > SPM_MAX_BPW_VALUE) *bpw = SPM_MAX_BPW_VALUE;
    
    return 0;
}

static int ioctl_write_config(const spm_device_t *dev, const spm_cfg_t *cfg) {
    uint32_t mode = cfg_to_mode_mask(cfg);
    if (ioctl_write_mode(dev, mode) < 0) return -1;
    
    uint32_t hz = cfg->speed_hz;
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) return -1;
    
    uint8_t bpw = cfg->bits_per_word;
    if (dev->sys->ioctl_(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0) return -1;
    
    return 0;
}

/* ====================================================== */
/* ============== High-Level Config Helpers ============= */
/* ====================================================== */

static spm_ecode_t read_device_config(const spm_device_t *dev, spm_cfg_t *cfg) {
    uint32_t mode;
    uint8_t bpw;
    uint32_t hz;
    
    if (ioctl_read_config(dev, &mode, &bpw, &hz) < 0) {
        return spm_map_errno();
    }
    
    cfg->mode           = mask_to_mode(mode);
    cfg->cs_active_high = !!(mode & SPI_CS_HIGH);
    cfg->lsb_first      = !!(mode & SPI_LSB_FIRST);
    cfg->bits_per_word  = bpw;
    cfg->speed_hz       = hz;
    
    return SPM_OK;
}

static spm_ecode_t write_device_config(const spm_device_t *dev, spm_cfg_t *cfg) {
    if (ioctl_write_config(dev, cfg) < 0) {
        return spm_map_errno();
    }
    
    /* Verify what was actually set */
    spm_cfg_t actual;
    spm_ecode_t rc = read_device_config(dev, &actual);
    if (rc != SPM_OK) return rc;
    
    /* Update cfg with actual values if different */
    if (cfg->mode != actual.mode || 
        cfg->speed_hz != actual.speed_hz ||
        cfg->bits_per_word != actual.bits_per_word) {
        *cfg = actual;
    }
    
    return SPM_OK;
}

static spm_ecode_t read_device_caps(const spm_device_t *dev, spm_cap_t *caps) {
    uint32_t mode, hz;
    uint8_t bpw;
    
    if (ioctl_read_config(dev, &mode, &bpw, &hz) < 0) {
        return spm_map_errno();
    }
    
    caps->max_speed_hz      = hz ? hz : 25000000u;
    caps->min_bits_per_word = SPM_MIN_BPW_VALUE;
    caps->max_bits_per_word = SPM_MAX_BPW_VALUE;
    caps->features          = mode & SPI_MODE_USER_MASK;
    
    return SPM_OK;
}

/* ====================================================== */
/* ===================== Public API ===================== */
/* ====================================================== */

spm_ecode_t spm_dev_open_sys_ops(uint8_t bus, uint8_t cs, const spm_cfg_t *cfg,
                                  const spm_sys_ops_t *sys, spm_device_t **out_dev)
{
    if (!out_dev) return SPM_EPARAM;
    if (!sys) sys = &SPM_SYS_DEFAULT;
    if (!spm_sys_valid(sys)) return SPM_EPARAM;

    char path[32];
    snprintf(path, sizeof(path), "/dev/spidev%u.%u", bus, cs);

    int fd = sys->open_(path, O_RDWR);
    if (fd < 0) return spm_map_errno();

    spm_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        sys->close_(fd);
        return SPM_ENOMEM;
    }

    dev->fd = fd;
    dev->sys = sys;
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->cfg = cfg ? *cfg : get_default_cfg();
    sanitize_cfg(&dev->cfg);

    spm_ecode_t rc = write_device_config(dev, &dev->cfg);
    if (rc != SPM_OK) {
        SPM_ERROR(&dev->err, rc);
        sys->close_(fd);
        free(dev);
        return rc;
    }

    *out_dev = dev;
    return SPM_OK;
}

spm_ecode_t spm_dev_open(uint8_t bus, uint8_t cs, const spm_cfg_t *cfg, spm_device_t **out_dev) {
    return spm_dev_open_sys_ops(bus, cs, cfg, &SPM_SYS_DEFAULT, out_dev);
}

spm_ecode_t spm_dev_close(spm_device_t *dev) {
    if (!dev) return SPM_EPARAM;
    
    spm_ecode_t rc = SPM_OK;
    if (spm_sys_valid(dev->sys) && dev->fd >= 0) {
        if (dev->sys->close_(dev->fd) < 0) {
            rc = spm_map_errno();
            SPM_ERROR(&dev->err, rc);
        }
    }
    
    free(dev);
    return rc;
}

spm_ecode_t spm_transfer(spm_device_t *dev, const void *tx, void *rx, size_t len) {
    VALIDATE_DEV_STATE(dev);
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
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(xfers && count > 0 && count <= SPM_MAX_BATCH_XFERS, dev);

    /* Allocate kernel transfer array */
    struct spi_ioc_transfer *trs;
    if (count <= 32) {
        trs = alloca(count * sizeof(*trs));
    } else {
        trs = calloc(count, sizeof(*trs));
        if (!trs) {
            SPM_ERROR(&dev->err, SPM_ENOMEM);
            return SPM_ENOMEM;
        }
    }

    /* Build kernel transfer array */
    for (size_t i = 0; i < count; i++) {
        const spm_batch_xfer_t *x = &xfers[i];
        
        if (!x->tx && !x->rx) {
            if (count > 32) free(trs);
            SPM_ERROR(&dev->err, SPM_EPARAM);
            return SPM_EPARAM;
        }
        
        if (x->len == 0 || x->len > UINT32_MAX) {
            if (count > 32) free(trs);
            SPM_ERROR(&dev->err, SPM_EPARAM);
            return SPM_EPARAM;
        }

        trs[i] = (struct spi_ioc_transfer){
            .tx_buf        = (uintptr_t)x->tx,
            .rx_buf        = (uintptr_t)x->rx,
            .len           = (uint32_t)x->len,
            .speed_hz      = x->speed_hz      ? x->speed_hz      : dev->cfg.speed_hz,
            .bits_per_word = x->bits_per_word ? x->bits_per_word : dev->cfg.bits_per_word,
            .delay_usecs   = x->delay_usecs,
            .cs_change     = x->cs_change ? 1 : 0,
        };
    }

    int ret = dev->sys->ioctl_(dev->fd, SPI_IOC_MESSAGE(count), trs);
    
    if (count > 32) free(trs);
    if (ret < 0) {
        SPM_ERROR(&dev->err, spm_map_errno());
        return dev->err.code;
    }

    return SPM_OK;
}

spm_ecode_t spm_write(spm_device_t *dev, const void *tx, size_t len) {
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(tx && len > 0, dev);
    return spm_transfer(dev, tx, NULL, len);
}

spm_ecode_t spm_read(spm_device_t *dev, void *rx, size_t len) {
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(rx && len > 0, dev);
    return spm_transfer(dev, NULL, rx, len);
}

spm_ecode_t spm_dev_get_cfg(spm_device_t *dev, spm_cfg_t *out_cfg) {
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(out_cfg, dev);
    
    spm_ecode_t rc = read_device_config(dev, out_cfg);
    if (rc != SPM_OK) SPM_ERROR(&dev->err, rc);
    return rc;
}

spm_ecode_t spm_dev_set_cfg(spm_device_t *dev, const spm_cfg_t *cfg) {
    VALIDATE_DEV_STATE(dev);
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
    VALIDATE_DEV_STATE(dev);
    
    spm_ecode_t rc = read_device_config(dev, &dev->cfg);
    if (rc != SPM_OK) SPM_ERROR(&dev->err, rc);
    return rc;
}

spm_ecode_t spm_dev_get_caps(spm_device_t *dev, spm_cap_t *out_caps) {
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(out_caps, dev);
    
    spm_ecode_t rc = read_device_caps(dev, out_caps);
    if (rc != SPM_OK) SPM_ERROR(&dev->err, rc);
    return rc;
}

spm_ecode_t spm_dev_set_speed(spm_device_t *dev, uint32_t speed_hz) {
    VALIDATE_DEV_STATE(dev);
    VALIDATE_PARAM(speed_hz > 0, dev);
    
    spm_cfg_t cfg = dev->cfg;
    cfg.speed_hz = speed_hz;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_set_mode(spm_device_t *dev, spm_mode_t mode) {
    VALIDATE_DEV_STATE(dev);
    
    spm_cfg_t cfg = dev->cfg;
    cfg.mode = mode;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_set_bpw(spm_device_t *dev, uint8_t bpw) {
    VALIDATE_DEV_STATE(dev);
    
    spm_cfg_t cfg = dev->cfg;
    cfg.bits_per_word = bpw;
    return spm_dev_set_cfg(dev, &cfg);
}

spm_ecode_t spm_dev_get_path(const spm_device_t *dev, char *out_path, size_t size) {
    if (!dev || !out_path || size == 0) return SPM_EPARAM;
    
    size_t len = strlen(dev->path);
    if (len >= size) return SPM_EPARAM;
    
    strncpy(out_path, dev->path, size - 1);
    out_path[size - 1] = '\0';
    return SPM_OK;
}

spm_ecode_t spm_dev_get_fd(const spm_device_t *dev, int *out_fd) {
    if (!dev || !out_fd) return SPM_EPARAM;
    *out_fd = dev->fd;
    return SPM_OK;
}