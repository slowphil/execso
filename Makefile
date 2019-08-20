CFLAGS ?= -std=c99 -O2 -Wall -Wextra
LDFLAGS += -s

LIB = exec.so
EXEC_TEST = exec_test
ENV_TEST = env_test

all: $(LIB)

clean:
	-rm -f $(LIB) $(EXEC_TEST) $(ENV_TEST) *.o

$(LIB): exec.o env.o
	$(CC) -shared $(LDFLAGS) -o $@ $^ -ldl

exec.o env.o: CFLAGS += -fPIC

$(EXEC_TEST): CFLAGS += -DEXEC_TEST
$(EXEC_TEST): exec.c env.c
	$(CC) -o $@ $(CFLAGS) $^ -ldl

$(ENV_TEST): CFLAGS += -DENV_TEST
$(ENV_TEST): env.c
	$(CC) -o $@ $(CFLAGS) $^

test: $(EXEC_TEST) $(ENV_TEST)
	./$(ENV_TEST)
	./$(EXEC_TEST)

.PHONY: all clean test
