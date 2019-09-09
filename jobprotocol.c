// TODO: Use this file for helper functions (especially those you want available
// to both executables.

/* Example: Something like the function below might be useful

   // Find and return the location of the first newline character in a string
   // First argument is a string, second argument is the length of the string
   int find_newline(const char *buf, int len);

*/

#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#ifndef __JOB_PROTOCOL_H__
#define __JOB_PROTOCOL_H__

#ifndef PORT
  #define PORT 55555
#endif

#ifndef MAX_JOBS
    #define MAX_JOBS 32
#endif

// No paths or lines may be larger than the BUFSIZE below
#define BUFSIZE 256

#define CMD_INVALID -1
typedef enum {CMD_LISTJOBS, CMD_RUNJOB, CMD_KILLJOB, CMD_WATCHJOB, CMD_EXIT} JobCommand;
//static const int n_job_commands = 5;
// See here for explanation of enums in C: https://www.geeksforgeeks.org/enumeration-enum-c/

typedef enum {NEWLINE_CRLF, NEWLINE_LF} NewlineType;

#define PIPE_READ 0
#define PIPE_WRITE 1

struct job_buffer {
	char buf[BUFSIZE];
	int consumed;
	int inbuf;
};
typedef struct job_buffer Buffer;

struct client {
	int socket_fd;
	struct job_buffer buffer;
};
typedef struct client Client;

struct watcher_node {
	int client_fd;
	struct watcher_node *next;
};
typedef struct watcher_node WatcherNode;

struct watcher_list {
	struct watcher_node* first;
	int count;
};
typedef struct watcher_list WatcherList;

struct job_node {
	int pid;
	int stdout_fd;
	int stderr_fd;
	int dead;
	int wait_status;
	struct job_buffer stdout_buffer;
	struct job_buffer stderr_buffer;
	struct watcher_list watcher_list;
	struct job_node* next;
};
typedef struct job_node JobNode;


struct job_list {
	struct job_node* first;
	int count;
};
typedef struct job_list JobList;

void shift_buffer(Buffer *);
/* Returns the specific JobCommand enum value related to the
 * input str. Returns CMD_INVALID if no match is found.
 */
JobCommand get_job_command(char* in){
  if(strcmp(in, "jobs") == 0){
    return CMD_LISTJOBS;
  }
  else if(strcmp(in, "run") == 0){
    return CMD_RUNJOB;
  }
  else if(strcmp(in, "kill") == 0){
    return CMD_KILLJOB;
  }
  else if(strcmp(in, "watch") == 0){
    return CMD_WATCHJOB;
  }
  else if(strcmp(in, "exit") == 0){
    return CMD_EXIT;
  }
  else{
    return CMD_INVALID;
  }
}

/* Forks the process and launches a job executable. Allocates a
 * JobNode containing PID, stdout and stderr pipes, and returns
 * it. Returns NULL if the JobNode could not be created.
 */
int remove_newline(char*, int);
int find_network_newline(const char*, int);
JobNode* start_job(char * job, char * const args[], int client_fd){
  int fd[2];
  JobNode *node = malloc(sizeof(struct job_node));
  pipe(fd);
  int p = fork();
  if (p < 0){
    perror("fork");
    return NULL;
  }
  if (p == 0){
    if ((close(fd[0])) == -1) {
        perror("close");
    }
    //dup2(client_fd, fileno(stdout));
    //dup2(fd[1], fileno(stdout));
    execv(job, args);
    exit(0);
    close(fd[1]);
  }
  else if (p > 0){
    close(fd[1]);
    // char m[BUFSIZE];
    // char s[2];
    // int b_r;
    // m[0] = '\0';
    // while ((b_r = read(fd[0], &s, sizeof(char)) > 0)){
    //   if (s[b_r-1] != '\n'){
    //     strcat(m, s);
    //   }
    //   else{
    //     //strcat(m, "\0");
    //     //remove_newline(m, BUFSIZE);
    //     //printf("%s\n", m);
    //     // strcat(m, "\r\n\0");
    //     write(client_fd, m, strlen(m));
    //     memset(&m[0], 0, sizeof(m));
    //   }
    // }
    // while (read(fd[0], &m, BUFSIZE) > 0){
    //   remove_newline(m, BUFSIZE);
    //   printf("%s", m);
    // }
    node->pid = p;
    node->dead = -1;
    node->stdout_fd = client_fd;
    wait(&(node->wait_status));
    return node;
  }
  return NULL;
}

/* Frees all memory held by a job list and resets it.
 * Returns 0 on success, -1 otherwise.
 */
int empty_job_list(JobList* jobs){
  JobNode *node;
  if (jobs->count > 0){
      node = jobs->first;
  }
  int counter = 0;
  JobNode *temp;
  while (counter < jobs->count){
    if (counter+1 < jobs->count){
       temp = node->next;
    }
    free(node);
    node = temp;
    counter += 1;
  }
  return 0;
}

/* Adds the given job to the given list of jobs.
 * Returns 0 on success, -1 otherwise.
 */
int add_job(JobList* jobs, JobNode* job){
  int counter = 0;
  if (jobs->count + 1 > MAX_JOBS){
    return -1;
  }
  if (jobs->count == 0){
    jobs->first = job;
    jobs->count += 1;
    return 0;
  }
  JobNode *node = jobs->first;
  while (counter < jobs->count){
    if (counter+1 < jobs->count){
      node = node->next;
    }
    counter += 1;
  }
  node->next = job;
  jobs->count += 1;
  return 0;
}

int kill_job_node(JobNode *);

/* Sends SIGKILL to the given job_pid only if it is part of the given
 * job list. Returns 0 if successful, 1 if it is not found, or -1 if
 * the kill command failed.
 */
int kill_job(JobList* jobs, int job_pid){
  int counter = 0;
  if (jobs->count > 0){
    JobNode *node = jobs->first;
    while (counter < jobs->count){
      if (node->pid == job_pid){
        jobs->count -= 1;
        return kill_job_node(node);
      }
      if (counter + 1 == jobs->count){
        return 1;
      }
      node = node->next;
      counter += 1;
    }
  }
  return 1;
}

/* Sends a kill signal to the job specified by job_node.
 * Return 0 on success, 1 if job_node is NULL, or -1 on failure.
 */
int kill_job_node(JobNode *node){
  if (node == NULL){
    return 1;
  }
  int val = kill(node->pid, SIGKILL);
  return val;
}
/* Removes a job from the given job list and frees it from memory.
 * Returns 0 if successful, or -1 if not found.
 */
int remove_job(JobList* jobs, int pid){
  int counter = 0;
  JobNode *node;
  if (jobs->count > 0){
    node = jobs->first;
  }
  else{
    return -1;
  }
  if (node->pid == pid){
    if (jobs->count > 1){
      jobs->first = node->next;
      kill_job_node(node);
    }
    else{
      jobs->first = NULL;
    }
    jobs->count -= 1;
    free(node);
    return 0;
  }
  while (counter < jobs->count){
    JobNode *temp;
    if (counter + 1 < jobs->count){
      temp = node->next;
    }
    if (temp->pid == pid){
      if (counter + 2 < jobs->count){
        node->next = temp->next;
      }
      else{
        node->next = NULL;
      }
      kill_job_node(temp);
      free(temp);
      jobs->count -= 1;
      return 0;
    }
    counter += 1;
  }
  return -1;
}
/* Marks a job as dead.
 * Returns 0 on success, or -1 if not found.
 */
int mark_job_dead(JobList* jobs, int pid, int dead){
  int counter = 0;
  JobNode *node = jobs->first;
  while (counter < jobs->count){
    if (node->pid == pid){
      node->dead = dead;
      return 0;
    }
    if (node->next == NULL){
      return -1;
    }
    node = node->next;
    counter += 1;
  }
  return -1;
}

/* Frees all memory held by a job node and its children.
 */
int delete_job_node(JobNode* node){
  if (node->next != NULL){
    while (node->next != NULL){
      JobNode *temp = node->next;
      free(node);
      node = temp;
    }
    free(node);
  }
  else{
    free(node);
  }
  return 0;
}

/* Kills all jobs. Return number of jobs in list.
 */
int kill_all_jobs(JobList * jobs){
  JobNode *node = jobs->first;
  int counter = 0;
  while (0 < jobs->count){
    JobNode *temp;
    if (node->next != NULL){
      temp = node->next;
    }
    kill_job_node(node);
    node = temp;
    jobs->count -= 1;
    counter += 1;
  }
  return counter;
}

/* Adds the given watcher to the given list of watchers.
 * Returns 0 on success, -1 otherwise.
 */
int add_watcher(WatcherList* watchers, int fd){
  WatcherNode node = { .client_fd = fd, .next = NULL};
  if (watchers->count == 0){
    watchers->first = &node;
    return 0;
  }
  int counter = 0;
  WatcherNode *temp = watchers->first;
  while (counter < watchers->count){
    if (temp->next == NULL){
      temp->next = &node;
      watchers->count += 1;
      return 0;
    }
    temp = temp->next;
    counter += 1;
  }
  return -1;
}

/* Removes a watcher from the given watcher list and frees it from memory.
 * Returns 0 if successful, or 1 if not found.
 */
int remove_watcher(WatcherList* watchers, int fd){
  int counter = 0;
  WatcherNode *node = watchers->first;
  if (node->client_fd == fd){
    watchers->first = node->next;
    watchers->count -= 1;
    free(node);
    return 0;
  }
  while (counter < watchers->count){
    WatcherNode *temp;
    if (node->next != NULL){
      temp = node->next;
    }
    if (temp->client_fd == fd){
      if (temp->next != NULL){
        node->next = temp->next;
      }
      else{
        node->next = NULL;
      }
      free(temp);
      watchers->count -= 1;
      return 0;
    }
    counter += 1;
  }
  return -1;
}

/* Removes a client from every watcher list in the given job list.
 */
void remove_client_from_all_watchers(JobList* jobs, int fd){
  JobNode *node = jobs->first;
  while (node != NULL){
    WatcherList *lst = &(node->watcher_list);
    remove_watcher(lst, fd);
    node = node->next;
  }
}

/* Adds the given watcher to a given job pid.
 * Returns 0 on success, 1 if job was not found, or -1 if watcher could not
 * be allocated.
 */
int add_watcher_by_pid(JobList* jobs, int pid, int fd){
  JobNode *node = jobs->first;
  while (node != NULL){
    if (node->pid == pid){
      WatcherList *lst = &(node->watcher_list);
      return add_watcher(lst, fd);
    }
    node = node->next;
  }
  return 1;
}

/* Removes the given watcher from the list of a given job pid.
 * Returns 0 on success, 1 if job was not found, or 2 if client_fd could
 * not be found in list of watchers.
 */
int remove_watcher_by_pid(JobList* jobs, int pid, int fd){
  JobNode *node = jobs->first;
  while (node != NULL){
    if (node->pid == pid){
      WatcherList *lst = &(node->watcher_list);
      if(remove_watcher(lst, fd) == -1){
        return 2;
      }
      return 0;
    }
    node = node->next;
  }
  return 1;
}

/* Frees all memory held by a watcher list and resets it.
 * Returns 0 on success, -1 otherwise.
 */
int empty_watcher_list(WatcherList *watchers){
  WatcherNode *node = watchers->first;
  while (node->next != NULL){
    WatcherNode *temp = node->next;
    free(node);
    node = temp;
  }
  free(watchers);
  return 0;
}

/* Frees all memory held by a watcher node and its children.
 */
int delete_watcher_node(WatcherNode *node){
  if (node->next != NULL){
    while (node->next != NULL){
      WatcherNode *temp = node->next;
      free(node);
      node = temp;
    }
    free(node);
  }
  else{
    free(node);
  }
  return 0;
}

/* Replaces the first '\n' or '\r\n' found in str with a null terminator.
 * Returns the index of the new first null terminator if found, or -1 if
 * not found.
 */
int remove_newline(char *buf, int inbuf){
  for (int i = 0; i < inbuf; i++){
    if (buf[i] == '\r' && buf[i+1] == '\n'){
      buf[i] = '\0';
      return i;
    }
    if (buf[i] == '\n'){
      buf[i] = '\0';
      return i;
    }
  }
  return -1;
}

/* Replaces the first '\n' found in str with '\r', then
 * replaces the character after it with '\n'.
 * Returns 1 + index of '\n' on success, -1 if there is no newline,
 * or -2 if there's no space for a new character.
 */
int convert_to_crlf(char *buf, int inbuf){
  for (int i = 0; i < inbuf; i++){
    if (buf[i] == '\n'){
      buf[i] = '\r';
      if (i+1 < BUFSIZE){
        buf[i+1] = '\n';
        return i+2;
      }
      else{
        return -2;
      }
    }
  }
  return -1;
}

/* Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_network_newline(const char *buf, int inbuf){
  for (int i = 0; i < inbuf; i++){
    if (buf[i] == '\r' && buf[i+1] == '\n'){
      return i+2;
    }
  }
  return -1;
}

/* Search the first n characters of buf for an unix newline (\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_unix_newline(const char *buf, int inbuf){
  for (int i = 0; i < inbuf; i++){
    if (buf[i] == '\n'){
      return i+1;
    }
  }
  return -1;
}

/* Read as much as possible from file descriptor fd into the given buffer.
 * Returns number of bytes read, or 0 if fd closed, or -1 on error.
 */
int read_to_buf(int fd, Buffer* buf){
  int nbytes = read(fd, buf->buf, BUFSIZE);
  buf->inbuf += nbytes;
  int where = remove_newline(buf->buf, buf->inbuf);
  buf->consumed = where;
  return nbytes;
}

/* Returns a pointer to the next message in the buffer, sets msg_len to
 * the length of characters in the message, with the given newline type.
 * Returns NULL if no message is left.
 */
char* get_next_msg(Buffer* buf, int* msg_len, NewlineType type){
  int where;
  if (type == 0){
    convert_to_crlf(buf->buf, buf->inbuf);
    where = find_network_newline(buf->buf, buf->inbuf);
  }
  else{
    where = find_unix_newline(buf->buf, buf->inbuf);
  }
  if (where > 0){
    buf->buf[where-2] = '\0';
    int num = where-buf->inbuf-2;
    msg_len = &num;
    return buf->buf;
  }
  return NULL;
}

/* Removes consumed characters from the buffer and shifts the rest
 * to make space for new characters.
 */
void shift_buffer(Buffer *buf){
  memmove(&buf->buf, &buf->buf[buf->consumed], buf->inbuf-buf->consumed);
  buf->inbuf -= buf->consumed;
}

/* Returns 1 if buffer is full, 0 otherwise.
 */
int is_buffer_full(Buffer *buf){
  if (buf->inbuf == BUFSIZE-1){
    return 1;
  }
  return 0;
}

#endif
