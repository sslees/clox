NAME ?= clox

CFLAGS += -std=c99 -Wall -Wextra -Werror
CFLAGS += -D_GNU_SOURCE -DCONSTANTS_MAX=256 -DFRAMES_MAX=64

DFLAGS = -O0 -g3 --coverage -DDEBUG -DDEBUG_CHECK_STACK -DDEBUG_STRESS_GC
PFLAGS = -O0 -g3 -pg
RFLAGS = -O3 -flto
LIBS = -lm

HDRS := $(wildcard source/*.h)
SRCS := $(wildcard source/*.c)
OBJS := $(notdir $(SRCS:.c=.o))
DEPS := $(OBJS:.o=.d)

CLANG_FORMAT = $(wildcard $(HOME)/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format)
SCRIPT = examples/fib25.lox
TESTS = $(HOME)/downloads/craftinginterpreters/test
TEST = cd $(TESTS)/..; dart tool/bin/test.dart clox --interpreter $(CURDIR)/$(1)
BENCH = find bench -type f -exec echo -n {} " " \; -exec $(1) {} \;

debug: build/debug/$(NAME)
	ln -sf $< .

profile: build/gprof/analysis.txt

release $(NAME): build/release/$(NAME)
	ln -sf $< .

build/release/$(NAME): build/pgo/$(NAME)
	$(call BENCH,$<) > /dev/null
	mkdir -p build/release
	$(CC) $(CFLAGS) $(RFLAGS) $(SRCS) $(LIBS) -o $@ -fprofile-use=build/pgo

build/pgo/$(NAME): $(HDRS) $(SRCS) Makefile
	mkdir -p build/pgo
	$(RM) build/pgo/*.gcda
	$(CC) $(CFLAGS) $(RFLAGS) $(SRCS) $(LIBS) -o $@ -fprofile-generate=build/pgo

build/gprof/analysis.txt: build/gprof/$(NAME)
	$(RM) build/gprof/gmon.out.*
	GMON_OUT_PREFIX=build/gprof/gmon.out $< $(SCRIPT) > /dev/null
	gprof $< build/gprof/gmon.out.* > $@

build/gprof/$(NAME): $(HDRS) $(SRCS) Makefile
	mkdir -p build/gprof
	$(CC) $(CFLAGS) $(PFLAGS) $(SRCS) $(LIBS) -o $@

build/debug/$(NAME): $(addprefix build/debug/,$(OBJS))
	$(RM) build/debug/*.gcda
	$(CC) $(CFLAGS) $(DFLAGS) $^ $(LIBS) -o $@

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
		$< $(SCRIPT) > /dev/null && echo "Memcheck:  no leaks, no errors"

leak-full: build/debug/$(NAME)
	@echo Checking for leaks with: valgrind --leak-check=full --errors-for-leak-kinds=all $< \[script\]
	@for script in $$(find -L $(TESTS) -path "$(TESTS)/benchmark" -prune -o -type f -name "*.lox" -print); do \
		valgrind --leak-check=full --errors-for-leak-kinds=all $< $$script 2>&1 | \
		grep -q "ERROR SUMMARY: 0 errors" || echo leak check FAIL: $$script ; \
	done

heap: build/release/$(NAME)
	@valgrind --tool=dhat --dhat-out-file=/dev/null \
		$< $(SCRIPT) 2>&1 >/dev/null | \
		head -n 11 | tail -n 5 | cut -d " " -f 2- | grep -v "At t-end:"

bench: build/release/$(NAME)
	@$(call BENCH,$<) | \
		awk '{t+=$$2;printf"%-25s %7.3f s\n",$$1,$$2}END{printf"%-25s %7.3f s\n","BENCHMARK TOTAL",t}'

format:
	@$(CLANG_FORMAT) -i source/*.c source/*.h

run: $(NAME)
	./$<

clean:
	$(RM) -r build $(NAME)

.PHONY: release profile debug all test test-debug test-release cov leak leak-full heap bench format run clean
