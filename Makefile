#sudo apt-get install pkg-config
#sudo apt-get install libasound2-dev

CFLAGS+= `pkg-config --cflags alsa`
CFLAGS+= -Wall -Werror
#CFLAGS+= -ggdb
CFLAGS+= -Os

LDFLAGS+= -L/usr/X11R6/lib

LDLIBS+= `pkg-config --libs alsa`
LDLIBS+= -lX11 -lm
LDLIBS+= -lpthread

dbaudio2: dbaudio2.o dbx.o
	gcc $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	@-rm -f dbaudio2 *.o
