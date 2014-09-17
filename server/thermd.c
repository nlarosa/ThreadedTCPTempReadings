
/*

Nicholas LaRosa
Siddharth Saraph
CSE 30264, Project 2, Server (Multi-threaded)

usage: ./thermd 

*/

#include <sys/socket.h>
#include <sys/time.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#define THERMS 2
#define THREADS 1024
#define BUFFER 1024

pthread_mutex_t atMutex = PTHREAD_MUTEX_INITIALIZER;		// mutex will lock activeThreads variable
pthread_mutex_t fpMutex = PTHREAD_MUTEX_INITIALIZER;		// mutex will lock file pointer (one write at a time)

pthread_cond_t atCond = PTHREAD_COND_INITIALIZER;		// allows the main thread to wait for changes to activeThreads

//static int activeThreads;		// number of threads currently active
//FILE * writeFile;			// file that all threads will write to

struct thermStruct
{
	char hostName[32];		// host sending thermometer data
	int numTherms;			// number of thermometers attached to this host
	int sensorNum;			// /dev/gotemp (sensor 0), /dev/gotemp2 (sensor 1)
	double sensorData;
	double lowValue;
	double highValue;
	char timeStamp[32];		
	int action;			// send (0), request (1)	
};

struct varsNeeded
{
	int socket;			// socket between server and client
	int * activeThreads;		// pointer to number of threads active
};

void * readClient( void * arg )			// receiving the socket between server and client, receive data from client and write to file
{
	int socket = (( struct varsNeeded * )arg)->socket;			// dereference the arguments
	int * activeThreads = (( struct varsNeeded * )arg)->activeThreads;

	struct thermStruct currPkt;		// array of packets - accept up to set amount
	char packetCount, numReadings;		// keep track of how many thermometer readings have been received	
	int packetCountInt, numReadingsInt;
	int inputBytes, outputBytes;
	
	//char logString[30];			// string to be written to the file
	//char fileName[30];			// with this name
	char tempString[ 10 ];			// this string holds the data for a single reading, and date strings
	char readingString[ THERMS * 10 ];	// this is an intermediate string to hold thermometer readings

	char recvLine[ 1024 ];
	int filenameSize;
	char filename[ 70 ];
	char logString[ 32 ];

	char statusString[ 128 ];		// client can request a status - which is an error if data > upper bound

	//printf( "Hello from thread %u.\n", pthread_self() );

	/*
	if( read( socket, recvLine, BUFFER ) <= 0 )                		// first recieve the filename size (two bytes) and the filename
                {
                        perror( "Server - recv() error" );
                        exit( 1 );
                }

                memcpy( &filenameSize, recvLine, sizeof( uint16_t ) );          // store the first two bytes as a 16-bit integer

                filenameSize = ntohs( filenameSize );                           // convert this to host long

                printf( "Filename size is %d bytes.\n", filenameSize );
                
               	recvLine[ sizeof( uint16_t ) + filenameSize ] = '\0';           // and then store those next amount of bytes as the filename
               
                filename = &recvLine[ sizeof( uint16_t ) ];
               
		printf( "File name is %s.\n", filename );
	*/
	
	//sleep( 10 );
	
	if( recv( socket, &packetCount, sizeof(char), 0 ) <= 0 )			// first receive the number of packets
	{
		perror( "Server - recv() error with packet count" );
		exit( 1 );
	}

	//printf( "There are %c packets incoming...\n", packetCount );

	packetCountInt = atoi( &packetCount );
	numReadingsInt = atoi( &numReadings );
	
	while( numReadingsInt < packetCountInt )		// continue to read until we have as many thermometer readings as thermometers attached to client
	{
		if( ( inputBytes = recv( socket, &currPkt.hostName[0], 32*sizeof(char), 0 ) ) <= 0 )	// first receive the the hostname
		{
			perror( "Server - recv() error with host name" );
			exit( 1 );
		}
		//printf( "%d bytes received.\n", inputBytes );
		currPkt.hostName[ inputBytes ] = 0;

		//printf( "Hostname: %s\n", currPkt.hostName );
	
		if( recv( socket, &(currPkt.numTherms), sizeof(int), 0 ) <= 0 )			// receive the number of thermometers
		{
			perror( "Server - recv() error with number of thermometers" );
			exit( 1 );
		}		

		//printf( "Number of therms: %d\n", currPkt.numTherms );
	
		if( recv( socket, &(currPkt.sensorNum), sizeof(int), 0 ) <= 0 )			// receive the current thermometer
		{
			perror( "Server - recv() error with thermometer number" );
			exit( 1 );
		}

		//printf( "This is sensor number %d\n", currPkt.sensorNum );

		if( recv( socket, &(currPkt.sensorData), sizeof(double), 0 ) <= 0 )		// receive the number of thermometers
		{
			perror( "Server - recv() error with sensor data" );
			exit( 1 );
		}

		//printf( "Data: %2.2f\n", currPkt.sensorData );

		if( recv( socket, &(currPkt.lowValue), sizeof(double), 0 ) <= 0 )			// receive the number of thermometers
		{
			perror( "Server - recv() error with low value" );
			exit( 1 );
		}

		//printf( "Low Threshold: %2.2f\n", currPkt.lowValue );

		if( recv( socket, &(currPkt.highValue), sizeof(double), 0 ) <= 0 )			// receive the number of thermometers
		{
			perror( "Server - recv() error with high value" );
			exit( 1 );
		}

		//printf( "High Threshold: %2.2f\n", currPkt.highValue );

		if( ( inputBytes = recv( socket, currPkt.timeStamp, 32*sizeof(char), 0 ) ) <= 0 )	// receive the timestamp
		{
			perror( "Server - recv() error with timestamp" );
			exit( 1 );
		}
		currPkt.timeStamp[ inputBytes ] = 0;
		//printf( "Timestamp: %s\n", currPkt.timeStamp );

		if( recv( socket, &(currPkt.action), sizeof(int), 0 ) <= 0 )			// receive the current action ( store at 0, send status at 1 )
		{
			perror( "Server - recv() error with action" );
			exit( 1 );
		}

		//printf( "Action: %d\n", currPkt.action );

		sprintf( tempString, " %2.2f", currPkt.sensorData );				// construct the string of data
		strcat( readingString, tempString );

		numReadingsInt++;
		
		if( currPkt.action == 1 )							// send a status report
		{
			if( currPkt.sensorData > currPkt.highValue )
			{
				sprintf( statusString, "Error - data from sensor %d received on %s greater than upper value bound.", currPkt.sensorNum, currPkt.timeStamp );
			
				if( outputBytes = write( socket, statusString, 128*sizeof(char) ) <= 0 )
				{
					perror( "Server - sendto() error with status message." );
					exit( 1 );
				}
			}
			else
			{
				sprintf( statusString, "Success - data from sensor %d is normal.", currPkt.sensorNum );
				
				if( outputBytes = write( socket, statusString, 64*sizeof(char) ) <= 0 )
				{
					perror( "Server - sendto() error with status message." );
					exit( 1 );
				}
			}	
		}
	}
	
	strcat( filename, "/var/log/therm/temp_logs/g14_" );
	memcpy( tempString, &(currPkt.timeStamp[20]), 4 );	// retrieve the year
	tempString[4] = 0;
	strcat( filename, tempString );
	strcat( logString, tempString );

	memcpy( tempString, &(currPkt.timeStamp[4]), 2 );	// retrieve the month
	tempString[2] = 0;
	strcat( filename, "_" );
	strcat( filename, tempString );
	strcat( logString, " " );
	strcat( logString, tempString );

	strcat( filename, "_" );				// append the hostname, file path is complete
	strcat( filename, currPkt.hostName );

	memcpy( tempString, &(currPkt.timeStamp[8]), 2 );	// retrieve the day
	tempString[2] = 0;
	strcat( logString, " " );
	strcat( logString, tempString );

	memcpy( tempString, &(currPkt.timeStamp[11]), 2 );	// retrieve the hour
	tempString[2] = 0;
	strcat( logString, " " );
	strcat( logString, tempString );

	memcpy( tempString, &(currPkt.timeStamp[14]), 2 );	// retrieve the minute
	tempString[2] = 0;
	strcat( logString, " " );
	strcat( logString, tempString );

	sprintf( logString, "%s%s", logString, readingString );	// append the data string to the log string 

	pthread_mutex_lock( &fpMutex );		// lock the file pointer
	//printf( "FP Mutex locked at thread %u.\n", pthread_self() );

	//printf( "Writing to file at thread %u.\n", pthread_self() );
	FILE * writeFile = fopen( filename, "a" );
	fprintf( writeFile, "%s\n", logString );
	fclose( writeFile );

	pthread_mutex_unlock( &fpMutex );	// unlock the file pointer
	//printf( "FP Mutex unlocked at thread %u.\n", pthread_self() );

	pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
	//printf( "AT Mutex locked at thread %u.\n", pthread_self() );
	(*activeThreads)--;			// decrement activeThreads right before exit
	//printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
	pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
	pthread_mutex_unlock( &atMutex );	// unlock our mutex
	//printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	

	if( close( socket ) != 0 )		// close the socket
	{
		printf( "Server - socket closing failed!\n" );
	}

	pthread_exit( NULL );
}

int main( int argc, char** argv )
{
	int sockListen, sockConnect[ THREADS ];		// array will hold socket identifiers of all clients
	int enableOptions, activeThreads, sockNumber;	// keep track of how many threads are active, which socket index is next
	//int inputBytes, bytes, outputBytes, totalBytes;
	struct sockaddr_in serverAddress;
	//struct sockaddr_in * intermediate;
	struct sockaddr_storage clientAddress;
	//struct addrinfo *hostInfo, *p;
	socklen_t socketSize;
	//unsigned char * ipAddress;
	pthread_t threads[ THREADS ];	

	activeThreads = 0;
	enableOptions = 1;

	//struct timeval startTimer;		// keep structs for recording timestamp at start and end of transfer
	//struct tm * startTimeLocal;
	//struct timeval endTimer;
	//struct tm * endTimeLocal;	

	if( argc != 1 )
	{
		printf("\nusage: thermd\n\n");
		exit( 1 );
	}
	
	if( ( sockListen = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Server - socket() error" );
		exit( 1 );
	}
	
	if( ( setsockopt( sockListen, SOL_SOCKET, SO_REUSEADDR, &enableOptions, sizeof(int) ) ) < 0 )
	{
		perror( "Server - setsockopt() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( 9775 );					// and set server port

	if( bind( sockListen, ( struct sockaddr * )&serverAddress, sizeof( struct sockaddr_in) ) < 0 )
	{
		perror( "Server - bind() error" );
		exit( 1 );
	}
		
	if( listen( sockListen, 1024 ) < 0 ) 					// listen to up to 1024 connections
	{
		perror( "Server - listen() error" );
		exit( 1 );
    	}

	//prevActive = 0;
	sockNumber = 0;

	pthread_mutex_lock( &atMutex );
	activeThreads = 0;							// initialize thread count
	pthread_mutex_unlock( &atMutex );

	while( 1 )								// continue until server is ended
	{
		pthread_mutex_lock( &atMutex );
		while( activeThreads < THREADS )
		{
			pthread_mutex_unlock( &atMutex );			// unlock mutex after comparison		
	
			socketSize = sizeof( clientAddress );
	
			if ( ( sockConnect[sockNumber] = accept( sockListen, ( struct sockaddr * )&clientAddress, &socketSize ) ) < 0 )	// accept new connection
			{
				perror( "Server - accept() error" );
				continue;
			}

			pthread_mutex_lock( &atMutex );
			struct varsNeeded threadArg;
			threadArg.socket = sockConnect[sockNumber];
			threadArg.activeThreads = &activeThreads;	
			pthread_mutex_unlock( &atMutex );		

			if( pthread_create( &threads[sockNumber], NULL, readClient, ( void * )&threadArg ) != 0 )
			{
				perror( "Server - pthread_create() error" );
				continue;
			} 
		
			pthread_mutex_lock( &atMutex );
			//prevActive = activeThreads;
			activeThreads++;					// increment thread counter
			//printf( "Starting thread. There are now %d threads active.\n", activeThreads );
			pthread_mutex_unlock( &atMutex );

			sockNumber = ( sockNumber + 1 ) % THREADS;		// array index for sockey
		}

		pthread_mutex_lock( &atMutex );
		pthread_cond_wait( &atCond, &atMutex );				// wait to start any more threads until we get the signal
		pthread_mutex_unlock( &atMutex );				// unlock our mutex
	
		/*								
		intermediate = ( struct sockaddr_in * )&clientAddress;
		ipAddress = ( unsigned char * )&intermediate->sin_addr.s_addr;

		willAccept = 0;							// determines if we should connect to the IP

		if( ipAddress[0] == 127 )
		{
			willAccept = 1;
		}
		else if( ipAddress[0] == 129 && ipAddress[1] == 74 )
		{
			willAccept = 1;
		}
		else if( ipAddress[0] == 192 && ipAddress[1] == 168 )
		{
			willAccept = 1;
		}

		if( !willAccept )
		{
			printf( "\nClients must be from localhost or private networks.\n" );
			close( sockConnect );
			continue;
		}	
		*/		
	}

	if( close( sockListen ) != 0 )					// close the socket
	{
		printf( "Server - sockfd closing failed!\n" );
	}		

	pthread_exit( NULL );

	return 0;
}

