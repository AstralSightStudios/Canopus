# Host tests for the Canopus portable C runtime.
#
# Compiles the target-independent runtime + tests with the host clang.
# The runtime must also compile cleanly for arm-none-eabi; see
# tools/canopus-core later for the cross-check.

CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -g -O0

INC = -I. -I../../sdk/c -I../../runtime/lifecycle -I../../runtime/resources \
      -I../../runtime/diagnostics -I../../runtime/control -I../../runtime/module \
      -I../../modules/examples/hello -I../../manager/protocol -I../../manager/storage \
      -I../../manager/ui -I../../tests/host -I../../app-sdk/c -I../../app-sdk/launcher
CFLAGS += -DCANOPUS_HOST=1

# Bare-metal portable sources (freestanding, no libc) — the cross-thumb
# sanity build checks exactly these.
PORTABLE_SRCS = \
	../../runtime/control/canopus_control.c \
	../../runtime/lifecycle/canopus_lifecycle.c \
	../../runtime/resources/canopus_resource.c \
	../../runtime/diagnostics/canopus_diagnostics.c \
	../../runtime/module/canopus_module.c \
	../../manager/protocol/canopus_protocol.c \
	../../app-sdk/c/canopus_app.c \
	../../app-sdk/launcher/canopus_ordered_list.c

# Host test build additionally links the libc-dependent layers
# (storage, manager UI, fake target, hello module).
RUNTIME_SRCS = \
	$(PORTABLE_SRCS) \
	../../manager/storage/canopus_store.c \
	../../manager/ui/canopus_manager.c \
	fake_target.c \
	../../modules/examples/hello/hello.c

TEST_SRCS = \
	main.c \
	test_abi.c \
	test_control.c \
	test_lifecycle.c \
	test_resources.c \
	test_diagnostics.c \
	test_hello_module.c \
	test_supervisor.c \
	test_manager_ui.c \
	test_app_sdk.c

BIN = canopus_host_tests

.PHONY: all run clean cross-arm64 cross-thumb

all: $(BIN)

$(BIN): $(RUNTIME_SRCS) $(TEST_SRCS) ../../sdk/c/canopus_abi.h ../../sdk/c/canopus_runtime.h
	$(CC) $(CFLAGS) $(INC) $(RUNTIME_SRCS) $(TEST_SRCS) -o $(BIN)

run: $(BIN)
	./$(BIN)

# Sanity: the bare-metal portable runtime must compile for the actual target
# profile (Cortex-M33 Thumb soft-float). Only compile+assemble, no link.
# Storage / manager UI / fake target need libc and are NOT part of this check.
cross-thumb:
	$(CC) --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
		-ffreestanding -fno-common -fno-builtin -fno-stack-protector \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -Os -Wall -Wextra -Werror \
		-DCANOPUS_HOST=1 $(INC) -c $(PORTABLE_SRCS)

clean:
	rm -f $(BIN) *.o
