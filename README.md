# SPIMonkey 🐵

> **Lightweight, type-safe SPI abstraction layer for Linux spidev**

[![C Standard](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://kernel.org)

SPIMonkey provides a clean, structured C **API** for controlling SPI devices on Linux via the **spidev** kernel driver. It wraps the low-level `ioctl()` interface with safe, well-tested functions for configuration management, data transfers, and error handling.

---

## Features

-  **Simple Device Management** — Open, configure, and close `/dev/spidevX.Y` with minimal boilerplate
-  **Type-Safe Configuration** — Structured `spm_cfg_t` for clock mode, speed, bits-per-word, and more
-  **Batch Transfers** — Execute multiple SPI transactions in a single syscall for improved performance
-  **Automatic Validation** — Configurations are sanitized and clamped to valid ranges before driver application
-  **Config Synchronization** — Keep cached settings in sync with kernel driver state
-  **Dependency Injection** — Custom system operations for unit testing without hardware
-  **Comprehensive Testing** — Extensive unit tests with configurable failure injection
-  **Documentation** — Doxygen-style API docs with usage examples
-  **Thread Safety** — Currently not thread-safety supported!

---

##  Quick Start

### Installation

```bash
git clone https://github.com/HaukeSchlosser/spimonkey.git
cd spimonkey
make
sudo make install
```

This installs:
- Library: `/usr/local/lib/libspimonkey.so`
- Headers: `/usr/local/include/spimonkey/`

### Basic Example

```c
#include <spimonkey/spi_monkey.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    spm_device_t *dev = NULL;
    
    // Configure SPI: Mode 0, 1 MHz, 8 bits per word
    spm_cfg_t cfg = {
        .mode = SPM_MODE0,
        .speed_hz = 1000000,
        .bits_per_word = 8,
        .lsb_first = false,
        .cs_active_high = false
    };
    
    // Open device /dev/spidev0.0
    spm_ecode_t rc = spm_dev_open(0, 0, &cfg, &dev);
    if (rc != SPM_OK) {
        fprintf(stderr, "Open failed: %s\n", spm_strerror(rc));
        return 1;
    }
    
    // Full-duplex transfer
    uint8_t tx_data[] = {0x9F, 0x00, 0x00, 0x00}; // Read ID command
    uint8_t rx_data[4] = {0};
    
    rc = spm_transfer(dev, tx_data, rx_data, sizeof(tx_data));
    if (rc != SPM_OK) {
        fprintf(stderr, "Transfer failed: %s\n", spm_strerror(rc));
        spm_dev_close(dev);
        return 1;
    }
    
    printf("Device ID: 0x%02X%02X%02X\n", rx_data[1], rx_data[2], rx_data[3]);
    
    // Cleanup
    spm_dev_close(dev);
    return 0;
}
```

**Compile:**
```bash
gcc -std=c11 -O2 -Wall example.c -o example -lspimonkey
```

---

##  API Reference

### Device Lifecycle

| Function | Description |
|----------|-------------|
| `spm_dev_open()` | Open SPI device with initial configuration |
| `spm_dev_open_sys_ops()` | Open with custom system operations (for testing) |
| `spm_dev_close()` | Close device and free resources |

### Data Transfer

| Function | Description |
|----------|-------------|
| `spm_transfer()` | Full-duplex SPI transfer |
| `spm_write()` | Write-only transfer (convenience wrapper) |
| `spm_read()` | Read-only transfer (sends dummy bytes on MOSI) |
| `spm_batch()` | Execute multiple transfers in single ioctl |

### Configuration Management

| Function | Description |
|----------|-------------|
| `spm_dev_get_cfg()` | Read current configuration from driver |
| `spm_dev_set_cfg()` | Apply new configuration (sanitized) |
| `spm_dev_refresh_cfg()` | Sync cached config from driver |
| `spm_dev_set_speed()` | Set clock frequency (convenience) |
| `spm_dev_set_mode()` | Set SPI mode: MODE0..MODE3 |
| `spm_dev_set_bpw()` | Set bits-per-word |

### Device Info

| Function | Description |
|----------|-------------|
| `spm_dev_get_path()` | Get device path string (e.g., `/dev/spidev0.0`) |
| `spm_dev_get_fd()` | Get raw file descriptor (use with caution) |

### Error Handling

| Function | Description |
|----------|-------------|
| `spm_strerror()` | Get human-readable error message |
| `spm_perror()` | Print formatted error to stderr |
| `spm_get_last_error()` | Get detailed error context |

---

## Advanced Usage

### Batch Transfers

Reduce syscall overhead by grouping multiple transfers:

```c
spm_batch_xfer_t xfers[] = {
    {
        .tx = write_enable_cmd,
        .rx = NULL,
        .len = 1,
        .speed_hz = 0,          // Use device default
        .bits_per_word = 0,     // Use device default
        .delay_usecs = 10,      // 10µs delay after this transfer
        .cs_change = false      // Keep CS asserted
    },
    {
        .tx = program_cmd,
        .rx = NULL,
        .len = 256,
        .speed_hz = 0,
        .bits_per_word = 0,
        .delay_usecs = 0,
        .cs_change = false
    }
};

spm_ecode_t rc = spm_batch(dev, xfers, 2);
```

### Dynamic Configuration

```c
// Query current settings
spm_cfg_t current;
spm_dev_get_cfg(dev, &current);
printf("Current speed: %u Hz\n", current.speed_hz);

// Change speed on-the-fly
spm_dev_set_speed(dev, 10000000); // 10 MHz

// Verify applied speed (may differ due to hardware constraints)
spm_dev_refresh_cfg(dev);
spm_dev_get_cfg(dev, &current);
printf("Applied speed: %u Hz\n", current.speed_hz);
```

---

##  Testing

### Run Test Suite

```bash
make test_run
```

This executes:
1. **`spm_sys_fake_test`** — Tests fake system operations layer
2. **`spi_monkey_test`** — Complete API test suite (33+ tests)

### Test Coverage

- Device lifecycle (open/close)
- Configuration management (get/set/refresh)
- Data transfers (read/write/full-duplex/batch)
- Error handling and parameter validation
- Buffer safety (null pointers, zero lengths)
- Failure injection (simulated driver errors)

### Example Output

```
→ Running test/build/spi_monkey_test
spi_monkey_test::open_returns_valid_dev_on_success             PASSED
spi_monkey_test::transfer_succeeds_full_duplex                 PASSED
spi_monkey_test::batch_transfer_succeeds_multiple_xfers        PASSED
...
spi_monkey_test: ALL TESTS PASSED
```

---

##  Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `SPM_OK` | 0 | Success |
| `SPM_EPARAM` | -1 | Invalid parameter (NULL pointer, zero length, etc.) |
| `SPM_ENOTSUP` | -2 | Operation not supported by driver |
| `SPM_ENODEV` | -3 | Device not found or inaccessible |
| `SPM_EBUS` | -4 | Bus error during transfer |
| `SPM_ETIMEOUT` | -5 | Operation timeout |
| `SPM_EIO` | -6 | General I/O error |
| `SPM_ESTATE` | -7 | Invalid device state |
| `SPM_ECONFIG` | -8 | Configuration error |
| `SPM_ENOMEM` | -9 | Memory allocation failed |
| `SPM_ECRC` | -10 | Data integrity error |
| `SPM_EAGAIN` | -11 | Resource temporarily unavailable |

### Error Context

Each error includes:
-  Error code
-  System `errno` (if applicable)
-  Source file and line number
-  Function name

**Example:**
```c
spm_ecode_t rc = spm_transfer(dev, NULL, rx, 10); // Invalid: tx=NULL, rx!=NULL
if (rc != SPM_OK) {
    const spm_error_t *err = spm_get_last_error(dev);
    fprintf(stderr, "Error in %s():%d - %s (errno=%d)\n",
            err->func, err->line, spm_strerror(rc), err->syserr);
}
```

---

## Requirements

- **OS**: Linux 3.0+ with spidev driver (`CONFIG_SPI_SPIDEV=y`)
- **Compiler**: GCC 8+ or Clang 6+ (C11 standard)
- **Hardware**: Any SPI controller with spidev support (Raspberry Pi, BeagleBone, etc.)
- **Permissions**: Read/write access to `/dev/spidevX.Y` (add user to `spi` group or use `sudo`)

### Enable SPI on Raspberry Pi

```bash
# Enable SPI via raspi-config
sudo raspi-config
# → Interface Options → SPI → Yes

# Verify device nodes
ls -l /dev/spidev*
# Expected: /dev/spidev0.0, /dev/spidev0.1

# Add user to spi group
sudo usermod -a -G spi $USER
# Logout and login for changes to take effect
```

---

##  Build Configuration

### Debug Build

```bash
make clean
CFLAGS="-g -O0 -DDEBUG" make
```

### Static Library

```bash
make clean
make STATIC=1
# Produces: build/libspimonkey.a
```

### Cross-Compilation

```bash
make clean
CC=arm-linux-gnueabihf-gcc make
```

---

##  License

This project is licensed under the **GPL-3.0**.