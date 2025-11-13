#ifndef SPIMONKEY_H
#define SPIMONKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/spi/spidev.h>

#include "spm_error.h"

/* ====================================================== */
/* ===================== Constants ====================== */
/* ====================================================== */

#define SPM_DEFAULT_MAX_SPEED_HZ 25000000u
#define SPM_DEFAULT_SPEED_HZ     5000000u   /* 5 MHz */
#define SPM_PATH_MAX             32 
#define SPM_MAX_BATCH_XFERS      256

/* ====================================================== */
/* ======================= Types ======================== */
/* ====================================================== */

typedef struct spm_sys_ops spm_sys_ops_t;
typedef struct spm_device spm_device_t;

/**
 * @brief SPI clock polarity and phase modes.
 */
typedef enum {
    SPM_MODE0 = SPI_MODE_0,  /**< CPOL=0, CPHA=0 */
    SPM_MODE1 = SPI_MODE_1,  /**< CPOL=0, CPHA=1 */
    SPM_MODE2 = SPI_MODE_2,  /**< CPOL=1, CPHA=0 */
    SPM_MODE3 = SPI_MODE_3,  /**< CPOL=1, CPHA=1 */
} spm_mode_t;

/**
 * @brief Batch transfer descriptor.
 */
typedef struct spm_batch_xfer {
    const void *tx;
    void       *rx;
    size_t      len;
    uint32_t    speed_hz;
    uint8_t     bits_per_word;
    uint16_t    delay_usecs;
    bool        cs_change;
} spm_batch_xfer_t;

/**
 * @brief SPI configuration parameters.
 */
typedef struct {
    spm_mode_t  mode;            /**< Clock mode (MODE0..MODE3) */
    uint32_t    speed_hz;        /**< Clock frequency in Hz */
    uint8_t     bits_per_word;   /**< Bits per word (typically 8) */
    bool        lsb_first;       /**< Transmit LSB first (rare) */
    bool        cs_active_high;  /**< Chip-select active high */
    uint16_t    delay_usecs;     /**< Inter-transfer delay in µs */
    bool        cs_change;       /**< Deassert CS between transfers */
} spm_cfg_t;

/* ====================================================== */
/* ================= Device Lifecycle =================== */
/* ====================================================== */

/**
 * @brief Open a spidev device with custom system operations.
 * 
 * Opens /dev/spidev<bus>.<cs>, allocates a device handle, applies
 * the initial configuration (sanitized), and returns the handle.
 * This variant allows dependency injection via custom sys_ops.
 * 
 * @param bus       SPI bus number
 * @param cs        Chip-select line
 * @param cfg       Initial config (NULL = defaults)
 * @param sys       System operations table (NULL = defaults)
 * @param out_dev   Output: device handle (must not be NULL)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Applied config may differ from requested due to sanitization/clamping
 */
spm_ecode_t spm_dev_open_sys_ops(
    uint8_t bus,
    uint8_t cs,
    const spm_cfg_t *cfg,
    const spm_sys_ops_t *sys,
    spm_device_t **out_dev
);

/**
 * @brief Open a spidev device with default system operations.
 * 
 * Convenience wrapper around spm_dev_open_sys_ops() using default
 * system calls (open, close, ioctl).
 * 
 * @param bus       SPI bus number
 * @param cs        Chip-select line
 * @param cfg       Initial config (NULL = defaults)
 * @param out_dev   Output: device handle (must not be NULL)
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_dev_open(
    uint8_t bus,
    uint8_t cs,
    const spm_cfg_t *cfg,
    spm_device_t **out_dev
);

/**
 * @brief Close device and free all resources.
 * 
 * Closes the file descriptor and frees the device handle. After
 * this call, the handle must not be used. Memory is always freed,
 * even if close() fails.
 * 
 * @param dev  Device handle (may be NULL)
 * 
 * @return SPM_OK on success, SPM_EIO if close failed
 * 
 * @note Not thread-safe
 */
spm_ecode_t spm_dev_close(
    spm_device_t *dev
);

/* ====================================================== */
/* =================== Data Transfer ==================== */
/* ====================================================== */

/**
 * @brief Perform full-duplex SPI transfer.
 * 
 * Executes one SPI_IOC_MESSAGE(1) with current device config.
 * Supports full-duplex, write-only (rx=NULL), and read-only (tx=NULL).
 * 
 * @param dev  Device handle
 * @param tx   Transmit buffer (NULL for read-only)
 * @param rx   Receive buffer (NULL for write-only)
 * @param len  Transfer length in bytes (1..UINT32_MAX)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note At least one of tx/rx must be non-NULL
 * @note Does not modify dev->cfg
 */
spm_ecode_t spm_transfer(
    spm_device_t *dev,
    const void *tx,
    void *rx,
    size_t len
);

/**
 * @brief Write-only SPI transfer.
 * 
 * Convenience wrapper for spm_transfer(dev, tx, NULL, len).
 * 
 * @param dev  Device handle
 * @param tx   Transmit buffer (must not be NULL)
 * @param len  Number of bytes to write (must be > 0)
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_write(
    spm_device_t *dev,
    const void *tx,
    size_t len
);

/**
 * @brief Read-only SPI transfer.
 * 
 * Convenience wrapper for spm_transfer(dev, NULL, rx, len).
 * Transmits dummy bytes on MOSI.
 * 
 * @param dev  Device handle
 * @param rx   Receive buffer (must not be NULL)
 * @param len  Number of bytes to read (must be > 0)
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_read(
    spm_device_t *dev,
    void *rx,
    size_t len
);

/**
 * @brief Perform batch SPI transfers.
 * 
 * Executes multiple transfers in a single ioctl (SPI_IOC_MESSAGE(n)).
 * Reduces syscall overhead. CS remains asserted between transfers
 * unless cs_change=true in a descriptor.
 * 
 * @param dev     Device handle
 * @param xfers   Array of transfer descriptors (must not be NULL)
 * @param count   Number of transfers (1..256)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Each xfer inherits device config if speed_hz/bpw are 0
 * @note All transfers execute atomically (CS assertion)
 */
spm_ecode_t spm_batch(
    spm_device_t *dev,
    const spm_batch_xfer_t *xfers,
    size_t count
);

/* ====================================================== */
/* ============== Configuration Management ============== */
/* ====================================================== */

/**
 * @brief Read current config from kernel driver.
 * 
 * Queries spidev for active settings. Result reflects driver state,
 * which may differ from requested values due to clamping/rounding.
 * 
 * @param dev      Device handle
 * @param out_cfg  Output: current config (must not be NULL)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Does not modify dev->cfg cached state
 */
spm_ecode_t spm_dev_get_cfg(
    spm_device_t *dev,
    spm_cfg_t *out_cfg
);

/**
 * @brief Apply new config and update cached state.
 * 
 * Sanitizes input, writes to driver, then updates dev->cfg with
 * actual applied values (may differ due to clamping/rounding).
 * 
 * @param dev  Device handle
 * @param cfg  Desired config (must not be NULL)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Values are sanitized before applying
 */
spm_ecode_t spm_dev_set_cfg(
    spm_device_t *dev,
    const spm_cfg_t *cfg
);

/**
 * @brief Refresh cached config from driver.
 * 
 * Reads current driver state and updates dev->cfg. Use when
 * external changes or driver adjustments may have desynchronized
 * the cached state.
 * 
 * @param dev  Device handle
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Policy fields (delay, cs_change) are not affected
 */
spm_ecode_t spm_dev_refresh_cfg(
    spm_device_t *dev
);

/**
 * @brief Set SPI clock frequency.
 * 
 * Modifies dev->cfg.speed_hz and applies via spm_dev_set_cfg().
 * Driver may round/clamp the value.
 * 
 * @param dev       Device handle
 * @param speed_hz  Desired clock in Hz (must be > 0)
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_dev_set_speed(
    spm_device_t *dev,
    uint32_t speed_hz
);

/**
 * @brief Set SPI clock mode (CPOL/CPHA).
 * 
 * Modifies dev->cfg.mode and applies via spm_dev_set_cfg().
 * Other flags (LSB_FIRST, CS_HIGH) remain unchanged.
 * 
 * @param dev   Device handle
 * @param mode  Desired mode (SPM_MODE0..SPM_MODE3)
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_dev_set_mode(
    spm_device_t *dev,
    spm_mode_t mode
);

/**
 * @brief Set bits per word.
 * 
 * Modifies dev->cfg.bits_per_word and applies via spm_dev_set_cfg().
 * Driver may clamp to supported values.
 * 
 * @param dev  Device handle
 * @param bpw  Desired bits per word
 * 
 * @return SPM_OK on success, error code otherwise
 */
spm_ecode_t spm_dev_set_bpw(
    spm_device_t *dev,
    uint8_t bpw
);

/* ====================================================== */
/* ==================== Device Info ===================== */
/* ====================================================== */

/**
 * @brief Get device path string.
 * 
 * Copies the spidev path (e.g., "/dev/spidev0.0") to the output buffer.
 * 
 * @param dev       Device handle
 * @param out_path  Output buffer (must not be NULL)
 * @param size      Buffer size in bytes
 * 
 * @return SPM_OK on success, SPM_EPARAM if buffer too small
 * 
 * @note Output is always null-terminated if size >= 1
 */
spm_ecode_t spm_dev_get_path(
    const spm_device_t *dev,
    char *out_path,
    size_t size
);

/**
 * @brief Get file descriptor.
 * 
 * Returns the raw spidev file descriptor. Use with caution;
 * direct operations may desynchronize cached state.
 * 
 * @param dev     Device handle
 * @param out_fd  Output: file descriptor (must not be NULL)
 * 
 * @return SPM_OK on success, error code otherwise
 * 
 * @note Returns -1 if device is closed
 */
spm_ecode_t spm_dev_get_fd(
    const spm_device_t *dev,
    int *out_fd
);

#ifdef __cplusplus
}
#endif
#endif /* SPIMONKEY_H */