all: server client

# Amazon Linux 2 currently has gcc 7.3.0, which means it only supports C++17
client: crc.c
	g++ -g -w -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -o crc crc.c -lpthread

server: crsd.c
	g++ -g -w -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -o crsd crsd.c -lpthread

clean:
	rm crsd crc
