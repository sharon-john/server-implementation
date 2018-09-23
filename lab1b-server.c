#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <assert.h>
#include <netinet/in.h>
#include "zlib.h"

//Lab1A constructs for shell processing

int to_child_pipe[2];           //read from parent
int from_child_pipe[2];         //write to shell
pid_t child_pid= 0;            //child is assigned to be 0, keep track of processes this way
struct termios prevState;       //struct to save terminal modes from most recent state
int shell=1;                    //global variable to check for shell status
// int socket=1;

char comp_buff[1024];     //compression buffer
char decomp_buff[1024];   //decompression buffer
char read_buffer[1024];
int sockfd, newsockfd, portnum;
unsigned int client;
struct sockaddr_in serv_addr, clientaddress;
int n;

//Complex Unix socket API used from
//https://www.tutorialspoint.com/unix_sockets/socket_server_example.htm

void ServerSocket()
{
    /* First call to socket() function */
   sockfd = socket(AF_INET, SOCK_STREAM, 0);

   if (sockfd < 0) {
      perror("ERROR opening socket");
      exit(1);
   }

   /* Initialize socket structure */

   memset((char *) &serv_addr, 0, sizeof(serv_addr));

   //random number generator to create port in the range >1024.

   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = INADDR_ANY;
   serv_addr.sin_port = htons(portnum);

   /* Now bind the host address using bind() call.*/

   if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
      perror("ERROR on binding");
      exit(1);
   }

   /* Now start listening for the clients, here process will
      * go in sleep mode and will wait for the incoming connection
   */

   listen(sockfd,5);                  //only one client at a time connecting
   client = sizeof(clientaddress);    //size of client address

   /* Accept actual connection from the client */
   newsockfd = accept(sockfd, (struct sockaddr *)&clientaddress, &client);

   if (newsockfd < 0) {
      perror("ERROR on accept");
      exit(1);
   }

}

void output_to_client(int dest, char *buffer, size_t bytes)
{
  size_t i=0;
  while (i < bytes){
    /*  if (buffer==decomp_buff)
      {
        char Arr[1]={};
        if (buffer[i]== '\r' || buffer[i] == '\n'){
        Arr[0]='\n';
        if (write(dest, Arr, sizeof(Arr)) < 0)
          {
            fprintf(stderr, "ERROR WRITING: %s\n", strerror(errno));
          }
        }
      }
    */
      if ((int)(buffer[i]) == 3){
          if (shell){
              if (kill(child_pid, SIGINT) < 0){
                  fprintf(stderr, "ERROR KILLING: %s\r\n", strerror(errno));
              }
          }
      }
      else if ((int)(buffer[i]) == 4){
          if (shell){
              if (close(to_child_pipe[1]) < 0){
                  fprintf(stderr, "ERROR CLOSING: %s\r\n", strerror(errno));
              }
          /*    if (ofd==1)   //if EOF from shell pipes, exit immediately
              {
                exit(0);
              }
        */
        }
      }
      else {
          if (write(dest, buffer + i, 1) < 0){
              fprintf(stderr, "ERROR WRITING: %s\r\n", strerror(errno));
          }
      }
    i++;
  }
}

int _compress(char* buffer, size_t bytes)
{
  int Compression_Size= sizeof(comp_buff);

  z_stream CStream;    //z_CStream object

  CStream.zalloc = Z_NULL;
  CStream.zfree = Z_NULL;
  CStream.opaque = Z_NULL;

  deflateInit(&CStream, Z_DEFAULT_COMPRESSION);

//  if (deflateInit_ != Z_OK)
//  {
//    fprintf(stderr, "ERROR INITIALISING DEFLATE");
//    exit(1);
//  }

  CStream.avail_in = bytes;
  CStream.next_in = (unsigned char*) buffer;
  CStream.avail_out = Compression_Size;
  CStream.next_out = (unsigned char*) comp_buff;

  do {
      deflate(&CStream, Z_SYNC_FLUSH);
  }
  while (CStream.avail_in > 0);

  int NewBytes;
  NewBytes= (Compression_Size - CStream.avail_out);

  deflateEnd(&CStream);

  return NewBytes;
}

int _decompress(char* buffer, size_t bytes)
{
  int Decompression_Size= sizeof(decomp_buff);

  z_stream DStream;       //z_stream DStream object

  DStream.zalloc = Z_NULL;
  DStream.zfree = Z_NULL;
  DStream.opaque = Z_NULL;

  inflateInit(&DStream);

  DStream.avail_in = bytes;
  DStream.next_in = (unsigned char*) buffer;
  DStream.avail_out = Decompression_Size;
  DStream.next_out = (unsigned char*) decomp_buff;

  do {
     inflate(&DStream, Z_SYNC_FLUSH);
  }

  while (DStream.avail_in > 0);

  int NewBytes = (Decompression_Size- DStream.avail_out);

  inflateEnd(&DStream);

  return NewBytes;
}


void signal_handler(int num)
{
    if (num==SIGPIPE)
    {
        exit(0);
    }
}

void ExitHandler()
{
    int ex=0;
    int status= waitpid(child_pid, &ex, 0);
    if (status < 0)
    {
      fprintf(stderr, "WAITPID ERROR: %s \n", strerror(errno));

    }
    fprintf(stderr, "EXIT SIGNAL=%d AND STATUS=%d\n", WTERMSIG(ex), ex>>8);   //WEXITSTATUS was avoided due to TA recommendation, ex right shifted by 8 bits instead
}


int main(int argc, char* argv[])
{
  static struct option longopts[] = {
      {"port", required_argument, NULL, 'p'},
      {"compress", no_argument, NULL, 'c'},
      {0,0,0,0}
    };

//  int fd=0;
  int arg=0;
  int compress=0;

  while ((arg=getopt_long(argc, argv, "p:c", longopts, NULL)) != -1)
  {
    switch (arg) {
        case 'p':
            portnum=atoi(optarg);
            break;
        case 'c':
            compress = 1;
            break;
        default:
            fprintf(stderr, "Usage: server --port=[PORT] [--compress] \r\n");
            exit(1);
    }
  }

  if (!portnum)
  {
    fprintf(stderr, "Incorrect Usage.\n");
    exit(1);
  }

  atexit(ExitHandler);

  ServerSocket();    //creates and sets up server socket and client

  pipe(to_child_pipe);
  pipe(from_child_pipe);

  child_pid =fork();

  if(child_pid < 0){
      fprintf(stderr, "FORK FAILED: %s", strerror(errno));
  }

  if (child_pid!= 0)               //Parent process -> server
  {
      int checker=0;
      checker= close(to_child_pipe[0]);
  if (checker <0)
  {
      fprintf(stderr, "PIPE CLOSE FAILED: %s\n", strerror(errno));
  }

     checker=close(from_child_pipe[1]);
    if (checker <0)
    {
      fprintf(stderr, "PIPE CLOSE FAILED: %s\n", strerror(errno));
    }

    struct pollfd polling[2];                     //poller boilerplate

      polling[0].fd = newsockfd;
      polling[0].events = POLLIN | POLLHUP | POLLERR;
      polling[0].revents = 0;

      polling[1].fd = from_child_pipe[0];
      polling[1].events = POLLIN | POLLHUP | POLLERR;
      polling[1].revents = 0;

      while(1)
      {
          if (poll(polling, 2, 0) < 0)
          {
            fprintf(stderr, "POLL FAILED: %s\n", strerror(errno));
            exit(1);
          }

          if (polling[0].revents & POLLIN)
          {
            size_t bytes = read(newsockfd, read_buffer, 6400);

            if (bytes <=0)
            {
              fprintf(stderr, "READ FAILED: %s\n", strerror(errno));
              exit(1);
            }

            if(compress)
            {
              size_t result=_decompress(read_buffer, bytes);
              size_t i=0;
              ssize_t write_to_shell;
              while (i < result)
              {
                        if (decomp_buff[i]== '\r' || decomp_buff[i] == '\n'){
                        char Arr[1]={'\n'};
                        write_to_shell = write(to_child_pipe[1], Arr, sizeof(Arr));
                        if (write_to_shell < 0)  {
                            fprintf(stderr, "ERROR WRITING: %s\n", strerror(errno));
                          }
                        }
                        else if ((int)decomp_buff[i] == 3){    //for ^C
                            if (shell)
                            {
                                write_to_shell = kill(child_pid, SIGINT);
                                if (write_to_shell < 0){
                                    fprintf(stderr, "ERROR KILLING: %s\n", strerror(errno));
                                }
                            }
                        }

                        else if ((int)decomp_buff[i] == 4){    //for EOF, ^D
                            if (shell)
                            {
                                write_to_shell= close(to_child_pipe[1]);
                                if (write_to_shell < 0){
                                    fprintf(stderr, "ERROR CLOSING: %s\n", strerror(errno));
                                }
                            }
                            exit(0);
                        }

                        else
                        {
                            write_to_shell= write(to_child_pipe[1], decomp_buff + i, 1);
                            if (write_to_shell < 0)
                            {
                                fprintf(stderr, "ERROR WRITING: %s\n", strerror(errno));
                            }
                        }
                        i++;
              }
            }

            else
            {

              // write(STDOUT_FILENO, read_buffer, bytes);
              output_to_client(to_child_pipe[1], read_buffer, bytes);
            }

          }

          if (polling[1].revents & POLLIN)
          {
            size_t bytes2 = read(from_child_pipe[0], read_buffer, 6400);

            if (bytes2 <=0 )
            {
              fprintf(stderr, "READ FAILED: %s\n", strerror(errno));
              exit(1);
            }

            if (compress)
            {
              int result2= _compress(read_buffer, bytes2);
              write(newsockfd, comp_buff, result2);
            }
            else
            {
              output_to_client(newsockfd, read_buffer, bytes2);
            }
          }

          if(polling[1].revents & (POLLHUP | POLLERR))
          {
            int ret = close(to_child_pipe[1]);
            if (ret<0) {
              fprintf(stderr, "PIPE CLOSE FAILED: %s\n", strerror(errno));
              exit(1);
            }
            break;
          }

      }

  }

  if (child_pid ==0)      //child process -> SHELL IS FORKED
  {
    int ret = dup2(to_child_pipe[0], STDIN_FILENO);

    if (ret==-1){
        fprintf(stderr, "DUP FAILED: %s", strerror(errno));
    }

    ret = dup2(from_child_pipe[1], STDOUT_FILENO);
    if (ret==-1){
        fprintf(stderr, "DUP FAILED: %s", strerror(errno));
    }

    close(to_child_pipe[0]);
    close(from_child_pipe[1]);

    char  **a = NULL;

    if (execvp("/bin/bash", a) == -1 )
    {
      fprintf(stderr, "EXECVP() FAILED: %s\n", strerror(errno));
      exit(1);
    }
  }

  return 0;
}
