CC = g++
CFLAGS = -Wall -Wextra -O2
TARGET = aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.cpp
	$(CC) $(CFLAGS) aesdsocket.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
