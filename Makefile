override CFLAGS := -Wall -Wextra -std=c11 -g $(CFLAGS)

.PHONY: all clean install distinst format

all: mousekeys

clean:
	rm -f *.o mousekeys

install: all
	install -Z -m 644 -o root 71-mousekeys.rules --target-directory /etc/udev/rules.d/
	install -Z -m 644 -o root mousekeys@.service --target-directory /etc/systemd/system/
	install -Z -m 755 -o root mousekeys --target-directory /usr/local/libexec/
	systemctl daemon-reload

distinst: all
	install -Z -m 644 -o root 71-mousekeys.rules --target-directory /usr/lib/udev/rules.d/
	sed -e 's|/usr/local/libexec|/usr/libexec|' -i mousekeys@.service
	install -Z -m 644 -o root mousekeys@.service --target-directory /usr/lib/systemd/system/
	install -Z -m 755 -o root mousekeys --target-directory /usr/libexec/
	systemctl daemon-reload

format:
	clang-format --style=mozilla -i --sort-includes mousekeys.c


mousekeys: mousekeys.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


