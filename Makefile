all: httpd

httpd: httpd.c
	gcc -W -Wall -lpthread -o httpd httpd.c

httpclient: httpclient.c
	gcc -W -Wall -o httpclient httpclient.c

clean:
	rm httpd httpclient
