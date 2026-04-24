PROGS?=		lsbom mkbom ditto

CFLAGS?=	-g -O2
WARNFLAGS?=	-Wall -Wextra -Wformat
COMPILE_FLAGS=	${CPPFLAGS} ${CFLAGS} ${WARNFLAGS}
LINK_FLAGS=	${LDFLAGS}
LDADD?=		-framework CoreFoundation -F/System/Library/PrivateFrameworks -framework Bom

.PHONY: all clean show-config

all: ${PROGS}

show-config:
	@printf 'PROGS=%s\n' '${PROGS}'
	@printf 'CC=%s\n' '${CC}'
	@printf 'CPPFLAGS=%s\n' '${CPPFLAGS}'
	@printf 'CFLAGS=%s\n' '${CFLAGS}'
	@printf 'LDFLAGS=%s\n' '${LDFLAGS}'
	@printf 'LDADD=%s\n' '${LDADD}'

lsbom: lsbom.o
	${CC} ${LINK_FLAGS} -o $@ lsbom.o ${LDADD}

mkbom: mkbom.o
	${CC} ${LINK_FLAGS} -o $@ mkbom.o ${LDADD}

ditto: ditto.o
	${CC} ${LINK_FLAGS} -o $@ ditto.o ${LDADD}

lsbom.o: lsbom.c Bom.h
	${CC} ${COMPILE_FLAGS} -c lsbom.c -o $@

mkbom.o: mkbom.c Bom.h
	${CC} ${COMPILE_FLAGS} -c mkbom.c -o $@

ditto.o: ditto.c Bom.h
	${CC} ${COMPILE_FLAGS} -c ditto.c -o $@

clean:
	rm -f lsbom mkbom ditto lsbom.o mkbom.o ditto.o
