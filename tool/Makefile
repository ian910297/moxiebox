EXEC = write

CFLAGS = -Wall -std=gnu99

all: $(EXEC)

%: %.c
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	$(RM) *.o $(EXEC)
