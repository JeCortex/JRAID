/*
 * osd_server.c
 *
 * Contact: JeCortex@yahoo.com
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
        int sockfd, new_fd;
        struct sockaddr_in my_addr;
        struct sockaddr_in addr;
        int sin_size, bytes;
        char buff[1024];

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
                perror("socket");
                exit(1);
        }

        printf("create socket success\n");

        memset(&my_addr, 0, sizeof(struct sockaddr_in));
        my_addr.sin_family = AF_INET;
        my_addr.sin_port = htons(9876);
        my_addr.sin_addr.s_addr = inet_addr("10.10.11.101")/*INADDR_ANY*/;

        if (bind(sockfd, (struct sockaddr*)&my_addr,
                 sizeof(struct sockaddr_in)) == -1) {
                         perror("bind error");
                         exit(1);
        }

        if (listen(sockfd, 10) == -1) {
                perror("listen error");
                exit(1);
        }

        sin_size = sizeof(struct sockaddr_in);
        new_fd = accept(sockfd, (struct sockaddr*)&addr, &sin_size);
        if (new_fd == -1) {
                perror("accept error");
                exit(1);
        }

        memset(buff, 1024, 0);
        bytes = recv(new_fd, buff, sizeof(buff), 0);
        printf("recv %d bytes\n", bytes);

        printf("%s\n", buff);

        close(new_fd);
        close(sockfd);
}
