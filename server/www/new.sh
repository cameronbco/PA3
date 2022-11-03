#!/bin/bash
(echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.1; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.05; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.025; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.0125; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.00625; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; 
sleep 0.003125; 
echo -en "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"
) | nc 127.0.0.1 5555 > test.txt
