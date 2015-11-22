
compile: server.c
	gcc -o espeak-ipa-server server.c -lpthread -lespeak -lmicrohttpd
