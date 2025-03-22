//standard headers required
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#include <arpa/inet.h>  //inet_ntop
#include <fcntl.h>      //file operations
#include <signal.h>     //signal
#include <syslog.h>     //syslog

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>

#include <pthread.h>    //threads
#include <sys/queue.h>  //linked list to store thread id's

#include <time.h>
#include <sys/time.h>


#define DAEMON_CODE           (1)
#define PORT_NO				        (9000)
#define PORT_BIND			        ("1234")
#define BACK_LOG			        (10)
#define BUFFER_CAPACITY       (1)

#define MULTIPLIER_FACTOR     (2)
#define TIME_BUFFER           (120)

//comment to run the normal code
#define USE_AESD_CHAR_DEVICE    (1)

#if USE_AESD_CHAR_DEVICE
  #define FILE_PATH_TO_WRITE    ("/dev/aesdchar")
#else
  #define FILE_PATH_TO_WRITE    ("/var/tmp/aesdsocketdata")
#endif

typedef struct
{
    pthread_t pt_thread;            //thread paramaters
    int thread_accept_fd;           //for storing the respective client accept fd 
    bool thread_completion_status;  //to keep track of detachment, success or not
}threadParams_t;

//Linked list node added
typedef struct slist_data_s
{
  threadParams_t thread_data;
  SLIST_ENTRY(slist_data_s) entries;
}slist_data_t;

int counter = 0;
//int file_des = 0;
int server_socket_fd = 0;
int client_accept_fd = 0;
#if USE_AESD_CHAR_DEVICE

#else
  timer_t timerid;
#endif


int g_Signal_handler_detection = 0;
pthread_mutex_t data_lock;

/* Function prototypes */
void *get_in_addr(struct sockaddr *sa);
void socket_termination_signal_handler(int signo);
void exit_handling();
void *recv_client_send_server(void *thread_parameters);

#if USE_AESD_CHAR_DEVICE

#else
static void timer_thread();
#endif


/**
* set @param result with @param ts_1 + @param ts_2
*/
static inline void timespec_add( struct timespec *result,
                        const struct timespec *ts_1, const struct timespec *ts_2)
{
    result->tv_sec = ts_1->tv_sec + ts_2->tv_sec;
    result->tv_nsec = ts_1->tv_nsec + ts_2->tv_nsec;
    if( result->tv_nsec > 1000000000L ) {
        result->tv_nsec -= 1000000000L;
        result->tv_sec ++;
    }
}

void *recv_client_send_server(void *thread_parameters)
{
#if 1
    /* file open logic */
    mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char* filename = FILE_PATH_TO_WRITE;
    int file_des = open(filename, (O_RDWR | O_CREAT), mode);
    if(file_des == -1) //file_ptr
    {
      syslog(LOG_ERR,"file creation,opening failure\n");
      perror("file creation");
      //return -1;
    }
 #endif 
    /* sig status */
    int sig_status = 0;

    threadParams_t *l_tp = (threadParams_t*) thread_parameters;
    syslog(LOG_DEBUG, "client fd rcvd is %d",l_tp->thread_accept_fd);
    
    /* Add signals to be masked */
    sigset_t x; // for each thread a signal handler
    int ret_sig_stat_1 = 0,ret_sig_stat_2 = 0,ret_sig_stat_3 = 0;
    ret_sig_stat_1 = sigemptyset(&x); 
    ret_sig_stat_2 = sigaddset(&x,SIGINT);
    ret_sig_stat_3 = sigaddset(&x,SIGTERM);
    if( (ret_sig_stat_1 == -1) || (ret_sig_stat_2 == -1) || (ret_sig_stat_3 == -1)  ) 
    {
      perror("sig signal set");
    }
   
    /* local declarations */
    int current_data_pos = 0; int no_of_bytes_rcvd = 0;
    char temp_buffer[BUFFER_CAPACITY];
    memset(temp_buffer, '\0', BUFFER_CAPACITY);
    
    char temp_read[1];
    memset(temp_read, '\0', 1);
        
    char* writer_file_buffer_ptr = NULL;
    int write_buffer_size = BUFFER_CAPACITY;
    writer_file_buffer_ptr = (char *)malloc(sizeof(char)*BUFFER_CAPACITY);
    if(writer_file_buffer_ptr == NULL)
    { 
      perror("writer_file_buffer");
      syslog(LOG_ERR,"writer_file_buffer_ptr failure\n");
    }
    memset(writer_file_buffer_ptr,'\0',BUFFER_CAPACITY);
      
    /* read */
    int curr_location = 0; 
    int read_buffer_size = BUFFER_CAPACITY;
    char *new_line_read = NULL;
    int send_status = 0;
    int exit_write_loop = 0;

    sig_status = sigprocmask(SIG_BLOCK, &x, NULL);
    if(sig_status == -1)
    {
      perror("sig_status - 1");
    }
    
    //changed logic to work only on temp_buffer from previous writer_file_buffer_ptr.
    //writer_file_buffer_ptr caused 40,000 errors as a memory beyind its scope was accessed.
    //converted to do to avoid multiple break conditions and variable handling
    //while((no_of_bytes_rcvd = (recv(client_accept_fd, writer_file_buffer_ptr + curr_location, BUFFER_CAPACITY, 0))))
    do
    {    
      no_of_bytes_rcvd = recv(client_accept_fd, temp_buffer/*writer_file_buffer_ptr + curr_location*/, sizeof(temp_buffer), 0);
      
      if(no_of_bytes_rcvd == -1)
      {
        perror("error in reception");
      }
      
      if(!no_of_bytes_rcvd || (strchr(temp_buffer, '\n') != NULL))
      {        exit_write_loop = 1;   }
         
      if((no_of_bytes_rcvd+curr_location) >= write_buffer_size)
      {
        //write_buffer_size *= MULTIPLIER_FACTOR;
        write_buffer_size += MULTIPLIER_FACTOR;
        syslog(LOG_DEBUG,"write_buffer_size = %d\n",write_buffer_size);

        char* tmpptr = (char *)realloc(writer_file_buffer_ptr, (sizeof(char) * write_buffer_size) );
        if(tmpptr == NULL)
        {
          perror("write realloc failure");
          free(writer_file_buffer_ptr); writer_file_buffer_ptr = NULL;
          free(tmpptr); tmpptr = NULL;
        }
        
        writer_file_buffer_ptr = tmpptr;
      }
      
      //seperate memory to store the data
      //the whole working is done on static buffer now
      memcpy(&writer_file_buffer_ptr[curr_location], temp_buffer, no_of_bytes_rcvd);
      curr_location += no_of_bytes_rcvd;
            
    }while(exit_write_loop == 0);
    
    exit_write_loop = 0;

    sig_status = sigprocmask(SIG_UNBLOCK, &x, NULL);
    if(sig_status == -1)
    {
      perror("sig_status - 2");
    }

    //protecting the wrte to the file - global fd
    pthread_mutex_lock(&data_lock);
    
    int ret_status = 0;
    ret_status = write(file_des,writer_file_buffer_ptr,curr_location);
    syslog(LOG_DEBUG,"return status  = %d\n",ret_status);
    if(ret_status == -1)
    {
      syslog(LOG_ERR,"file writing failure\n");
      perror("file writing");
    }
    
    counter = counter + ret_status;
    syslog(LOG_DEBUG,"counter  = %d\n",counter);
    /* present number of byts in file */
    //current_data_pos = lseek(file_des,0,SEEK_CUR);
    //syslog(LOG_DEBUG,"position is %d\n",current_data_pos);
    current_data_pos = counter;
    pthread_mutex_unlock(&data_lock);

    lseek(file_des,0,SEEK_SET);
    
    int bytes_sent = 0;
    int bytes_read = 0;
    int read_buffer_loc = 0; 
    int store_previous_new_line = 0;
    
    /* sig status */
    sig_status = sigprocmask(SIG_BLOCK, &x, NULL);
    if(sig_status == -1)
    {
      perror("sig_status - 1");
    }
    
    while(bytes_sent < current_data_pos)
    {
      //syslog(LOG_ERR,"read iteration\n");
      read_buffer_loc = 0;
        
      char* read_file_buffer_ptr = NULL; 
      read_file_buffer_ptr = (char *)malloc(sizeof(char)*BUFFER_CAPACITY);
      
      if(read_file_buffer_ptr == NULL)
      {
        perror("read malloc failure");
        syslog(LOG_DEBUG,"read alloc failure");
      }
      
      //protecting the read to the file - global fd
      pthread_mutex_lock(&data_lock);
      //Converted to do while for ease of variable handling
      //while(1)
      do
      {
        /* read one byte at a time for line check */
        //bytes_read = read(file_des,read_file_buffer_ptr + read_buffer_loc ,sizeof(char));
        bytes_read = read(file_des,temp_read /*+ read_buffer_loc*/ ,sizeof(temp_read));
        if(bytes_read == -1)
        {
          perror("bytes_read");
          syslog(LOG_DEBUG,"bytes_read ilure");
        }
        

        if(read_buffer_loc > 1)
        {
         // new_line_read = strchr(read_file_buffer_ptr,'\n');  
         new_line_read = strchr(temp_read,'\n');           
        }

        if(read_buffer_size+store_previous_new_line < (current_data_pos))
        {
          read_buffer_size = read_buffer_size + (current_data_pos-store_previous_new_line);
        
          char* tmpptr  = realloc(read_file_buffer_ptr, sizeof(char)*read_buffer_size);
          if(tmpptr == NULL)
          {
            perror("read realloc failure");
            free(read_file_buffer_ptr); read_file_buffer_ptr = NULL;
            free(tmpptr); tmpptr = NULL;
          }        
          read_file_buffer_ptr = tmpptr;
        }  
        
        memcpy(&read_file_buffer_ptr[read_buffer_loc], temp_read, bytes_read);
        /* accumulation of one by one read */
        read_buffer_loc = read_buffer_loc + bytes_read;
         
      }while(new_line_read == NULL);
       
      store_previous_new_line = read_buffer_loc;
      pthread_mutex_unlock(&data_lock);
        
      send_status = send(client_accept_fd,read_file_buffer_ptr,read_buffer_loc,0);
      //syslog(LOG_DEBUG,"send_status (send return) = %d\n",send_status);
      if(send_status == -1)
      { 
        perror("error in sending to client");
      }
      
      free(read_file_buffer_ptr);
      read_file_buffer_ptr = NULL;
      
      bytes_sent = bytes_sent + read_buffer_loc;
      //syslog(LOG_DEBUG,"bytes_sent variable value = %d\n",bytes_sent);
    }
    
    sig_status = sigprocmask(SIG_UNBLOCK, &x, NULL);
    if(sig_status == -1)
    {
      perror("sig_status - 2");
    }

    close(l_tp->thread_accept_fd);
        
    free(writer_file_buffer_ptr);
    writer_file_buffer_ptr = NULL;
    
    l_tp->thread_completion_status = true;
    close(file_des);
    return NULL;
}


#if USE_AESD_CHAR_DEVICE

#else
// https://www.tutorialspoint.com/c_standard_library/c_function_strftime.htm
// timer thread to add timestamps
// https://man7.org/linux/man-pages/man3/strftime.3.html
static void timer_thread()
{
    syslog(LOG_DEBUG, "TIMESTAMP");
    time_t rawtime;
    struct tm *info;
    char *time_stamp = (char *)malloc(TIME_BUFFER*sizeof(char));
    if(time_stamp == NULL)
    {
      perror("time_stamp failed");      
    }

    time(&rawtime);
    info = localtime(&rawtime);
    
    int ret_bytes = 0; 
    ret_bytes = strftime(time_stamp,TIME_BUFFER,"timestamp:%Y %b %a %d %H:%M:%S%n", info);
    if(ret_bytes == 0)
    {
      perror("strftime failed");
      goto timer_exit_location;
    }
    
    if(pthread_mutex_lock(&data_lock) != 0)
    {
      perror("lock not acquired");
      goto timer_exit_location;
    }
    
    /* protecting the data written to file between mutex lock and unlock */
    int bytes_written= 0;
    bytes_written = write(file_des, time_stamp, ret_bytes);
    if(bytes_written == -1)
    {
      syslog(LOG_ERR,"bytes_writtenfailure\n");
      perror("file writing");
      goto timer_exit_location;
    }
    
    if(pthread_mutex_unlock(&data_lock) != 0)
    {
      perror("lock not acquired");
      goto timer_exit_location;
    }
       
timer_exit_location:
    free(time_stamp);
    
}
#endif


/* Start of program */
int main(int argc, char *argv[])
{
  syslog(LOG_DEBUG,"-------------START OF PROGRAM-------------------");
#if DAEMON_CODE
  int set_daemon = 0;
  if(argc > 1)
  {
    if((strcmp(argv[1],"-d")) == 0)
    {
      set_daemon = 1;
    }
  }
#endif

  slist_data_t *datap = NULL;
  SLIST_HEAD(slisthead,slist_data_s)head;
  SLIST_INIT(&head);

  int ret_mutex_status;
  ret_mutex_status = pthread_mutex_init(&data_lock,NULL);
  if(ret_mutex_status != 0)
  {
    perror("pthread_mutex_init");
    return -1;
  }
  
  //daemon
  pid_t pid = 0;
  openlog(NULL, 0, LOG_USER);
   
  /* Signal handler */ // Why? Running in while loop client connection
  if(signal(SIGINT,socket_termination_signal_handler) == SIG_ERR)
  {
    perror("SIGINT registeration");
    return -1;
  }
  
  if(signal(SIGTERM,socket_termination_signal_handler) == SIG_ERR)
  {
    perror("SIGTERM registeration");
    return -1;
  }
  
  /* Socket */  
  server_socket_fd = socket(AF_INET,SOCK_STREAM,0);
  if(server_socket_fd == -1)
  {
    perror("server_socket_fd");  
    return -1; 
  }
    
  /* socketopt, Allows reuse */
  int server_setsockopt_fd = 0;
  int opt = 1;
    
  // Forcefully attaching socket to the port 9000
  server_setsockopt_fd = setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                                        &opt, sizeof(opt));
  if(server_setsockopt_fd == -1)
  {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  
  /* Bind */
  int server_bind_fd = 0;
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(PORT_NO);
  
  server_bind_fd = bind(server_socket_fd,(struct sockaddr*)&server_address,sizeof(server_address));
  if(server_bind_fd == -1)
  {
    perror("server bind fd");
    return -1;
  }
  
 #if DAEMON_CODE 
  /* Daemon creation*/
  if(set_daemon == 1)
  {
    syslog(LOG_DEBUG,"daemon\n");
    pid = fork();
    if(pid == -1)
    {
      perror("fork failed");
      return -1;
    }
    else if(pid > 0)
    {
      //parent context
      syslog(LOG_DEBUG,"CHILD PID = %d\n",pid);
      exit(EXIT_SUCCESS);
    }
    
    /* create new session and process grp*/
    if(setsid()== -1)
    {
      perror("set sid failure");
      return -1;
    }
    
    //change cd to root
    if(chdir("/") == -1)
    {
      perror("chdir");
      return -1;
    }
    
    //close all open files (in,out,error)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO); 

    open("/dev/null",O_RDWR);
    dup(0);
    dup(0);
  }
  #endif

#if USE_AESD_CHAR_DEVICE

#else

  //for both context
  struct sigevent sev;
  struct timespec start_time;
  int clock_id = CLOCK_MONOTONIC;
	struct itimerspec itimerspec;
  
  //wrte code below as post this everything will be in one context
  //writing above may likely be it in another context
  //indicatest that the timer should start only in one context
  //either parent or child (pid = 00) child and daemon = 0 (parent)
  //if((set_daemon == 0 ) || (pid == 0))
  //Since, the code is at this location. It would not matter to put a explicity check
  // as always only parent or child would execute at once.
  //If this step was above, then it is possible that in daemon mode
  //two iterations would have started one in parent and other in child
  //https://github.com/cu-ecen-aeld/aesd-lectures/blob/master/lecture9/timer_thread.c
  memset(&sev,0,sizeof(struct sigevent));
  //function for setting up timer and 
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = timer_thread;
  
  if(timer_create(clock_id,&sev,&timerid) != 0 )
  {
    perror("timer_create error");
    return -1;
  } 

  if(clock_gettime(clock_id,&start_time) != 0 )
  {
    perror("clock_gettime error");
    return -1;
  } 
  
  itimerspec.it_interval.tv_sec = 10;
  itimerspec.it_interval.tv_nsec = 1000000; //extra delay to margin
  timespec_add(&itimerspec.it_value,&start_time,&itimerspec.it_interval);
  
  if(timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL ) != 0 )
  {
    perror("timer_settime error");
    return -1;
  } 
  
#endif

  
  /* listen */
  int server_listen_fd = 0;
  server_listen_fd = listen(server_socket_fd,BACK_LOG);
  if(server_listen_fd == -1)
  {
    perror("server_listen_fd");
    return -1;
  }
  
  #if 0
  /* file open logic */
  mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
  char* filename = FILE_PATH_TO_WRITE;
  file_des = open(filename, (O_RDWR | O_CREAT), mode);
  if(file_des == -1) //file_ptr
  {
    syslog(LOG_ERR,"file creation,opening failure\n");
    perror("file creation");
    return -1;
  }
  #endif
  
  bool run_status = true;
    
  while(run_status)
  { 
    syslog(LOG_DEBUG,"run_status");
    
    /* Accept */
    socklen_t server_address_len = 0;
    server_address_len = sizeof(server_address);
    char s[INET6_ADDRSTRLEN];
    memset(&s[0],'\0',sizeof(s));
    
    client_accept_fd = accept(server_socket_fd, (struct sockaddr *)&server_address,&server_address_len);
    /* Accepted connection from client */
    inet_ntop(AF_INET,get_in_addr((struct sockaddr *)&server_address),s,sizeof(s));
    syslog(LOG_DEBUG, "Accepted connection from %s", s);
    if(client_accept_fd == -1)
    {
      if(g_Signal_handler_detection == 1)
      {
        exit_handling();
        //free the memory
        while(!SLIST_EMPTY(&head))
        {
          datap = SLIST_FIRST(&head);

          SLIST_REMOVE_HEAD(&head, entries);

          free(datap);
        }
        
        break;
      }
      else
      {
        syslog(LOG_DEBUG, "executed");
        perror("client_accept_fd");
      }  
    }
     
    syslog(LOG_DEBUG,"client_accept_fd completed");
    
    //TODO: create a memory for connection
    datap = (slist_data_t*)malloc(sizeof(slist_data_t));
    if(datap == NULL)
    {
      return -1;
    }
    
    //TODO: assign the client accept fd and thread ID, init with false status
    //to indicate thread did not exeute yet and any other parameters
    (datap->thread_data).thread_accept_fd = client_accept_fd;
    (datap->thread_data).thread_completion_status = false;
   
    SLIST_INSERT_HEAD(&head,datap,entries);
     
    //TODO: create a thread and assign the function to process
    int thread_stat = 0;
    thread_stat = pthread_create(&(datap->thread_data).pt_thread,NULL,&recv_client_send_server,(void *)&(datap->thread_data));
    if(thread_stat != 0)
    {
      perror("error in thread creation");
    }
    
    //TODO: wait for threads to join
    SLIST_FOREACH(datap,&head,entries)
    {
      if((datap->thread_data).thread_completion_status == true)
      { 
        pthread_join((datap->thread_data).pt_thread, NULL);
      }
    }  
  }
  
  SLIST_FOREACH(datap,&head,entries)
  {
    pthread_join((datap->thread_data).pt_thread, NULL);
  }
  
  close(client_accept_fd);
  
  //free the memory
  while(!SLIST_EMPTY(&head))
  {
    datap = SLIST_FIRST(&head);
    SLIST_REMOVE_HEAD(&head, entries);
    free(datap);
  }

  pthread_mutex_destroy(&data_lock);
  
  syslog(LOG_DEBUG,"-------------END OF PROGRAM-------------------");
  return 0;
}

//get socket address, IPV4 or IPV6
void *get_in_addr(struct sockaddr *sa)
{
  if(sa->sa_family == AF_INET)
  {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* handling sigint and sigterm graceful exit */
void socket_termination_signal_handler(int signo)
{

  syslog(LOG_DEBUG,"Caught signal, exiting\n");
 
  if(shutdown(server_socket_fd,SHUT_RDWR))
  {
    perror("Failed on shutdown");
    syslog(LOG_ERR,"Could not close socket fd in signal handler: %s",strerror(errno));
  }
 
#if USE_AESD_CHAR_DEVICE

#else

  int ret_status = 0;
  ret_status = timer_delete(timerid);
  if(ret_status == -1)
  { perror("error in deleting timer");}
   
  syslog(LOG_DEBUG,"deleting timer");

#endif
  
  g_Signal_handler_detection = 1;

}

/* commmon part handling for normal and sigint,sigterm exit */
void exit_handling()
{ 
  //closed any pending operations
  //close open sockets
  //delete the FILE createdfSL
  syslog(LOG_DEBUG,"exit_handling");
#if USE_AESD_CHAR_DEVICE
  //Don't remove file for /dev/aesdchar - driver is not a file
#else
  int ret_status = 0;
  ret_status = remove(FILE_PATH_TO_WRITE);
  syslog(LOG_DEBUG,"ret_status - remove:: %d\n",ret_status);
#endif
  close(client_accept_fd);
  close(server_socket_fd);
  //close(file_des);
  closelog();
    
  pthread_mutex_destroy(&data_lock);
  
}

/* EOF */
