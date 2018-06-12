#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFSIZE		4096

pthread_t 	        sendt, receive;
pthread_mutex_t         mutex;
char                    *Lnum;
char                    *Lname;
char                    answer[BUFSIZE];
int connectsock( char *host, char *service, char *protocol );
void rand_name(char *name, const int length);
void *get_message(void *consock);
void *send_message(void *consock);
/*
**	Client
*/
int
main( int argc, char *argv[] )
{
	char		buf[BUFSIZE];
	char		*service;		
	char		*host = "localhost";
	int		cc;
	int		csock;
	
	switch( argc ) 
	{
		case    2:
			service = argv[1];
			break;
		case    3:
			host = argv[1];
			service = argv[2];
			break;
		default:
			fprintf( stderr, "usage: chat [host] port\n" );
			exit(-1);
	}

	/*	Create the socket to the controller  */
	if ( ( csock = connectsock( host, service, "tcp" )) == 0 )
	{
		fprintf( stderr, "Cannot connect to server.\n" );
		exit( -1 );
	}

	printf( "The server is ready, please start sending to the server.\n" );
	fflush( stdout );
	pthread_mutex_init(&mutex, NULL);
	int *con_sock = (int *) malloc(sizeof(int));
	*con_sock = csock;
	// thread for message receive by client
        if(pthread_create(&receive, NULL, get_message, (void *) con_sock)<0) {
            fprintf( stderr, "Could not create the thread: %s \r\n", strerror(errno) );
            exit(-1);
        }
	pthread_join(receive, NULL);
	pthread_exit(0);
	pthread_mutex_destroy(&mutex);
}
/* Reference to: https://www.codeproject.com/Questions/640193/Random-string-in-language-C */
void rand_name(char *name, const int length) {
    
    static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    printf("HERE");
    fflush(stdout);
    for (int i = 0; i < length-1; i++) {
         
	int ind = abs(rand()) % (strlen(letters) - 1);
        printf("%d\n", ind);
        fflush(stdout);
        name[i] = letters[abs(rand()) % (strlen(letters) - 1)];
        
    }

    name[length-1] = "\0";
    printf("%s", name);
    fflush(stdout);
}
/*
 * Ref to: https://codereview.stackexchange.com/questions/151044/socket-client-in-c-using-threads/151046#151046
 */

void *get_message(void *consock){
    char buf[BUFSIZE];
    int len;
    char *cname;
    while (1) {

        if ((len = recv(*((int *) consock), buf, BUFSIZE, 0)) <= 0) {
            printf( "The server has gone.\n" );
            close(consock);
            break;
        }
        printf("%s", buf);
        //Receive a reply from the server
        buf[len+1] = "\0";
	printf("%d\n", strlen(buf));
        
	/*//what message was sent?
	if(strcmp(buf, "QS|ADMIN\r\n") == 0){
	// need to send GROUP|Lname|Lnum\r\n
                printf("here");
		fflush(stdout);
		Lname = (char *)malloc(sizeof(char)*5);
		rand_name(Lname, 5);
		//convert int to string
		Lnum = sprintf(Lnum, "|%d", 5);
		strcpy(answer, "GROUP|");
	        strcat(answer, Lname);
	        strcat(answer, Lnum);
	        strcat(answer, "\r\n");
        } else if (strcmp(buf, "QS|JOIN\r\n") == 0){
		rand_name(cname, 5);
		strcpy(answer, "JOIN|");
	        strcat(answer, cname);
	} else if(strcmp(buf, "WAIT\r\n") == 0){
		// Do not send anything		
		continue;	
	} else if(strcmp(buf, "FULL\r\n") == 0){
                printf( "Server doesn't accept any clients. Try later.\n" );	
		fflush(stdout);
                close(consock);	
	} else if(strcmp(buf, "WINNER\r\n") == 0){
		printf("You win");
		fflush(stdout);
		close(consock);	
	} else if(strcmp(buf, "NOT A WINNER\r\n")==0){
		printf("Losed :c");
		fflush(stdout);
		close(consock);	
	} else {
		printf("You are entering game mode");
		fflush(stdout);
			
	}
	*/
	// send a response to server 
	if(pthread_create(&sendt, NULL, send_message, (void *) consock) < 0) {
	    printf( "Error in thread.\n" );
            close(consock);
            exit(-1);
	}
	pthread_join(sendt, NULL);
    }
}

void *send_message(void *consock){
	
	if(send(*((int *) consock), answer, strlen(answer), 0) < 0){
	    printf( "Connection to server refused.\n" );
	    fflush(stdout);
            close(consock);
            exit(-1);
	}	
	pthread_exit(NULL);
}



