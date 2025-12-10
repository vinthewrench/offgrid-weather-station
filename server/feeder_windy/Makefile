CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra
LIBS=-lcurl

SRC=src/feeder_windy.cpp
OBJ=$(SRC:.cpp=.o)
TARGET=feeder_windy

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

clean:
	rm -f $(OBJ) $(TARGET)
