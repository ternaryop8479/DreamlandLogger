CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
INCLUDES = -I./include

# 源文件
SRCS = src/main.cpp \
       src/io/buffer.cpp \
       src/io/program.cpp \
       src/player_list.cpp \
       src/command_request.cpp \
       src/server_manager.cpp \
       src/web_server.cpp

# 目标文件
OBJS = $(SRCS:.cpp=.o)

# 输出文件
TARGET = dreamland_logger

# 默认目标
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# 编译规则
src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 创建必要目录
setup:
	mkdir -p data/uploads
	mkdir -p server
	@echo "请将MC服务器文件放入 server/ 目录"

# 清理
clean:
	rm -f $(OBJS) $(TARGET)

# 运行
run: $(TARGET)
	./$(TARGET) "cd server && java -jar server.jar nogui"

.PHONY: all clean setup run
