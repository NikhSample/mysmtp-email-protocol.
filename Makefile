all: mysmtp_server mysmtp_client

mysmtp_server: mysmtp_server.c
	gcc -o mysmtp_server mysmtp_server.c

mysmtp_client: mysmtp_client.c
	gcc -o mysmtp_client mysmtp_client.c

rs: mysmtp_server 
	./mysmtp_server 2525

rc: mysmtp_client
	./mysmtp_client 127.0.0.1 2525

clean:
	rm -f mysmtp_server mysmtp_client


