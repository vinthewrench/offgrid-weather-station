CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra
LIBS=-lcurl

SRC=src/feeder_wu.cpp
OBJ=$(SRC:.cpp=.o)
TARGET=feeder_wu

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

clean:
	rm -f $(OBJ) $(TARGET)
