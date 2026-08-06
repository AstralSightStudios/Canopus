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
      -I../../manager/ui -I../../manager/service -I../../manager/client \
      -I../../manager/native-app -I../../manager/package -I../../tests/host \
      -I../../third_party/monocypher \
      -I../../app-sdk/c -I../../app-sdk/launcher -I../../app-sdk/ui
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
	../../manager/client/canopus_client.c \
	../../manager/service/canopus_supervisor.c \
	../../manager/package/canopus_installer_bundle.c \
	../../third_party/monocypher/monocypher.c \
	../../third_party/monocypher/monocypher-ed25519.c \
	../../manager/ui/canopus_manager.c \
	../../manager/ui/canopus_manager_native.c \
	../../manager/native-app/canopus_manager_app.c \
	../../app-sdk/c/canopus_app.c \
	../../app-sdk/ui/canopus_ui.c \
	../../app-sdk/launcher/canopus_ordered_list.c

# Host test build additionally links the libc-dependent layers
# (storage, POSIX client adapter, fake target, hello module).
RUNTIME_SRCS = \
	$(PORTABLE_SRCS) \
	../../manager/storage/canopus_store.c \
	../../manager/client/canopus_client_posix.c \
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
	test_manager_native.c \
	test_manager_app.c \
	test_app_sdk.c \
	test_ui.c \
	test_client.c \
	test_supervisor_device.c \
	test_installer_receipt.c

BIN = canopus_host_tests

.PHONY: all run clean cross-arm64 cross-thumb

all: $(BIN)

$(BIN): $(RUNTIME_SRCS) $(TEST_SRCS) ../../sdk/c/canopus_abi.h ../../sdk/c/canopus_runtime.h
	$(CC) $(CFLAGS) $(INC) $(RUNTIME_SRCS) $(TEST_SRCS) -o $(BIN)

run: $(BIN)
	./$(BIN)

# Sanity: the bare-metal portable runtime must compile for the actual target
# profile (Cortex-M33 Thumb soft-float). Only compile+assemble, no link.
# Storage / POSIX client adapter / fake target need libc and are NOT part of
# this check.
cross-thumb:
	$(CC) --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
		-ffreestanding -fno-common -fno-builtin -fno-stack-protector \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -Os -Wall -Wextra -Werror \
		-DCANOPUS_HOST=1 $(INC) -c $(PORTABLE_SRCS)

clean:
	rm -f $(BIN) *.o
