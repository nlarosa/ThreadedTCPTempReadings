
Siddharth Saraph
Nick LaRosa
10/31/13

File Listing:

	client/
	client/therm.c
	client/Makefile
	server/
	server/thermd.c
	server/Makefile
	README.txt
	thermcron.txt

Project 2 client README.txt:
Client Summary:
  For project 2, the client code is located in the client directory. The
therm.c client uses the newgo.c code provided to read data from up to two
temperature sensors on the host machine. It reads sensor configuration data
from the /etc/t_client/client.conf file to determine the number of sensors
attached to the host, and the acceptable range of temperature readings.
After reading sensor data, this data along with the low and high temperature
range values, the number of devices attached to the host, the device number,
the hostname, an action field, and a timestamp are placed into a struct and
sent to the thermd.c server. Any errors are written to an error log file
/var/log/therm/error/g14_error_log.
  The action field corresponds to whether the client is sending just a data
packet, or a data+request status packet. Action is set to 0 in the first case,
and 1 in the second case. Data is recorded by the server in either case, but
in the request status case, the client receives status data back from the
server after transmitting its temperature data. If the temperature status is
normal, the client program closes the connection with the server and exits.
If an overtemp status is received from the server, the client writes a
corresponding message to the error file before exiting. Ideally, if we had
root permissions, we could start a machine shutdown in the even of an
overtemp.

Server Summary:
  The server is implemented via TCP. The main thread begins by opening a 
socket through which to listen for new client connections. Next, the socket 
options are set to allow for port reuse through the setsockopt() function. 
Binding to our assigned port, the main thread then issues a call to listen(), 
establishing the maximum number of outstanding connections in the socket's 
listen queue. Entering an infinite loop, the server waits for incoming client 
connections through a call to accept(), which creates a new socket between 
the server and client once a connection is recieved. At this point, a thread 
is created, and the most recently created socket is sent as an argument to 
the threaded function, in addition to the current thread count. After creating 
the thread, the main thread mutexes the thread count and increments it, and 
continues this process as long as this thread count is less than a maximum 
amount (set to 1024 in our case). After the creation of each new thread, the 
thread count is again compared against the maximum amount (while mutexed), 
continuing the thread creation. If the thread count indeed exceeds the maximum, 
the main thread issues a conditional wait, waiting in the while loop until a 
thread simultaneously decrements (under a mutex) and closes. This allows the 
server to create a client thread right as a client thread ends, ensuring that 
the maximum amount of threads is always in use (if there are more client 
connection requests than open threads). The client thread function handles 
receiving the packet data from a single client by calling recv() for each 
expected packet struct member, and loops until it has received all packets
that the client promised to send (through an initial integer transmission). 
At the end of receiving the packets, the server proceeds to write to a 
specific file name, derived from the retrieved hostname and timestamp. The 
file pointer written to is mutexed, guaranteeing that the same file is not 
written to simutaneously by multiple threads. Implementing the first extra 
credit option, the client thread function checks at each packet for an action 
field with integer 1; if a status is requested via an action 1, the server 
stores the readings to the files (as normal), but also sends a status string 
indicating whether or not the thermometer data at any packet exceeds the 
upper bound value. 

Usage:
-After compiling the client using the makefile, start the client as follows:

 ./therm <server_ip_address> <server_port_num> <integer_action>

-The action field should be 1 for data+request status packets, or 0 for just
 data packets

NOTE: Since we have attempted extra credit, the crontab file provided has a 1
for the action field. If a user wishes not to use request status packets, the
crontab should be editted accordingly.

NOTE: If the server and client are running on the same host, use the 127.0.0.1
