NAME ?= clox

CFLAGS += -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS += -D_GNU_SOURCE -DCONSTANTS_MAX=256 -DFRAMES_MAX=64

DFLAGS = -Og -g3 --coverage -DDEBUG -DDEBUG_STRESS_GC
RFLAGS = -O3 -flto
SCRIPT = examples/fib25.lox

HDRS := $(wildcard source/*.h)
SRCS := $(wildcard source/*.c)
OBJS := $(notdir $(SRCS:.c=.o))
DEPS := $(OBJS:.o=.d)

TEST = cd $(HOME)/downloads/craftinginterpreters; \
	dart tool/bin/test.dart clox --interpreter $(CURDIR)/$(1)
BENCH = find bench -type f -exec echo -n {} " " \; -exec $(1) {} \;

debug: build/debug/$(NAME)
	ln -sf $< .

release $(NAME): build/release/$(NAME)
	ln -sf $< .

build/release/$(NAME): build/profile/$(NAME)
	$(call BENCH,$<) > /dev/null
	mkdir -p build/release
	$(CC) $(CFLAGS) $(RFLAGS) $(SRCS) -o $@ -fprofile-use=build/profile

build/profile/$(NAME): $(HDRS) $(SRCS) Makefile
	mkdir -p build/profile
	$(RM) build/profile/*.gcda
	$(CC) $(CFLAGS) $(RFLAGS) $(SRCS) -o $@ -fprofile-generate=build/profile

build/debug/$(NAME): $(addprefix build/debug/,$(OBJS))
	$(RM) build/debug/*.gcda
	$(CC) $(CFLAGS) $(DFLAGS) $^ -o $@

build/debug/%.o: source/%.c Makefile
	mkdir -p build/debug
	$(CC) $(CFLAGS) $(DFLAGS) -c $< -o $@ -MMD -MP

-include $(addprefix build/debug/,$(DEPS))

all: format test-debug test-release cov leak heap bench

test test-debug: build/debug/$(NAME)
	@echo "Testing $<:"
	@$(call TEST,$<)

test-release: build/release/$(NAME)
	@echo "Testing $<:"
	@$(call TEST,$<)

cov: build/debug/$(NAME)
	@$(call TEST,$<) > /dev/null
	@gcov build/debug/*.gcda | \
		sed "s/'//g;s/:/ /" | \
		awk '{getline b;getline c;getline d;printf "%s %s\n",$$0,b}' | \
		awk '{printf "%-19s %7s of %4s\n",$$2,$$5,$$7}'
	@mv *.c.gcov *.h.gcov build/debug

leak: build/debug/$(NAME)
	@valgrind -q --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1 \
		./$< $(SCRIPT) > /dev/null && echo "Memcheck:  no leaks, no errors"

heap: build/release/$(NAME)
	@valgrind --tool=dhat --dhat-out-file=/dev/null \
		./$< $(SCRIPT) 2>&1 >/dev/null | \
		head -n 11 | tail -n 5 | cut -d " " -f 2- | grep -v "At t-end:"

bench: build/release/$(NAME)
	@$(call BENCH,$<) | \
		awk '{t+=$$2;printf"%-25s %7.3f s\n",$$1,$$2}END{printf"%-25s %7.3f s\n","BENCHMARK TOTAL",t}'

format:
	@clang-format -i source/*.c source/*.h

run: $(NAME)
	./$<

clean:
	$(RM) -r build $(NAME)

.PHONY: release debug all test test-debug test-release cov leak heap bench format run clean
