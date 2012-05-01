#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

void error(char * msg){
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

int catch_signal(int sig, void (*handler)(int)) {
  struct sigaction action;
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  return sigaction (sig, &action, NULL); 
}

int open_listener_socket() {
  int s = socket(PF_INET, SOCK_STREAM, 0);
  if (s == -1)
    error("Can't open socket");
  return s;
}

int listener_d;

void bind_to_port(int socket, int port) {
  struct sockaddr_in name;
  name.sin_family = PF_INET;
  name.sin_port = (in_port_t)htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);

  int reuse = 1;
  if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, 
        (char *)&reuse, sizeof(int)) == -1) {
    error("Can't set the reuse option on the socket");
  }
  int c = bind (socket, (struct sockaddr *) &name, sizeof(name));
  if (c == -1)
    error("Can't bind to socket"); 
}

int say(int socket, char *s) {
  int result = send(socket, s, strlen(s), 0);
  if (result == -1){
    fprintf(stderr, "%s: %s\n", "Error talking to the client", 
        strerror(errno));
  } 
  return result;
}

void handle_shutdown(int sig) {
  if (listener_d)
    close(listener_d);
  fprintf(stderr, "Bye!\n");
  exit(0);
}

int read_in(int socket, char *buf, int buf_len) {
  int remain_len = buf_len;
  int num_chars = recv(socket, buf, buf_len, 0); 

  //recv() doesn't always get all the input in one call
  //so have to loop through and check if there's more
  //to get
  while ((num_chars > 0) && (buf[num_chars-1] != '\n')) {
    buf += num_chars;
    remain_len -= num_chars;
    num_chars = recv(socket, buf, remain_len, 0); 
  }

  //error
  if (num_chars < 0)
    return num_chars;
  //if empty, just set string to empty
  else if (num_chars == 0) 
    buf[0] = '\0';  
  //add terminating char to end of string
  else 
    buf[num_chars-2]='\0';

  return buf_len - remain_len;
}

int main(int argc, char * argv[]){
  if (catch_signal(SIGINT, handle_shutdown) == -1){
    error("Can't set the interrupt handler");
  }

  listener_d = open_listener_socket();
  bind_to_port(listener_d, 30000);

  if (listen(listener_d, 10) == -1){
    error("Can't listen");
  }
  puts("Waiting for connection");

  struct sockaddr_storage client_addr;
  unsigned int address_size = sizeof(client_addr);
  char buf[255];

  FILE * f;
  //if file's are bigger than 256 bytes, they're just outta luck
  char * file = malloc(256);

  while(1){
    int connect_d = accept(listener_d, 
                           (struct sockaddr *)&client_addr,
                           &address_size);
    if (connect_d == -1){
      error("Can't open secondary socket");
    }

    //handle multpile connections with new procs
    if (!fork()){
      //if we're here, we're a child proc, so close the listener
      //b/c we only ever send from this point on
      close(listener_d);

      if (say(connect_d, "Specify file:\r\n") != -1){
        read_in(connect_d, buf, sizeof(buf));
        f = fopen(buf, "r");
        if (f != NULL){
          fscanf (f, "%255c", file);
          say(connect_d, "=== File contents: ===\n");
          say(connect_d, file);
          say(connect_d, "======================\n");
        } else {
          say(connect_d, "Can't find file.\r\n");
        }
      }

      //close the connection and exit, the child proc's work
      //is done
      close(connect_d);
      exit(0);
    }
    close(connect_d);
  }
  return 0;
}
