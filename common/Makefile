
CFLAGS	= -Wall
CLIBS	= -lrt -lm -lsqlite3
PROGRAM = ktm_drv
OBJECTS = utils.o errors.o buf.o device.o main.o \
	  serial.o tcp.o regex.o parser.o dumb.o \
	  db.o eval.o ek270.o tekon17.o spg761.o \
	  spg741.o tekon19.o vkg2.o im2300.o \
	  spg742.o

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(CLIBS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install:
	cp $(PROGRAM) $(DESTDIR)/usr/bin

clean:
	rm -f *.o $(PROGRAM)

