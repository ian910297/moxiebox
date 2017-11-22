EXEC = write

CFLAGS = -Wall -std=gnu99

all: $(EXEC)

%: %.c
	$(CC) -o $@ $^ $(CFLAGS)

gen: $(EXEC)
	for i in `seq 1 0x200000`; do \
		./write -i 1 -o test.in -a; \
	done
	for i in `seq 1 0x200000`; do \
		./write -i 0 -o test.in -a; \
	done

clean:
	$(RM) *.o $(EXEC)
