#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>

#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))

char ip[16];

/* Zmodyfikowany kod z manuala select_tut */
static int listen_socket(int listen_port)
{
    struct sockaddr_in addr;
    int lfd;
    int yes;

    /* Tworzymy gniazdo TCP */
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }

    yes = 1;

    if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
    {
        perror("setsockopt");
        close(lfd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(listen_port);
    addr.sin_family = AF_INET;

    /* Przypisujemy strukture sockaddr z wybranym portem lokalnym do stworzonego gniazda przy pomocy bind */
    if (bind(lfd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
    {
        perror("bind");
        close(lfd);
        return -1;
    }

    printf("accepting connections on port %d\n", listen_port);
    listen(lfd, 10);
    return lfd;
}

static int connect_socket(int connect_port, char *address)
{
    struct sockaddr_in addr;
    int cfd;

    /* Tworzymy gniazdo AF_INET -> TCP */
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd == -1)
    {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(connect_port);
    addr.sin_family = AF_INET;

    if (!inet_aton(address, (struct in_addr *) &addr.sin_addr.s_addr))
    {
        perror("bad IP address format");
        close(cfd);
        return -1;
    }

    /* Ustanawiamy polaczenie z serwerem */
    if (connect(cfd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
    {
        perror("connect()");
        shutdown(cfd, SHUT_RDWR);
        close(cfd);
        return -1;
    }

    return cfd;
}

/* Makra sluza do zamkniecia */
#define SHUT_FD1                       \
do                                     \
{                                      \
    if (fd1[i] >= 0)                   \
    {                                  \
        shutdown(fd1[i], SHUT_RDWR);   \
        close(fd1[i]);                 \
        fd1[i] = -1;                   \
    }                                  \
} while (0)

#define SHUT_FD2                       \
do                                     \
{                                      \
    if (fd2[i] >= 0)                   \
    {                                  \
        shutdown(fd2[i], SHUT_RDWR);   \
        close(fd2[i]);                 \
        fd2[i] = -1;                   \
    }                                  \
} while (0)

#define BUF_SIZE 1024

void uzyskajAdresIP4(char * adress)
{
    int s1;
    int sockfd;
    struct addrinfo *result, *rp;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    s1 = getaddrinfo(adress,"5555",&hints,&result);

    if(s1 != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s1));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(sockfd == -1)
        {
            continue;    
        }
        
        if(bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            break;
        }

        close(sockfd);
    }
    inet_ntop(AF_INET, &(((struct sockaddr_in *)result->ai_addr)->sin_addr),ip,16);
    freeaddrinfo(result);
}

int main(int argc, char *argv[])
{
    int fd1[argc-1];
    int fd2[argc-1];
    char buf1[argc-1][BUF_SIZE];
    char buf2[argc-1][BUF_SIZE];
    int buf1_avail[argc-1];
    int buf2_avail[argc-1];
    int buf1_written[argc-1];
    int buf2_written[argc-1];
    char * adress[argc-1];
    char adressIP4[argc-1][16];
    int forward_port[argc-1];
    int came_port[argc-1];
    int h[argc-1];
    int fd;

    if (argc < 2)
    {
        printf("Uruchom program przynajmniej z jednym argumentem typu portLokalny:host:portHost");
        exit(EXIT_FAILURE);
    }

    /* Ignorujemy sygnal SIGPIPE*/
    signal(SIGPIPE, SIG_IGN);

    /* Obrobka danych z argumentow */
    for(int i=1; i<argc; i++)
    {
        came_port[i-1] = atoi(strtok(argv[i],":"));
        adress[i-1] = strtok(NULL,":");
        forward_port[i-1] = atoi(strtok(NULL,":"));
        uzyskajAdresIP4(adress[i-1]);
        for(int x=0; x<16; x++)
        {
            adressIP4[i-1][x]=ip[x];
        } 
        h[i-1] = listen_socket(came_port[i-1]);
        

        if (h[i-1] == -1)
        {
            exit(EXIT_FAILURE);
        }
    }

    /* Czyszczenie danych oraz nadawanie tablica fd1,fd2 wartosci -1 */
    for(int i=0; i<argc-1; i++)
    {
        fd1[i] = -1;
        fd2[i] = -1;
        buf1_avail[i] = 0;
        buf2_avail[i] = 0;
        buf1_written[i] = 0;
        buf2_written[i] = 0;
    }

    for (;;)
    {
        int ready, nfds = 0;
        ssize_t nbytes;
        fd_set readfds, writefds, exceptfds;

        /* Czyszczenie zbiorow deskryptorow plikow */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        for(int i=0; i<argc-1; i++)
        {
            /* ustawienie zbiorow deskryptorow plikow */
            FD_SET(h[i], &readfds);
            nfds = max(nfds, h[i]);
            if (fd1[i] > 0 && buf1_avail[i] < BUF_SIZE)
            {
                FD_SET(fd1[i], &readfds);
            }

            if (fd2[i] > 0 && buf2_avail[i] < BUF_SIZE)
            {
                FD_SET(fd2[i], &readfds);
            }

            if (fd1[i] > 0 && buf2_avail[i] - buf2_written[i] > 0)
            {
                FD_SET(fd1[i], &writefds);
            }

            if (fd2[i] > 0 && buf1_avail[i] - buf1_written[i] > 0)
            {
                FD_SET(fd2[i], &writefds);
            }

            if (fd1[i] > 0)
            {
                FD_SET(fd1[i], &exceptfds);
                nfds = max(nfds, fd1[i]);
            }

            if (fd2[i] > 0)
            {
                FD_SET(fd2[i], &exceptfds);
                nfds = max(nfds, fd2[i]);
            }
        }
        /* Sprawdzenie czy jakis deskryptor jest gotowy*/
        ready = select(nfds + 1, &readfds, &writefds, &exceptfds, NULL);

        /* Obsluga bledow */
        if (ready == -1 && errno == EINTR)
        {
            continue;
        }

        if (ready == -1)
        {
            perror("select()");
            exit(EXIT_FAILURE);
        }

        /* */
        for (int i = 0; i < argc-1; i++)
        {
            /* Sprawdzam czy sa dane przy uzyciu FD_ISSET*/
            if (FD_ISSET(h[i], &readfds))
            {
                socklen_t addrlen;
                struct sockaddr_in client_addr;
                addrlen = sizeof(client_addr);
                memset(&client_addr, 0, addrlen);
                fd = accept(h[i], (struct sockaddr *) &client_addr, &addrlen);
                if (fd == -1)
                {
                    perror("accept()");
                }
                else
                {
                    SHUT_FD1;
                    SHUT_FD2;
                    buf1_avail[i] = buf1_written[i] = 0;
                    buf2_avail[i] = buf2_written[i] = 0;
                    fd1[i] = fd;
                    fd2[i] = connect_socket(forward_port[i], adressIP4[i]);
                    if (fd2[i] == -1)
                    {
                        SHUT_FD1;
                    }
                    else
                    {
                        printf("Port: %d connect from %s\n",came_port[i], inet_ntoa(client_addr.sin_addr));
                        /* Skip any events on the old, closed file descriptors. */
                        continue;
                    }
                }
            }
        }


        for(int i=0; i<argc-1; i++)
        {
            /* Sprawdzam czy sa dane przy uzyciu FD_ISSET oraz je odczytuje */
            if (fd1[i] > 0 && FD_ISSET(fd1[i], &exceptfds))
            {
                char c;

                nbytes = recv(fd1[i], &c, 1, MSG_OOB);
                if (nbytes < 1)
                {
                    SHUT_FD1;
                }
                else
                {
                    send(fd2[i], &c, 1, MSG_OOB);
                }
            }

            if (fd2[i] > 0 && FD_ISSET(fd2[i], &exceptfds))
            {
                char c;

                nbytes = recv(fd2[i], &c, 1, MSG_OOB);
                if (nbytes < 1)
                {
                    SHUT_FD2;
                }
                else
                {
                    send(fd1[i], &c, 1, MSG_OOB);
                }
            }

            if (fd1[i] > 0 && FD_ISSET(fd1[i], &readfds))
            {
                nbytes = read(fd1[i], buf1[i] + buf1_avail[i], BUF_SIZE - buf1_avail[i]);

                if (nbytes < 1)
                {
                    SHUT_FD1;
                }
                else
                {
                    buf1_avail[i] += nbytes;
                }
            }

            if (fd2[i] > 0 && FD_ISSET(fd2[i], &readfds))
            {
                nbytes = read(fd2[i], buf2[i] + buf2_avail[i], BUF_SIZE - buf2_avail[i]);

                if (nbytes < 1)
                {
                    SHUT_FD2;
                }
                else
                {
                    buf2_avail[i] += nbytes;
                }
            }

            if (fd1[i] > 0 && FD_ISSET(fd1[i], &writefds) && buf2_avail[i] > 0)
            {
                nbytes = write(fd1[i], buf2[i] + buf2_written[i], buf2_avail[i] - buf2_written[i]);

                if (nbytes < 1)
                {
                    SHUT_FD1;
                }
                else
                {
                    buf2_written[i] += nbytes;
                }
            }

            if (fd2[i] > 0 && FD_ISSET(fd2[i], &writefds) && buf1_avail[i] > 0)
            {
                nbytes = write(fd2[i], buf1[i] + buf1_written[i], buf1_avail[i] - buf1_written[i]);

                if (nbytes < 1)
                {
                    SHUT_FD2;
                }
                else
                {
                    buf1_written[i] += nbytes;
                }
            }

            /* Czyscimy buffory oraz zamykamy gniazda */
            if (buf1_written[i] == buf1_avail[i])
            {
                buf1_written[i] = buf1_avail[i] = 0;
            }

            if (buf2_written[i] == buf2_avail[i])
            {
                buf2_written[i] = buf2_avail[i] = 0;
            }

            if (fd1[i] < 0 && buf1_avail[i] - buf1_written[i] == 0)
            {
                SHUT_FD2;
            }

            if (fd2[i] < 0 && buf2_avail[i] - buf2_written[i] == 0)
            {
                SHUT_FD1;
            }
        }
    }
    exit(EXIT_SUCCESS);
}
