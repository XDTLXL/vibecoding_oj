# VibeCoding OJ — 构建入口
#
# 目标：
#   make          构建 build/oj_server
#   make test     构建并运行单元测试（tests/unit/）
#   make clean    删除 build/
#   make help     显示本帮助
#
# 编译与链接参数依据 SPEC.md §4.1、§4.2：
#   - C++17
#   - 必须定义 CPPHTTPLIB_OPENSSL_SUPPORT，否则 httplib 不提供 SSLServer
#   - 链接 -lmysqlclient -lssl -lcrypto -lpthread

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wno-unused-parameter
CPPFLAGS ?=

DEFS     := -DCPPHTTPLIB_OPENSSL_SUPPORT
# 项目自己的头文件走 -I；vendored 的第三方单头文件（httplib.h / json.h）走 -isystem，
# 这样它们的告警不会污染本项目的编译输出（S2 验收要求 make 一次通过且无告警阻塞）
INCLUDES := -Isrc -isystem src/util
LDLIBS   := -lmysqlclient -lssl -lcrypto -lpthread

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN       := $(BUILD_DIR)/oj_server
TEST_BIN  := $(BUILD_DIR)/unit_tests

SRCS      := $(shell find src -name '*.cpp' 2>/dev/null | sort)
LIB_SRCS  := $(filter-out src/main.cpp,$(SRCS))
TEST_SRCS := $(sort $(wildcard tests/unit/*.cpp))

LIB_OBJS  := $(LIB_SRCS:%.cpp=$(OBJ_DIR)/%.o)
MAIN_OBJ  := $(OBJ_DIR)/src/main.o
TEST_OBJS := $(TEST_SRCS:%.cpp=$(OBJ_DIR)/%.o)
ALL_OBJS  := $(LIB_OBJS) $(MAIN_OBJ)

DEPS := $(ALL_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

.PHONY: all test compdb clean help

all: $(BIN)

$(BIN): $(LIB_OBJS) $(MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)
	@echo "[OK] 构建完成：$@"

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(DEFS) $(INCLUDES) -MMD -MP -c $< -o $@

test: $(TEST_BIN)
	@$(TEST_BIN)

# 生成 compile_commands.json，让 clangd / VSCode C++ 插件拿到与 Makefile 一致的编译参数。
# 不生成它的话，编辑器会把 src/main.cpp 报成「'httplib.h' file not found」等一堆假错误。
compdb:
	@tools/gen_compdb.sh "$(CURDIR)" "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(DEFS) $(INCLUDES)" \
	  "$(CURDIR)/$(OBJ_DIR)" $(SRCS) $(TEST_SRCS)

clean:
	rm -rf $(BUILD_DIR)
	@echo "[OK] 已清理 $(BUILD_DIR)/"

help:
	@echo "VibeCoding OJ 构建入口"
	@echo ""
	@echo "  make          构建 $(BIN)"
	@echo "  make test     构建并运行单元测试"
	@echo "  make compdb   生成 compile_commands.json（编辑器补全/跳转用）"
	@echo "  make clean    删除 $(BUILD_DIR)/"
	@echo "  make help     显示本帮助"
	@echo ""
	@echo "启动前的准备（详见 README.md）："
	@echo "  1. tools/gen_cert.sh              生成自签证书"
	@echo "  2. cp config/config.ini.example config/config.ini 并填写 jwt_secret"
	@echo "  3. sudo mkdir -p /tmp/oj_build && sudo chmod 1777 /tmp/oj_build"

-include $(DEPS)
