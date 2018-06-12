#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>

#define	BUFSIZE			4096
#define QNUM			128
#define QLEN 			5


typedef struct Client {
    char     name[256];
    int      score;
    int      group_index;
    int      is_in_quiz;
    int      socket_fd;
} Client;


typedef struct Quiz {
    char     *question[128];
    char     *answer[128];
    char     *winner[128];
} Quiz;


typedef struct Group {
    char            group_name[256];
    char            quiz_topic[256];
    int             des_size;
    int				quest_num;
    Client          clients[1010];
    int             admin_sock;
    pthread_mutex_t client_num;
    int             current_size;
    struct Quiz     quiz;
} Group;


int passivesock(char* service, char* protocol, int qlen, int* rport);
void* run_thread(void* ptr);
void* quiz_thread(void* ptr);

Group *groups;
Client clients[1010];
Quiz quizes[256];

int groupnum = 0, group_total_number = 0, client_number = 0, cc;
pthread_mutex_t group_mut, client, gr_num, gr_work, cl_leave;
fd_set clr_main;

// when group is created size is incremented. used to traverse through created groups
int group_size = 0;
char open_groups_mes[BUFSIZE];


// server sends OPENGROUPS|topic|name|size|curr...
char* open_groups()
{
    pthread_mutex_lock(&group_mut);
    strcpy(open_groups_mes, "OPENGROUPS");

    for(int i = 0; i < groupnum; i++)
    {
        strcat(open_groups_mes, "|");
        strcat(open_groups_mes, groups[i].quiz_topic);
        printf("topic is %s\n", groups[i].quiz_topic);
        fflush(stdout);
        strcat(open_groups_mes, "|");
        strcat(open_groups_mes, groups[i].group_name);
        printf("name is %s\n", groups[i].group_name);
        fflush(stdout);
        char dezired_size[5], current_size[5];
        sprintf(dezired_size, "|%d", groups[i].des_size);
        sprintf(current_size, "|%d", groups[i].current_size);
        strcat(open_groups_mes, dezired_size);
        strcat(open_groups_mes, current_size);
    }

    strcat(open_groups_mes, "\r\n");
    pthread_mutex_unlock(&group_mut);
    printf("%s\n", open_groups_mes);
    return open_groups_mes;
}


// JOIN|groupname|username is sent by user. If group is not found -1 is sent to answer with NOGROUP
int find_group(char *join_group)
{
    for(int i = 0; i < groupnum; i++)
    {
        if(strcmp(groups[i].group_name, join_group) == 0)
        {
            return i;
        }
    }
    return -1;
}


int main(int argc, char* argv[])
{
    int alen, msock, ssock, cc, rport = 0;
    char* service;
	FD_ZERO(&clr_main);
    struct sockaddr_in fsin;

    // arguments - server, if given
    switch (argc)
    {
    case 1:
        rport = 1;
        break;
    case 2:
        service = argv[1];
        break;
    default:
        fprintf(stderr, "usage: server [port]\r\n");
        exit(-1);
    }

    msock = passivesock(service, "tcp", QLEN, &rport);

    // Tells the user port that was chosen
    groups = (Group *)malloc(sizeof(Group)*32);
    if (rport)
    {
        printf("server: port %d\n", rport);
        fflush(stdout);
    }

    pthread_mutex_init(&client, NULL);
    pthread_mutex_init(&gr_num, NULL);
    pthread_mutex_init(&gr_work, NULL);
    pthread_mutex_init(&cl_leave, NULL);
    alen = sizeof(fsin);

    while (1) {

        /* Reference to https://stackoverflow.com/questions/3719462/sockets-and-threads-using-c */
        ssock = accept(msock, (struct sockaddr*)&fsin, &alen);

        if (ssock < 0)
        {
            fprintf(stderr, "accept: %s\n", strerror(errno));
            exit(-1);
        }

        int* newsocket_fd = malloc(sizeof(int));
        *newsocket_fd = ssock;
        pthread_t thread;

        if (pthread_create(&thread, NULL, run_thread, (void*)newsocket_fd) < 0)
        {
            fprintf(stderr, "Could not create the thread: %s \r\n", strerror(errno));
            exit(-1);
        }
    }
    pthread_mutex_destroy(&cl_leave);
    pthread_mutex_destroy(&gr_work);
    pthread_mutex_destroy(&gr_num);
    pthread_mutex_destroy(&client);
    pthread_exit(0);

    // close passivesock
    close(msock);
    return 0;
}

// referenced http://www.cs.tau.ac.il/~eddiea/samples/IOMultiplexing/TCP-multiplex-server.c.html
// to understand multiplexing
void* quiz_thread(void* ptr)
{
    printf(" \n IN THE QUIZ HANDLER \n");
    fflush(stdout);
    int index;
    char *buf = (char *)malloc(sizeof(char)*BUFSIZE);

    // ptr is referred to the index of the group
    index = *(int*)ptr;

    // fd sets
    fd_set read_fds, write_fds, except_fds; //fds for read, write and exceptions
    fd_set afds, bfds;
    int nfds = 0;
    int adm_sock = groups[index].admin_sock;
    int cur_size = 0;
    int des_size = 0;

    // timeval for select
    struct timeval timer;
    timer.tv_sec = 60; /*Initiated time to wait for fd to change */
    timer.tv_usec = 0;

    // Set the max file descriptor being monitored
    nfds = adm_sock + 1;

    // initialize all fd sets
    FD_ZERO(&afds);
    FD_ZERO(&bfds);
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);

    // Admin socket should be added to available fds set
    FD_SET(adm_sock, &afds);
	FD_CLR(adm_sock, &clr_main);

    // information about the group
    cur_size = groups[index].current_size;
    des_size = groups[index].des_size;

    for(int k = 0; k < cur_size; k++)
    {
		FD_CLR(groups[index].clients[k].socket_fd, &clr_main);
        FD_SET(groups[index].clients[k].socket_fd, &afds);
    }
    printf("QUEST_NUM is: %d for group with index %d\n", groups[index].quest_num, index);
    fflush(stdout);

		/*
        	TODO:
        		loop over all questions
        		write question text
        		while (everybody answered && timeout > 0)
        		select(   , timer)l
        		timeleft -= timer.tv_sec
        		if(timeout)
        		LEAVE not aswered clients
        */

    for(int z = 0; z < groups[index].quest_num; z++)
    {
        char question[BUFSIZE];
        char *size = (char *)malloc(sizeof(char)*8);
        strcpy(question, "QUES|");
        int strl = strlen(groups[index].quiz.question[z]);
        sprintf(size, "%d|", strl);
        strcat(question, size);
        strcat(question, groups[index].quiz.question[z]);
        printf("%s", question);
        fflush(stdout);

        for(int r = 0; r < cur_size; r++)
        {
            write(groups[index].clients[r].socket_fd, question, strlen(question));
        }

        // Reset the file descriptors for read
        memcpy((char *)&read_fds, (char *)&afds, sizeof(read_fds));

        // Reference to multiplexing echoserver code from Moodle
        if (select(nfds, &read_fds, (fd_set *)0, (fd_set *)0, &timer) < 0)
        {
            fprintf( stderr, "server select: %s\n", strerror(errno) );
            exit(-1);
        }

        //pthread_mutex_lock(&groups[index].client_num);
        printf("\n NFDS is %d", nfds);
        fflush(stdout);

        // look through all clients fds that are in clients arr
        for(int i = 0; i < nfds; i++)
        {
            printf("The fd is %d \n", i);
            if (FD_ISSET( i, &read_fds))
            {
                if (i + 1 > nfds)
                {
                    nfds = i + 1;
                }

                if ( (cc = read( i, buf, BUFSIZE )) <= 0 )
                {
                    printf( "The client has gone.\n" );
                    (void) close(i);
                    FD_CLR( i, &afds );
                    // if admin leaves, finish everything
                    if (i == adm_sock)
                    {
                        // TODO: same functionality as for CANCEL - clear group and sets
                        FD_ZERO(&afds);
                        FD_ZERO(&bfds);
                        FD_ZERO(&read_fds);
                        FD_ZERO(&write_fds);
                        FD_ZERO(&except_fds);
                        pthread_exit(0);
                    }

                    groups[index].current_size--;

                    // lower the max socket number if needed
                    if ( nfds == i + 1 ) nfds--;
                }
                else
                {
                    printf("\n buf is %s \n", buf);
                    fflush(stdout);
                    buf[cc - 2] = '\0';
                    if(strncmp(buf, "CANCEL", 6) == 0 && i == adm_sock) {
                        // need to clean up the group and send ENDGROUP to all clients

                        char* rest2 = buf;
                        strtok_r(rest2, "|", &rest2); //omit CANCEL
                        char *gr_name = (char *)malloc(sizeof(char)*256);
                        gr_name = strtok_r(rest2, "|", &rest2); // not really needed - use index to reference group
						char end[280];
						strcat(end, "|");
						strcat(end, gr_name);
						strcat(end, "\r\n");
                        // group[index] - needs to be cleaned
                        for (int k = 0; k < groups[index].current_size; k++)
                        {
                            // send to all clients "ENDGROUP" and close sockets
                            send(groups[index].clients[k].socket_fd, end, strlen(end), 0);
                            close(groups[index].clients[k].socket_fd);
                            // TODO: clear array of clients and group struct
                        }
                    }
                }
            }
        }
    }

    //pthread_mutex_unlock(&groups[index].client_num);
    pthread_exit(0);
}


void* run_thread(void* ptr)
{
    char buffer[BUFSIZE];
    char* cname = (char*)malloc(sizeof(char) * 255);
    int socket_desc, cl_score = 0;

    /* Reference on how to communicate between server and client:
     * https://stackoverflow.com/questions/6798967/sending-data-in-socket-programming-using-c */

    socket_desc = *(int*)ptr;
    // new client accesses the server, a message about the groups is sent
    open_groups();
    printf("%s", open_groups_mes);
    fflush(stdout);

    // conversation with the client starts here. OPENGROUPS message is sent
    if (send(socket_desc, open_groups_mes, strlen(open_groups_mes), 0) < 0)
    {
        printf("Unable to send data. Connection refused. \r\n");
        fflush(stdout);
        close(socket_desc);
        pthread_mutex_unlock(&client);
        free(ptr);
        pthread_exit(NULL);
    }

    while(cc = read(socket_desc, buffer, BUFSIZE) > 0)
    {
        // remove CRLF
        //buffer[cc] = '\0';
        buffer[strlen(buffer) - 2] = '\0';

        printf("CLIENT SENDS: %s \n", buffer);

        /* 
		   need to identify the command
           before the quiz client can send GROUP|...,
           QUIZ|quizsize|quiztext,
           CANCEL|groupname,
           JOIN|groupname|username,
           LEAVE,
           GETOPENGROUPS - just call to open_groups() */

        if(strncmp(buffer, "GROUP", 5) == 0)
        {   //number of clients is not incremented here
            // need to divide GROUP|topic|groupname|groupsize into parts
            char *groupname;
            char *topicname;
            char groupsize_str[10];
            int size_gr;
            char* rest = buffer;
            char *n;

            // skip GROUP
            strtok_r(rest, "|", &n);

            // topic
            topicname = strtok_r(NULL, "|", &n);
            printf("TOPIC: %s\n", topicname);
            fflush(stdout);

            // groupname - should be unique => find_group should return -1
            groupname = strtok_r(NULL, "|", &n);
            groupname[strlen(groupname)] = '\0';
            printf("GROUPNAME: %s\n", groupname);
            fflush(stdout);
            if(find_group(groupname) != -1)
            {
                send(socket_desc, "BAD\r\n",
                     strlen("BAD\r\n"), 0);
                continue;
            }

            // groupsize
            strcpy(groupsize_str, strtok_r(NULL, "|", &n));
            size_gr = atoi(groupsize_str);
			if(size_gr <= 0)
            {
                send(socket_desc, "BAD\r\n",
                     strlen("BAD\r\n"), 0);
                continue;
            }

            pthread_mutex_lock(&gr_work);

            groups[groupnum].group_name[0] = '\0';
            strcpy(groups[groupnum].group_name, "");
            strcat(groups[groupnum].group_name, groupname);

            groups[groupnum].quiz_topic[0] = '\0';
            strcpy(groups[groupnum].quiz_topic, topicname);

            groups[groupnum].des_size = size_gr;
            groups[groupnum].current_size = 0;
            groups[groupnum].admin_sock = socket_desc;
			FD_SET(socket_desc, &clr_main);
            // will be destroyed when CANCEL is called
            pthread_mutex_init(&groups[groupnum].client_num, NULL);
            groupnum++;
            group_total_number++;
            pthread_mutex_unlock(&gr_work);

            if(write(socket_desc, "SENDQUIZ\r\n", strlen("SENQUIZ\r\n")+1)<0)
            {
                printf("Unable to send data. Connection refused. \r\n");
				write(socket_desc, "BAD\r\n", strlen("BAD\r\n"));
                fflush(stdout);
                close(socket_desc);
                pthread_mutex_unlock(&client);
                free(ptr);
                pthread_exit(NULL);
            } else {

                // read and parse quiz
                char new_buf[2048*128];

                //omit quiz
                if(read(socket_desc, new_buf, 5) < 0)
                {
                    printf("Unable to send data. Connection refused. \r\n");
					write(socket_desc, "BAD\r\n", strlen("BAD\r\n"));
                    fflush(stdout);
                    close(socket_desc);
                    pthread_mutex_unlock(&client);
                    free(ptr);
                    pthread_exit(NULL);
                }

                // max number of questions = 128, max length = 2048.
                // hence, 128*2048 = 262144 is the max size => 8 bytes is enough
                char quiz_size[8];
                strcpy(quiz_size, "");
                int i = 0;
                char b[BUFSIZE];
                while(cc = read(socket_desc, b, 1) > 0 && strcmp(b, "|") != 0)
                {
                    strcat(quiz_size, b);
                    i++;
                }
                quiz_size[i] = '\0';
                int size = atoi(quiz_size);
                int read_size = 0;
                char quiz_buffer[128*2048];
                quiz_buffer[0] = '\0';
                strcpy(quiz_buffer, "");

                while(read_size < size)
                {
                    cc = read(socket_desc, new_buf, BUFSIZE);
                    strncat(quiz_buffer, new_buf, cc);
                    read_size += cc;
                }

                //need to parse this huge string
                // when 2 \n -> answer
                int w = 0;
                int z = 0;
                int q = 0;
                int newline = 0;
                int quest_number = 0;
                printf("SIZE IS %d\n", size);

                // mutex as I am referencing global array of Groups here
                while(w < size - 3)
                {
                    char answer[32];
                    char quest[2048];
                    if(quiz_buffer[w] == '\n' && quiz_buffer[w+1] == '\n' && newline == 0) {

                        quest[0] = '\0';
                        strcpy(quest, "");
                        for(z = q; z < w; z++)
                        {
                            strncat(quest, &quiz_buffer[z], 1);
                        }

                        groups[groupnum-1].quiz.question[quest_number] = (char *)malloc(sizeof(char)*2048);
                        groups[groupnum-1].quiz.question[quest_number][0] = '\0';
                        strcpy(groups[groupnum-1].quiz.question[quest_number], quest);
                        printf("QUEST: %s \n", quest);
                        fflush(stdout);
                        w = w + 2;
                        answer[0] = '\0';
                        strcpy(answer, "");

                        while(quiz_buffer[w] != '\n')
                        {
                            strncat(answer, &quiz_buffer[w], 1);
                            w++;
                        }

                        pthread_mutex_lock(&gr_work);
                        groups[groupnum-1].quiz.answer[quest_number] = (char *)malloc(sizeof(char)*2048);
                        groups[groupnum-1].quiz.answer[quest_number][0] = '\0';
                        strcpy(groups[groupnum-1].quiz.answer[quest_number], answer);
                        printf("ANSWER: %s \n", answer);
                        fflush(stdout);
                        w++;
                        q = w;
                        newline = 1;
                        w += 2;
                        groups[groupnum-1].quiz.winner[quest_number] = (char *)malloc(sizeof(char)*2048);
                        groups[groupnum-1].quiz.winner[quest_number][0] = '\0';
                        quest_number++;
                        pthread_mutex_unlock(&gr_work);
                    } else if(newline == 1) {
                        newline = 0;
                    }

                    w++; // loop increment
                }
                groups[groupnum-1].quest_num = quest_number;
                printf("HERE, QUEST_NUM AFTER struct is %d for index %i \n", groups[groupnum-1].quest_num, groupnum-1);
                fflush(stdout);
				write(socket_desc, "OK\r\n", strlen("OK\r\n"));
                continue;
            }
        } else if(strncmp(buffer, "GETOPENGROUPS", 13) == 0) {
            printf("BUFF is %s \n", buffer);
            fflush(stdout);
            strcpy(open_groups_mes, "");
            strcpy(open_groups_mes, open_groups());

            // conversation with the client starts here. OPENGROUPS message is sent
            if (send(socket_desc, open_groups_mes, strlen(open_groups_mes), 0) < 0)
            {
                printf("Unable to send data. Connection refused. \r\n");
                fflush(stdout);
                close(socket_desc);
                pthread_mutex_unlock(&client);
                free(ptr);
                pthread_exit(NULL);
            }
        } else if(strncmp(buffer, "CANCEL", 6) == 0) {
            // need to find the name of group in array
            //pthread_mutex_destroy(&client_num);
            char* rest2 = buffer;
            strtok_r(rest2, "|", &rest2); //omit CANCEL
            char *gr_name = (char *)malloc(sizeof(char)*256);
            gr_name = strtok_r(rest2, "|", &rest2);
			int index = find_group(gr_name);
			printf("The index for %s is %d \n", gr_name, index);
			fflush(stdout);
			if(socket_desc != groups[index].admin_sock){
				//Only admiin can CANCEL the group
				write(socket_desc, "BAD\r\n", strlen("BAD\r\n"));
				continue;
			}
			printf("WANTS TO CANCEL GROUP %s at index %d \n", gr_name, index);
			fflush(stdout);
            

            if(index != -1)
            {
                send(socket_desc, "OK\r\n", strlen("OK\r\n"), 0);
                pthread_mutex_lock(&gr_num);
                pthread_mutex_destroy(&groups[index].client_num);
                pthread_mutex_unlock(&gr_num);
                group_total_number--;
				char end[280];
				strcat(end, "|");
				strcat(end, gr_name);
				strcat(end, "\r\n");
                // here "ENDGROUP" needs to be sent to all clients who are in this group
                // if they are not playing the quiz yet
				for(int g = 0; g < groups[index].current_size; g++){
					write(groups[index].clients[g].socket_fd, end, strlen(end));
				}
				
            } else {
                // if no such group
                write(socket_desc, "BAD\r\n", strlen("BAD\r\n"));
                continue;
            }
        } else if (strncmp(buffer, "JOIN", 4) == 0) {
            printf("User wants to join group. \n");
            fflush(stdout);
            char *rest3 = buffer;
            char *z;
			
            strtok_r(rest3, "|", &z);
            char *gr_name_join = (char *)malloc(sizeof(char)*256);
            gr_name_join = strtok_r(NULL, "|", &z);
            int ind = find_group(gr_name_join);
            if (ind != -1)
            {
                printf("Group exists. \n");
                fflush(stdout);
                pthread_mutex_lock(&gr_num);

                // need to create a client structure here
                struct Client cl;
                cl.socket_fd = socket_desc;
                cl.score = 0;
                cl.is_in_quiz = 0;
                cl.name[0] = '\0';
                client_number ++;
                strcpy(cl.name, rest3);

                groups[ind].current_size++;

                if (groups[ind].current_size > groups[ind].des_size)
                {
                    printf("The group is full. \n");
                    send(socket_desc, "BAD\r\n", strlen("BAD\r\n"), 0);
                    groups[ind].current_size--;
                    client_number --;
                    continue;
                } else if (groups[ind].current_size < groups[ind].des_size) {
                    send(socket_desc, "OK\r\n", strlen("OK\r\n"), 0);

                    // set the index
                    cl.group_index = ind;
                    printf("SOCKET FOR THIS CLIENT IS %d", cl.socket_fd);
                    fflush(stdout);

                    // add to groups array of clients
					FD_SET(cl.socket_fd, &clr_main);
                    groups[ind].clients[groups[ind].current_size] = cl;
                } else if (groups[ind].current_size == groups[ind].des_size) {
                    send(socket_desc, "OK\r\n", strlen("OK\r\n"), 0);
                    // set the index
                    cl.group_index = ind;

                    // add to groups array of clients
                    groups[ind].clients[client_number - 1] = cl;
                    printf("SOCKET FOR THIS CLIENT IS %d \n", groups[ind].clients[client_number - 1].socket_fd);
                    fflush(stdout);
					FD_SET(cl.socket_fd, &clr_main);
                    printf("Index of this client is %d \n", groups[ind].current_size);

                    // if there is enough people joined, sends to quiz thread
                    pthread_t q_thread;
                    int* group_index = malloc(sizeof(int));
                    *group_index = ind;

                    // thread for Quiz
                    if (pthread_create(&q_thread, NULL, quiz_thread, (void*)group_index) < 0)
                    {
                        fprintf(stderr, "Could not create the thread: %s \r\n", strerror(errno));
                        exit(-1);
                    }
                }
                pthread_mutex_unlock(&gr_num);
            } else {
                printf("NAME_GR: %s\n", gr_name_join);
                send(socket_desc, "NOGROUP\r\n", strlen("NOGROUP\r\n"), 0);
                continue;
            }
        }
        else if (strncmp(buffer, "LEAVE", 5) == 0)
        {
            printf("User wants to leave the group \n");
            fflush(stdout);
            pthread_mutex_lock(&cl_leave);
            for(int i = 0; i < client_number; i++)
            {
                if (clients[i].socket_fd == socket_desc)
                {
                    //find the index of group and decrement the size in it
                    int gr_ind = clients[i].group_index;
                    if(gr_ind == -1)
                    {
                        send(socket_desc, "BAD\r\n", strlen("BAD\r\n"), 0);
                        printf("Client is not in the group yet \n");
                        fflush(stdout);
                    } else {
                        printf("Left \n");
                        fflush(stdout);
                        groups[gr_ind].current_size--;
                    }
                }
            }
            pthread_mutex_unlock(&cl_leave);
        }
    }
    pthread_exit(0);
}
