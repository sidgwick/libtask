LIB=libtask.a
TCPLIBS=

ASM=asm.o
OFILES=\
	$(ASM)\
	channel.o\
	context.o\
	fd.o\
	net.o\
	print.o\
	qlock.o\
	rendez.o\
	task.o\

all: $(LIB) primes tcpproxy testdelay

$(OFILES): taskimpl.h task.h 386-ucontext.h

AS=gcc -c -g -m32
CC=gcc -g -m32
CFLAGS=-Wall -c -I. -ggdb
LDFLAGS=-z noexecstack

%.o: %.S
	$(AS) $*.S

%.o: %.c
	$(CC) $(CFLAGS) $*.c

$(LIB): $(OFILES)
	ar rvc $(LIB) $(OFILES)

primes: primes.o $(LIB)
	$(CC) $(LDFLAGS) -o primes primes.o $(LIB)

tcpproxy: tcpproxy.o $(LIB)
	$(CC) $(LDFLAGS) -o tcpproxy tcpproxy.o $(LIB) $(TCPLIBS)

httpload: httpload.o $(LIB)
	$(CC) $(LDFLAGS) -o httpload httpload.o $(LIB)

testdelay: testdelay.o $(LIB)
	$(CC) $(LDFLAGS) -o testdelay testdelay.o $(LIB)

testdelay1: testdelay1.o $(LIB)
	$(CC) $(LDFLAGS) -o testdelay1 testdelay1.o $(LIB)

clean:
	rm -f asm.s *.o primes tcpproxy testdelay testdelay1 httpload $(LIB)

install: $(LIB)
	cp $(LIB) /usr/local/lib
	cp task.h /usr/local/include

