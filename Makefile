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

all: $(LIB) primes tcpproxy testdelay httpload

$(OFILES): taskimpl.h task.h 386-ucontext.h power-ucontext.h

AS=gcc -m32 -c
CC=gcc -m32
CFLAGS=-Wall -c -I. -ggdb -O0

%.o: %.S
	$(AS) $*.S

%.o: %.c
	$(CC) $(CFLAGS) $*.c

$(LIB): $(OFILES)
	ar rvc $(LIB) $(OFILES)

primes: primes.o $(LIB)
	$(CC) -o primes primes.o $(LIB)

tcpproxy: tcpproxy.o $(LIB)
	$(CC) -o tcpproxy tcpproxy.o $(LIB) $(TCPLIBS)

httpload: httpload.o $(LIB)
	$(CC) -o httpload httpload.o $(LIB)

testdelay: testdelay.o $(LIB)
	$(CC) -o testdelay testdelay.o $(LIB)

testdelay1: testdelay1.o $(LIB)
	$(CC) -o testdelay1 testdelay1.o $(LIB)

clean:
	rm -f *.o primes tcpproxy testdelay testdelay1 httpload $(LIB)

install: $(LIB)
	cp $(LIB) /usr/local/lib
	cp task.h /usr/local/include

