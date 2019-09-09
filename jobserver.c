#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>

#include "socket.h"
#include "jobprotocol.h"


#define QUEUE_LENGTH 5
#define MAX_CLIENTS 20

#ifndef JOBS_DIR
    #define JOBS_DIR "jobs/"
#endif

// Global list of jobs
JobList job_list;

// Flag to keep track of SIGINT received
int sigint_received;
static int c = 1;
int no_write = 0;

/* SIGINT handler:
 * We are just raising the sigint_received flag here. Our program will
 * periodically check to see if this flag has been raised, and any necessary
 * work will be done in main() and/or the helper functions. Write your signal
 * handlers with care, to avoid issues with async-signal-safety.
 */
int list_jobaz(JobList * jobs, char *list){
  int counter = 0;
 if (jobs->first != NULL){
   strcat(list, "[SERVER]");
   JobNode *node = jobs->first;
   while (counter < jobs->count){
     char pid[BUFSIZE];
     sprintf(pid, "%d", node->pid);
     strcat(list, " ");
     strcat(list, pid);
     if (counter +1 < jobs->count){
       node = node->next;
     }
     counter += 1;
   }
 }
 else{
   strcpy(list, "[SERVER] No currently running jobs");
   return 0;
 }
 return 0;
}
void sigint_handler(int code) {
  printf("[SERVER] Shutting down\n");
    c = 0;
}

// TODO: SIGCHLD (child stopped or terminated) handler: mark jobs as dead
void sigchld_handler(int code){

}

/*
 *  Client management
 */

/* Accept a connection and adds them to list of clients.
 * Return the new client's file descriptor or -1 on error.
 */
int setup_new_client(int listen_fd, Client *clients){
  //Structure used from lab11
  int user_index = 0;
  while (user_index < MAX_CLIENTS && clients[user_index].socket_fd != -1) {
      user_index++;
  }

  int client_fd = accept_connection(listen_fd);
  if (client_fd < 0) {
      return -1;
  }

  if (user_index >= MAX_CLIENTS) {
      fprintf(stderr, "server: max concurrent connections\n");
      close(client_fd);
      return -1;
  }
  clients[user_index].socket_fd = client_fd;
  //clients[user_index].buffer = malloc(sizeof(struct job_buffer));
  return client_fd;
}

/* Frees up all memory and exits.
 */
void clean_exit(int listen_fd, Client *clients, JobList *job_list, int exit_status){
  empty_job_list(job_list);
}

int read_from(int client_index, Client* clients, JobList *jobs) {
      no_write = 0;
      int fd = clients[client_index].socket_fd;
      int nbytes = read(fd, clients[client_index].buffer.buf, BUFSIZE);
      clients[client_index].buffer.inbuf += nbytes;

      int where = find_network_newline(clients[client_index].buffer.buf, clients[client_index].buffer.inbuf);
      clients[client_index].buffer.consumed = where-2;

      remove_newline(clients[client_index].buffer.buf, clients[client_index].buffer.inbuf);

      int buf_len = strlen(clients[client_index].buffer.buf);

      char delim[] = " ";
      char temp[BUFSIZE+1];
      strcpy(temp, clients[client_index].buffer.buf);
      temp[strlen(temp)] = '\0';
      char *ptr = strtok(temp, delim);
    if (ptr != NULL){
      if (get_job_command(ptr) == 0){
        printf("[CLIENT %d] %s\n", clients[client_index].socket_fd, ptr);
        char list[BUFSIZE+1];
        list_jobaz(jobs, list);
        strcpy(clients[client_index].buffer.buf, list);
        strcat(clients[client_index].buffer.buf, "\r\n\0");
        strcpy(list, "");
        buf_len = strlen(clients[client_index].buffer.buf);
      }
      else if (get_job_command(ptr) == 1){
        int counter = 0;
        char name[BUFSIZE+1];
        char *arguments[BUFSIZE+1];
        char output[BUFSIZE+1];
        strcpy(output, ptr);
        while (ptr != NULL){
          ptr = strtok(NULL, delim);
          if (ptr != NULL){
            strcat(output, " ");
            strcat(output, ptr);
            if (counter == 0){
              strcat(name, ptr);
            }
            else{
              arguments[counter] = ptr;
              strcat(arguments[counter], "\0");
            }
            counter += 1;
          }
        }
        printf("[CLIENT %d] %s\n", clients[client_index].socket_fd, output);
        char exe_file[BUFSIZE];
        snprintf(exe_file, BUFSIZE, "%s/%s", JOBS_DIR, name);
        arguments[0] = exe_file;
        arguments[counter] = NULL;
        JobNode *job;
        if (jobs->count + 1 <= MAX_JOBS){
          job = start_job(exe_file, arguments, clients[client_index].socket_fd);
          if (job != NULL){
            char num[BUFSIZE+1];
            sprintf(num, "%d", job->pid);
            strcpy(clients[client_index].buffer.buf, "[SERVER] Job ");
            strcat(clients[client_index].buffer.buf, num);
            strcat(clients[client_index].buffer.buf, " created");
            strcat(clients[client_index].buffer.buf, "\r\n\0");
            buf_len = strlen(clients[client_index].buffer.buf);
            strcpy(num, "");
            strcpy(output, "\0");
            add_job(jobs, job);
          }
        }
        else{
          strcpy(clients[client_index].buffer.buf, "[SERVER] MAXJOBS exceeded");
          strcat(clients[client_index].buffer.buf, "\r\n\0");
        }
        free(job);
      }
      else if (get_job_command(ptr) == 2){
        char out[BUFSIZE];
        strcpy(out, ptr);
        ptr = strtok(NULL, delim);
        strcat(out, " ");
        strcat(out, ptr);
        printf("[CLIENT %d] %s\n", clients[client_index].socket_fd, out);
        strcpy(out, "\0");
        if (remove_job(jobs, strtol(ptr, NULL, 10)) == -1){
          strcpy(clients[client_index].buffer.buf, "");
          strcat(clients[client_index].buffer.buf, "[SERVER] Job ");
          strcat(clients[client_index].buffer.buf, ptr);
          strcat(clients[client_index].buffer.buf, " not found");
          strcat(clients[client_index].buffer.buf, "\r\n\0");
          buf_len = strlen(clients[client_index].buffer.buf);
         }
         else{
           no_write = 1;
         }
      }
      else if (get_job_command(ptr) == 3){
        printf("[CLIENT %d] watch\n", clients[client_index].socket_fd);
      }
      else if (get_job_command(ptr) == 4){
        clients[client_index].socket_fd = -1;
        return fd;
      }
      else if (get_job_command(ptr) == -1){
        strcpy(clients[client_index].buffer.buf, "[SERVER] Invalid command:");
        while (ptr != NULL){
          strcat(clients[client_index].buffer.buf, " ");
          strcat(clients[client_index].buffer.buf, ptr);
          ptr = strtok(NULL, delim);
        }
        strcat(clients[client_index].buffer.buf, "\r\n\0");
        buf_len = strlen(clients[client_index].buffer.buf);
      }
    }
    if (no_write == 0){
      write(fd, clients[client_index].buffer.buf,  buf_len);
    }
    if (nbytes == 0) {
      clients[client_index].socket_fd = -1;
      return fd;
    }
    strcpy(clients[client_index].buffer.buf, "\0");
    return 0;
}

int main(void) {
  /* Here is a snippet of code to create the name of an executable
     * to execute:
     *
     * char exe_file[BUFSIZE];
     * snprintf(exe_file, BUFSIZE, "%s/%s", JOBS_DIR, <job_name>);
     */

    // Reset SIGINT received flag.
    sigint_received = 0;

    // This line causes stdout and stderr not to be buffered.
    // Don't change this! Necessary for autotesting.
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // TODO: Set up SIGCHLD handler
    //signal(SIGCHLD, sigint_handler);
    // TODO: Set up SIGINT handler
    //signal(SIGINT, sigint_handler);
    struct sigaction exiting;
    exiting.sa_handler = sigint_handler;
    exiting.sa_flags = 0;
    sigemptyset(&exiting.sa_mask);
    sigaction(SIGINT, &exiting, NULL);
    signal(SIGINT, sigint_handler);
    // TODO: Set up server socket
    struct sockaddr_in *self = init_server_addr(PORT);
    int sock_fd = setup_server_socket(self, QUEUE_LENGTH);

    // TODO: Initialize client tracking structure (array list)
    Client clients[MAX_CLIENTS];

    // TODO: Initialize job tracking structure (linked list)
    JobList jobs;
    jobs.count = 0;
    jobs.first = NULL;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket_fd = -1;
    }
    // TODO: Set up fd set(s) that we want to pass to select()
    int max_fd = sock_fd;
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);


    while (c) {
	     // Use select to wait on fds, also perform any necessary checks
	      // for errors or received signals
        listen_fds = all_fds;
        if (select(max_fd + 1, &listen_fds, NULL, NULL, NULL) < 0) {
            perror("server: select");
            exit(1);
        }
        // Accept incoming connections
        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = setup_new_client(sock_fd, clients);
            if (client_fd < 0) {
                continue;
            }
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");
        }
        // Check our job pipes, update max_fd if we got children
        // Check on all the connected clients, process any requests
	// or deal with any dead connections etc.
      for (int index = 0; index < MAX_CLIENTS; index++) {
          if (clients[index].socket_fd > -1 && FD_ISSET(clients[index].socket_fd, &listen_fds)) {
              // Note: never reduces max_fd
              int client_closed = read_from(index, clients, &jobs);
              if (client_closed > 0) {
                  FD_CLR(client_closed, &all_fds);
                  close(client_closed);
                  printf("Client %d disconnected\n", client_closed);
              }
          }
      }
    }
    free(self);
    close(sock_fd);
    return 0;
}
