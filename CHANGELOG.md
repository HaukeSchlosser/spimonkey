# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0] - 2025-11-09

### Added
- **Core API**: Full-featured SPI device abstraction layer
  - `spm_dev_open()` / `spm_dev_open_sys_ops()` - Device initialization with dependency injection support
  - `spm_dev_close()` - Clean resource deallocation
  
- **Data Transfer Functions**
  - `spm_transfer()` - Full-duplex SPI transfers
  - `spm_write()` - Write-only convenience wrapper
  - `spm_read()` - Read-only convenience wrapper
  - `spm_batch()` - Batch transfers with single ioctl (reduced syscall overhead)
  
- **Configuration Management**
  - `spm_dev_get_cfg()` - Query current driver configuration
  - `spm_dev_set_cfg()` - Apply new configuration with sanitization
  - `spm_dev_refresh_cfg()` - Sync cached config from driver
  - `spm_dev_set_speed()` - Convenience function for clock frequency
  - `spm_dev_set_mode()` - Convenience function for SPI mode (CPOL/CPHA)
  - `spm_dev_set_bpw()` - Convenience function for bits-per-word
  - `spm_dev_get_caps()` - Query device capabilities (best-effort)
  
- **Device Information**
  - `spm_dev_get_path()` - Retrieve spidev path string
  - `spm_dev_get_fd()` - Access raw file descriptor
  
- **Error Handling**
  - Comprehensive error code system (`SPM_OK`, `SPM_EPARAM`, `SPM_EIO`, etc.)
  - Thread-local error context with errno preservation
  - `spm_strerror()` - Human-readable error messages
  - `spm_perror()` - Formatted error output
  
- **Testing Infrastructure**
  - Full unit test suite (33+ tests)
  - `spm_sys_fake` - Fake system operations for isolated testing
  - Configurable failure injection for robustness testing
  - Statistics tracking (read/write/message counts)
  
- **Build System**
  - Makefile with shared library target (`libspimonkey.so`)
  - Separate test builds with fake dependencies
  - Debug and release configurations
  - RPATH support for test execution
  
- **Documentation**
  - Comprehensive API documentation with Doxygen-style comments
  - README with usage examples and integration guide
  - MIT License