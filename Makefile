UNAME := $(shell uname -s)
CC ?= cc
BIN := eosh
BUILD := build

CFLAGS ?= -g -O1
CFLAGS += -std=gnu11 -Wall -Wextra -Isrc
ifeq ($(WERROR),1)
  CFLAGS += -Werror
endif
ifeq ($(SAN),1)
  CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS += -fsanitize=address,undefined
endif

# Line editing: Homebrew readline, then pkg-config readline, then libedit.
RL_PREFIX := $(shell brew --prefix readline 2>/dev/null)
ifneq ($(RL_PREFIX),)
  CPPFLAGS += -I$(RL_PREFIX)/include
  LDLIBS += -L$(RL_PREFIX)/lib -lreadline
else ifeq ($(shell pkg-config --exists readline 2>/dev/null && echo y),y)
  CPPFLAGS += $(shell pkg-config --cflags readline)
  LDLIBS += $(shell pkg-config --libs readline)
else
  LDLIBS += -ledit
endif

SRCS := $(wildcard src/*.c) $(wildcard src/cmds/*.c) $(wildcard src/sys/*.c)
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

.PHONY: all clean test
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: $(BIN) $(BUILD)/test_pipe
	@$(BUILD)/test_pipe
	@sh tests/run.sh

$(BUILD)/test_pipe: tests/test_pipe.c src/pipe.c $(BUILD)/cmdtree.o
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_pipe.c $(BUILD)/cmdtree.o

clean:
	rm -rf $(BUILD) $(BIN)

-include $(OBJS:.o=.d)
