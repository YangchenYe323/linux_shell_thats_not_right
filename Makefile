src = $(wildcard ./*.c)
obj = $(patsubst %.c, %.o, $(src))
target = sshell
CC = gcc
CFLAGS = -Werror -Wextra -Wall

$(target): $(obj)
	$(CC) $(obj) -o $(target)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(obj) $(target)

exec:
	./$(target)
