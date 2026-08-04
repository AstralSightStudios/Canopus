# Host tests for the Canopus portable C runtime.
#
# Compiles the target-independent runtime + tests with the host clang.
# The runtime must also compile cleanly for arm-none-eabi; see
# tools/canopus-core later for the cross-check.

CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -g -O0

INC = -I../../sdk/c -I../../runtime/lifecycle -I../../runtime/resources \
      -I../../runtime/diagnostics -I../../runtime/control -I../../runtime/module

RUNTIME_SRCS = \
	../../runtime/control/canopus_control.c \
	../../runtime/lifecycle/canopus_lifecycle.c \
	../../runtime/resources/canopus_resource.c \
	../../runtime/diagnostics/canopus_diagnostics.c \
	../../runtime/module/canopus_module.c

TEST_SRCS = \
	main.c \
	test_abi.c \
	test_control.c \
	test_lifecycle.c \
	test_resources.c \
	test_diagnostics.c

BIN = canopus_host_tests

.PHONY: all run clean cross-arm64 cross-thumb

all: $(BIN)

$(BIN): $(RUNTIME_SRCS) $(TEST_SRCS) ../../sdk/c/canopus_abi.h ../../sdk/c/canopus_runtime.h
	$(CC) $(CFLAGS) $(INC) $(RUNTIME_SRCS) $(TEST_SRCS) -o $(BIN)

run: $(BIN)
	./$(BIN)

# Sanity: the runtime must also compile for the actual target profile
# (Cortex-M33 Thumb soft-float). Only compile+assemble, no link needed.
cross-thumb:
	$(CC) --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
		-ffreestanding -fno-common -fno-builtin -fno-stack-protector \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -Os -Wall -Wextra -Werror \
		$(INC) -c $(RUNTIME_SRCS)

clean:
	rm -f $(BIN) *.o
