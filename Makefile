NAME = nemu

ISA ?= picorv32
ISAS = $(shell ls src/isa/)
ifeq ($(filter $(ISAS), $(ISA)), ) # ISA must be valid
$(error Invalid ISA. Supported: $(ISAS))
endif

ifeq ($(ISA), picorv32)
VERILATOR = verilator
VERILATOR_ROOT ?= $(shell $(VERILATOR) --getenv VERILATOR_ROOT)
endif

ENGINE ?= interpreter
ENGINES = $(shell ls src/engine/)
ifeq ($(filter $(ENGINES), $(ENGINE)), ) # ENGINE must be valid
$(error Invalid ENGINE. Supported: $(ENGINES))
endif

$(info Building $(ISA)-$(NAME)-$(ENGINE))

STATIC_LIBS =
INC_DIR += ./include ./src/engine/$(ENGINE)
BUILD_DIR ?= ./build

ifeq ($(ISA), picorv32)
INC_DIR += $(VERILATOR_ROOT)/include $(VERILATOR_ROOT)/include/vltstd $(OBJ_DIR)/picorv32
endif

ifdef SHARE
SO = -so
SO_CFLAGS = -fPIC -D_SHARE=1
SO_LDLAGS = -shared -fPIC
else
LD_LIBS = -lm -ldl -lSDL2 -lreadline -ldl -lstdc++
endif

ifndef SHARE
DIFF ?= kvm
ifneq ($(ISA),x86)
ifeq ($(DIFF),kvm)
DIFF = qemu
$(info KVM is only supported with ISA=x86, use QEMU instead)
endif
endif

ifeq ($(DIFF),qemu)
DIFF_REF_PATH = $(NEMU_HOME)/tools/qemu-diff
DIFF_REF_SO = $(DIFF_REF_PATH)/build/$(ISA)-qemu-so
CFLAGS += -D__DIFF_REF_QEMU__
else ifeq ($(DIFF),kvm)
DIFF_REF_PATH = $(NEMU_HOME)/tools/kvm-diff
DIFF_REF_SO = $(DIFF_REF_PATH)/build/$(ISA)-kvm-so
CFLAGS += -D__DIFF_REF_KVM__
else ifeq ($(DIFF),nemu)
DIFF_REF_PATH = $(NEMU_HOME)
DIFF_REF_SO = $(DIFF_REF_PATH)/build/$(ISA)-nemu-interpreter-so
CFLAGS += -D__DIFF_REF_NEMU__
MKFLAGS = ISA=$(ISA) SHARE=1 ENGINE=interpreter
else
$(error invalid DIFF. Supported: qemu kvm nemu)
endif
endif

OBJ_DIR ?= $(BUILD_DIR)/obj-$(ISA)-$(ENGINE)$(SO)
BINARY ?= $(BUILD_DIR)/$(ISA)-$(NAME)-$(ENGINE)$(SO)

#include Makefile.git

.DEFAULT_GOAL = app

# Compilation flags
CC ?= gcc
CXX ?= g++
LD ?= gcc
INCLUDES  = $(addprefix -I, $(INC_DIR))
CFLAGS   += -MMD -g $(INCLUDES) \
            -D__ENGINE_$(ENGINE)__ \
            -D__ISA__=$(ISA) -D__ISA_$(ISA)__ -D_ISA_H_=\"isa/$(ISA).h\"
CXXFLAGS += -std=gnu++14
CXXFLAGS += $(CFLAGS)

# Files to be compiled
SRCS = $(shell find src/ -name "*.c" | grep -v "isa\|engine")
SRCS += $(shell find src/isa/$(ISA) -name "*.c")
SRCS += $(shell find src/engine/$(ENGINE) -name "*.c")
CXXSRCS = $(shell find src/isa/$(ISA) -name "*.cc")

OBJS = $(SRCS:src/%.c=$(OBJ_DIR)/%.o)
OBJS += $(CXXSRCS:src/%.cc=$(OBJ_DIR)/%.o)

ifeq ($(ISA), picorv32)
STATIC_LIBS += $(OBJ_DIR)/picorv32/Vpicorv32_axi__ALL.a

$(OBJ_DIR)/picorv32/Vpicorv32_axi__ALL.a:
	@echo + VERILATOR $@
		@mkdir -p $(dir $@)
	$(VERILATOR) -cc --build --trace ./verilator/picorv32/picorv32.v --top picorv32_axi -Mdir $(dir $@)
endif

$(OBJS): $(STATIC_LIBS)
# Compilation patterns
$(OBJ_DIR)/%.o: src/%.c
	@echo + CC $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(SO_CFLAGS) -c -o $@ $<


# Compilation patterns
$(OBJ_DIR)/%.o: src/%.cc
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(SO_CFLAGS) -c -o $@ $<


# Depencies
-include $(OBJS:.o=.d)

# Some convenient rules

.PHONY: app run gdb clean run-env $(DIFF_REF_SO)
app: $(BINARY)

override ARGS ?= --log=$(BUILD_DIR)/nemu-log.txt
override ARGS += --diff=$(DIFF_REF_SO)

# Command to execute NEMU
IMG :=
NEMU_EXEC := $(BINARY) $(ARGS) $(IMG)

$(BINARY): $(OBJS)
#	$(call git_commit, "compile")
	@echo + LD $@
	@$(CC) -g -rdynamic $(SO_LDLAGS) -o $@ $^ $(STATIC_LIBS) $(LD_LIBS)

run-env: $(BINARY) $(DIFF_REF_SO)

run: run-env
	$(call git_commit, "run")
	$(NEMU_EXEC)

gdb: run-env
	$(call git_commit, "gdb")
	gdb -s $(BINARY) --args $(NEMU_EXEC)

$(DIFF_REF_SO):
	$(MAKE) -C $(DIFF_REF_PATH) $(MKFLAGS)

clean:
	-rm -rf $(BUILD_DIR)
	$(MAKE) -C tools/gen-expr clean
	$(MAKE) -C tools/qemu-diff clean
	$(MAKE) -C tools/kvm-diff clean
