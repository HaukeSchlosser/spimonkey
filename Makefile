LIB_NAME  = spimonkey

CC        = gcc
CFLAGS    = -Wall -O2 -fPIC
LDFLAGS   = -shared -Wl,--no-undefined
INCLUDES  = -Iincludes

SRC_DIR   = src
BUILD_DIR = build
TARGET    = $(BUILD_DIR)/lib$(LIB_NAME).so

SRCS = \
	$(SRC_DIR)/spi_monkey.c \
	$(SRC_DIR)/spm_error.c \
	$(SRC_DIR)/spm_sys.c

INSTALL_LIB_DIR = /usr/local/lib
INSTALL_INC_DIR = /usr/local/include/$(LIB_NAME)

.PHONY: all clean install uninstall test_build test_run

# ===== Tests =====
TEST_SRC_DIR    = test/src
TEST_INC_DIR    = test/includes
TEST_BUILD_DIR  = test/build
TEST_INC        = -Iincludes -I$(TEST_INC_DIR)

# Quellfiles
TEST_FAKE_SRC   = $(TEST_SRC_DIR)/spm_sys_fake.c
TESTS           = spm_sys_fake_test spi_monkey_test

# Ziele
TEST_TARGETS    = $(addprefix $(TEST_BUILD_DIR)/,$(TESTS))

all: $(TARGET)

# ===== Library Build =====
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(SRCS)

# ===== Tests Build =====
$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)/%: $(TEST_SRC_DIR)/%.c $(TEST_FAKE_SRC) $(TARGET) | $(TEST_BUILD_DIR)
	$(CC) -Wall -O2 $(TEST_INC) \
	    -o $@ $< $(TEST_FAKE_SRC) \
	    -L$(BUILD_DIR) -l$(LIB_NAME) -Wl,-rpath,$(abspath $(BUILD_DIR))

test_build: $(TEST_TARGETS)

test_run: test_build
	@echo "Running tests..."
	@set -e; \
	for t in $(TEST_TARGETS); do \
	    echo ""; echo "→ Running $$t"; "$$t"; \
	done; \
	echo ""; echo "All tests PASSED."

test_clean:
	rm -rf "$(TEST_BUILD_DIR)"
	@echo "Cleaned test build files"

# ===== Install / Uninstall =====
install: $(TARGET)
	install -d "$(INSTALL_LIB_DIR)" "$(INSTALL_INC_DIR)"
	install -m 755 "$(TARGET)" "$(INSTALL_LIB_DIR)/lib$(LIB_NAME).so"
	install -m 644 includes/*.h "$(INSTALL_INC_DIR)/"
	-ldconfig 2>/dev/null || true
	@echo "Library installed to $(INSTALL_LIB_DIR)"
	@echo "Headers  installed to $(INSTALL_INC_DIR)"

uninstall:
	rm -f  "$(INSTALL_LIB_DIR)/lib$(LIB_NAME).so"
	rm -rf "$(INSTALL_INC_DIR)"
	-ldconfig 2>/dev/null || true
	@echo "Library uninstalled"

# ===== Clean =====
clean:
	rm -rf "$(BUILD_DIR)"
	@echo "Cleaned build files"