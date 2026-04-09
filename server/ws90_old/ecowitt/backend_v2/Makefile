# No version field.

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
INCLUDES = -Isrc
LIBS     = -lm -lcurl -lsqlite3 -lmicrohttpd

SRCS = \
    src/main.cpp \
    src/api_v2.cpp \
    src/http_server.cpp \
    src/state_v2.cpp \
    src/astro.cpp \
    src/config.cpp \
    src/utils.cpp \
    src/lunar.cpp \
    src/sunset.cpp \
    src/SolarTimeMgr.cpp

OBJS = $(SRCS:src/%.cpp=obj/%.o)

TARGET = ecowitt_backend_v2

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

obj/%.o: src/%.cpp
	mkdir -p obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf obj $(TARGET)
