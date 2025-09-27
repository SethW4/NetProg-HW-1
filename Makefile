CC = gcc
CFLAGS = -Wall -Wextra -O2 # show all warnings and balanced optimization
TARGET = tftp.out
SRC = hw1.c
INCLUDE_DIR = ../unpv13e-master/lib

.PHONY: all clean # prevent file naming conflicts

all: $(TARGET) # only build all when tftp.out is created

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o