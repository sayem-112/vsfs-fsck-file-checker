CC      = gcc
CFLAGS  = -Wall -std=c99 -O2
TARGET  = vsfsck

$(TARGET): src/fsck.c
	$(CC) $(CFLAGS) -o $@ src/fsck.c

clean:
	rm -f $(TARGET)

.PHONY: clean
