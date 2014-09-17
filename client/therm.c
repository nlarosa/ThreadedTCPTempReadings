/* Go! Temp reader
* written by Jeff Sadowski <jeff.sadowski@gmail.com>
* with information gathered from David L. Vernier
* and Greg KH This Program is Under the terms of the 
* GPL http://www.gnu.org/copyleft/gpl.html
* Any questions feel free to email me :-)
*/

// Siddharth Saraph
// Nicholas LaRosa
// October 31st, 2013

#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <sys/time.h>
#include <time.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#define DEBUG 0
#define PERROR ptr_errorlog
#define SBUF_SIZE 1024

void exit(int);

struct packet {
	unsigned char measurements;
	unsigned char counter;
	int16_t measurement0;
	int16_t measurement1;
	int16_t measurement2; 
	};

struct temperature {
  char name[32];
  int numdevs;
  int sensornum;
  double data;
  double threshlow;
  double threshhigh;
  char timestamp[32]; 
  int action;
};

/* Function to convert Celsius to Fahrenheit*/
float CtoF(float C){return (C*9.0/5.0)+32;}

int main(int argc,char *argv[])
{
  //temperature device configuration file 
  char tconf_filename[26] = "/etc/t_client/client.conf";
  struct stat conf_file;
  int rc_stat;
  //used to read from configuration file
  FILE *ptr_conf;
  int numdevs;
  char numdevs_c;
  double low_s0, high_s0, low_s1, high_s1;
  struct temperature sense0, sense1;
  //used for making data packets into data+request status packets
  int action;

  //used for setting up connection to server
  int ipvalid, portno;
  struct addrinfo hints, *servinfo, *p;
  int sockfd, connectfd;

  //used for transmitting data
  char sendbuf[SBUF_SIZE];
  int nbytes_sent;
  int nbytes_rec;
  int sendrc;

  //used to fill name field of struct temperature
  char hostname[32];
  //struct hostent *h;

  //client error log file
  char errorlog[64] = "/var/log/therm/error/g14_error_log";
  FILE *ptr_errorlog;

  //used for computing data transmission time
  //struct timeval tv;
  //struct tm *tm;
  time_t now;
  char timebuf[32];
  char monthbuf[4];

  //this stuff is from newgo.c
  char *fileName="/dev/gotemp";
  char *fileName2="/dev/gotemp2";
  struct stat buf, buf2;
  struct packet temp, temp2;
  float conversion=0.0078125;
  int fd, fd2;

  //temp variables
  int i, n;
  int status_size;

  //initialize hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  //initialize other variables
  memset(&sense0, 0, sizeof(sense0));
  memset(&sense1, 0, sizeof(sense1));
  memset(monthbuf, 0, 4);
  memset(sendbuf, 0, SBUF_SIZE);
  hostname[31] ='\0';
  nbytes_sent = 0;
  nbytes_rec = 0;


  if (DEBUG) printf("sizeof structs: %d %d\n", sizeof(sense0), sizeof(sense1));

  ptr_errorlog = fopen(errorlog, "a");
  if (ptr_errorlog == NULL){
    fprintf(stderr, "client: couldn't open /var/log/therm/error/g14_error_log\n");
    exit(1);
  }

  //check usage
  if (argc < 4){
    fprintf(PERROR, "usage:./therm <server_ip_addr> <port_no> <integer_action>\n");
    exit(1);
  }

  action = atoi(argv[3]);
  if (DEBUG) printf("action input is: %d\n", action);
  if ((action != 0) && (action != 1)){
    fprintf(PERROR, "usage: action must be 0 for data, or 1 for data+status\n");
    exit(1);
  }

  //check valid port
  portno = atoi(argv[2]);
  if ((portno < 1) || (portno > 65535)){
    fprintf(PERROR, "client: invalid port number\n");
    exit(1);
  }

  //check valid ip address, populate servinfo
  ipvalid = getaddrinfo(argv[1], argv[2], &hints, &servinfo);
  if (ipvalid != 0){
    fprintf(PERROR, "client: getaddrinfo: %s\n", gai_strerror(ipvalid));
    exit(1);
  }

  //check if /etc/t_client/client.conf exists
  rc_stat = stat(tconf_filename, &conf_file);
  if (rc_stat == -1){
    fprintf(PERROR, "client: couldn't find /etc/t_client/client.conf\n");
    exit(1);
  }

  //open /etc/t_client/client.conf
  ptr_conf = fopen(tconf_filename, "r");
  if (ptr_conf == NULL){
    fprintf(PERROR, "client: couldn't open /etc/t_client/client.conf\n");
    exit(1);
  }

  //find number of devices connected to host
  numdevs_c = fgetc(ptr_conf);
  if (numdevs_c == EOF){
    fprintf(PERROR, "couldn't read from /etc/t_client/client.conf\n");
    exit(1);    
  }
  
  //exit if there are 0 devices attached to host
  if (numdevs_c == '0') {fprintf(PERROR, "client: no temperature devices connected to host; exiting\n");exit(1);}

  else if (numdevs_c == '1'){
    fscanf(ptr_conf, " %lf %lf", &low_s0, &high_s0);
    if (DEBUG) printf("%3.2lf %3.2lf\n",low_s0, high_s0);
  }

  else if (numdevs_c == '2') {
    fscanf(ptr_conf, " %lf %lf %lf %lf", &low_s0, &high_s0, &low_s1, &high_s1);
    if (DEBUG) printf("%3.2lf %3.2lf %3.2lf %3.2lf\n",low_s0, high_s0, low_s1, high_s1);
  }
  
  //exit if there are more devices than expected
  else {
    fprintf(PERROR, "client: more devices than expected; exiting\n");
    exit(1);
  }

  fclose(ptr_conf);
  //start preparing sendbuf, add numdevs_c to sendbuf
  memcpy(sendbuf, &numdevs_c, 1); 

  //this gets the hostname without the domain name
  gethostname(hostname, 31);
  //This allows me to get the fully qualified hostname of the current host
  //However, note that this method is deprecated
  /*
  h =  gethostbyname(hostname);
  if (h == NULL){
    fprintf(PERROR, "client: couldn't gethostbyname\n");
    exit(1);
  }
*/

  if (DEBUG) printf("number of devices: %c\n", numdevs_c);

/* I got this number from the GoIO_SDK and it matched 
   what David L. Vernier got from his Engineer */

  numdevs = atoi(&numdevs_c);

  if (numdevs >= 1)
  {
  //populate sense0
  if (DEBUG) printf("%s\n", hostname);
  memcpy(sense0.name, hostname, strlen(hostname)); 
  sense0.numdevs = numdevs;
  sense0.sensornum = 0;
  sense0.threshlow = low_s0;
  sense0.threshhigh = high_s0;
  sense0.action = 1;
  if(stat( fileName, &buf ))
  {
     if(mknod(fileName,S_IFCHR|S_IRUSR|S_IWUSR|S_IRGRP |S_IWGRP|S_IROTH|S_IWOTH,makedev(180,176)))
     {
        fprintf(PERROR,"client: Cannot creat device %s  need to be root",fileName);
	exit(1);
     }
  }


  /* If cannot open, check permissions on dev, and see if it is plugged in */
  
  if((fd=open(fileName,O_RDONLY))==-1)
  {
     fprintf(PERROR,"client: Could not read %s\n",fileName);
     exit(1);
  }
  
  /* if cannot read, check is it plugged in */
  
  if(read(fd,&temp,sizeof(temp))!=8)
  {
     fprintf(PERROR,"client: Error reading %s\n",fileName);
     exit(1);
  }
/* 
  gettimeofday(&tv, NULL);
  tm = localtime(&tv.tv_sec);
  sprintf(sense0.timestamp, "%d:%02d:%02d", tm->tm_hour, tm->tm_min,
				tm->tm_sec);
*/
  time(&now);
  //note ctime returns a null-terminated string
  sprintf(timebuf, "%s", ctime(&now));
  i = strlen(timebuf);
  //overwrite '\n' character appended by ctime
  timebuf[i-1] = '\0';
  //convert month to number
  memcpy(monthbuf, timebuf+4, 3);
  if (strncmp(monthbuf, "Jan", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '1';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Feb", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '2';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Mar", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '3';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Apr", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '4';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "May", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '5';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Jun", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '6';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Jul", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '7';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Aug", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '8';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Sep", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '9';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Oct", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '0';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Nov", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '1';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Dec", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '2';
    timebuf[6] = ' '; 
  }
  else {
    fprintf(PERROR, "client: timestamp error\n");
    exit(1);
  }
  sprintf(sense0.timestamp, "%s", timebuf);

  sense0.data = CtoF(((float)temp.measurement0)*conversion);

  //Fill the sendbuf with the members of the first struct
  //Ignore endianness of doubles, ints, etc for now
  memcpy(sendbuf+1, sense0.name, 32);
  memcpy(sendbuf+33, &sense0.numdevs, 4);
  memcpy(sendbuf+37, &sense0.sensornum, 4);
  memcpy(sendbuf+41, &sense0.data, 8);
  memcpy(sendbuf+49, &sense0.threshlow, 8);
  memcpy(sendbuf+57, &sense0.threshhigh, 8);
  memcpy(sendbuf+65, sense0.timestamp, 32);
  memcpy(sendbuf+97, &sense0.action, 4);

  if (DEBUG){ 
    printf("***struct temperature sense0***\n");
    printf("name: %s\n", sense0.name); 
    printf("numdevs: %d\n", sense0.numdevs); 
    printf("sensor#: %d\n", sense0.sensornum); 
    printf("data: %3.2lf\n", sense0.data); 
    printf("threshlow: %3.2lf\n", sense0.threshlow); 
    printf("threshhigh: %3.2lf\n", sense0.threshhigh); 
    printf("timestamp: %s\n", sense0.timestamp);
    printf("action: %d\n", sense0.action); 
  }
  close(fd);
  }

  if (numdevs >= 2)
  {
  //populate sense1
  memcpy(sense1.name, hostname, strlen(hostname)); 
  sense1.numdevs = numdevs;
  sense1.sensornum = 1;
  sense1.threshlow = low_s0;
  sense1.threshhigh = high_s0;
  sense1.action = 1;
  if(stat( fileName2, &buf2 ))
  {
     if(mknod(fileName2,S_IFCHR|S_IRUSR|S_IWUSR|S_IRGRP |S_IWGRP|S_IROTH|S_IWOTH,makedev(180,177)))
     {
        fprintf(PERROR,"Cannot creat device %s  need to be root",fileName2);
	exit(1);
     }
  }

  if((fd2=open(fileName2,O_RDONLY))==-1)
  {
     fprintf(PERROR,"Could not read %s\n",fileName2);
     exit(1);
  }

  if(read(fd2,&temp2,sizeof(temp))!=8)
  {
     fprintf(PERROR,"Error reading %s\n",fileName2);
     exit(1);
  }

  /*
  gettimeofday(&tv, NULL);
  tm = localtime(&tv.tv_sec);
  sprintf(sense1.timestamp, "%d:%02d:%02d", tm->tm_hour, tm->tm_min,
				tm->tm_sec);
  */
  time(&now);
  //note ctime returns a null-terminated string
  sprintf(timebuf, "%s", ctime(&now));
  i = strlen(timebuf);
  //overwrite '\n' character appended by ctime
  timebuf[i-1] = '\0';
  //convert month to number
  memcpy(monthbuf, timebuf+4, 3);
  if (strncmp(monthbuf, "Jan", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '1';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Feb", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '2';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Mar", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '3';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Apr", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '4';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "May", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '5';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Jun", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '6';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Jul", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '7';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Aug", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '8';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Sep", 3) == 0){
    timebuf[4] = '0';
    timebuf[5] = '9';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Oct", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '0';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Nov", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '1';
    timebuf[6] = ' '; 
  }
  else if (strncmp(monthbuf, "Dec", 3) == 0){
    timebuf[4] = '1';
    timebuf[5] = '2';
    timebuf[6] = ' '; 
  }
  else {
    fprintf(PERROR, "client: timestamp error\n");
    exit(1);
  }
  sprintf(sense1.timestamp, "%s", timebuf);

  sense1.data = CtoF(((float)temp2.measurement0)*conversion);
  //Fill the sendbuf with the members of the second struct
  //Ignore endianness of doubles, ints, etc for now
  memcpy(sendbuf+101, sense1.name, 32);
  memcpy(sendbuf+133, &sense1.numdevs, 4);
  memcpy(sendbuf+137, &sense1.sensornum, 4);
  memcpy(sendbuf+141, &sense1.data, 8);
  memcpy(sendbuf+149, &sense1.threshlow, 8);
  memcpy(sendbuf+157, &sense1.threshhigh, 8);
  memcpy(sendbuf+165, sense1.timestamp, 32);
  memcpy(sendbuf+197, &sense1.action, 4);
  if (DEBUG){ 
    printf("***struct temperature sense1***\n");
    printf("name: %s\n", sense1.name); 
    printf("numdevs: %d\n", sense1.numdevs); 
    printf("sensor#: %d\n", sense1.sensornum); 
    printf("data: %3.2lf\n", sense1.data); 
    printf("threshlow: %3.2lf\n", sense1.threshlow); 
    printf("threshhigh: %3.2lf\n", sense1.threshhigh); 
    printf("timestamp: %s\n", sense1.timestamp);
    printf("action: %d\n", sense1.action); 
  }
  close(fd2);
  }

  //connect to server on appropriate port
  //this for loop calls socket and sets up a connection
  //with the server
  for(p = servinfo; p != NULL; p = p->ai_next){
    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd < 0){
      fprintf(PERROR, "client: couldn't get socket file descriptor\n");
      continue;
    }

    connectfd = connect(sockfd, p->ai_addr, p->ai_addrlen);
    if (connectfd < 0){
      fprintf(PERROR, "client: couldn't connect to server\n");
      continue;
    }
    if (DEBUG) printf("connected\n");
    break;
  }

  if(p == NULL){
    fprintf(PERROR, "error: exhausted structs addrinfo's, couldn't connect to server\n");
    exit(1);
  }
 
  freeaddrinfo(servinfo);

  if (DEBUG){
    i = 0;
    printf("First char of sendbuf is: %c\n", sendbuf[0]); 
    for(i = 0; i < 32; i = i + 1){
      //printf("Hi");
      printf("%c", sendbuf[1+i]);
    }
    printf("\n");
  }
  //send 201 bytes of sendbuf to server
  while (nbytes_sent < 201){
    sendrc = send(sockfd, sendbuf+nbytes_sent, 201-nbytes_sent, 0);
    if (sendrc < 0){
      fprintf(PERROR, "client: error sending data\n");
      exit(1);
    } 
    nbytes_sent = nbytes_sent + sendrc;
  }
  if (DEBUG) printf("nbytes_sent: %d\n", nbytes_sent);
  
  status_size = 128;
  if (action == 1){
    i = 0;
    //the server sends a null-terminated sequence of characters.
    //reusing sendbuf
    do{
      nbytes_rec = recv(sockfd, sendbuf + i*status_size, status_size, 0);
      if (DEBUG) printf("nbytes_rec: %d\n", nbytes_rec); 
      if (nbytes_rec < 0){
        fprintf(PERROR, "client: error receiving status data\n");
        exit(1);
      }
      i = i + 1;
    } while (i < numdevs);
    i = 0;
    do{
      //only print to error file if there is an overtemp
      if (sendbuf[0 + i*status_size] == 'E'){
	//print status message with timestamp to errorfile
	fprintf(PERROR, "%s\n", sendbuf+(i*status_size));
      }
      if (DEBUG) printf("%s\n", sendbuf+(i*status_size));
      i = i + 1;
    } while (i < numdevs);
  }
  
  fclose(ptr_errorlog);
  if (DEBUG) {printf("Session complete.\n");}
  close(sockfd);

  return 0;
}

