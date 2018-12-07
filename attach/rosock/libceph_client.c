/*
 * libceph_client.c
 *
 * Contact: JeCortex@yahoo.com
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>

int __init libceph_init(void)
{
        struct socket *sock = NULL;
        struct sockaddr_in s_addr;
        unsigned short portnum = 9876;
        char *sendbuf = NULL;
        struct kvec vec;
        struct msghdr msg;
        int ret=0;
        printk("init blah blah ...\n");

        memset(&s_addr,0,sizeof(s_addr));
        s_addr.sin_family=AF_INET;
        s_addr.sin_port=htons(portnum);
        s_addr.sin_addr.s_addr=in_aton("10.10.11.101");

        /*create a socket*/
        //ret=sock_create_kern(AF_INET, SOCK_STREAM,0,&sock);
        ret=sock_create(AF_INET, SOCK_STREAM,0,&sock);
        if(ret<0){
                printk("client:socket create error!\n");
                return ret;
        }

        printk("client: socket create ok!\n");

        /*connect server*/
        ret=sock->ops->connect(sock,(struct sockaddr *)&s_addr, sizeof(s_addr),0);
        if(ret!=0){
                sock_release(sock);
                printk("client:connect error!\n");
                return ret;
        }
        printk("client:connect ok!\n");

        /*kmalloc sendbuf*/
        sendbuf=kzalloc(1024,GFP_KERNEL);
        if(sendbuf==NULL){
                printk("client: sendbuf kmalloc error!\n");
                return -1;

        }

        vec.iov_base = sendbuf;
        vec.iov_len = 1024;

        memset(&msg,0,sizeof(msg));

        ret=kernel_sendmsg(sock,&msg,&vec,1,512); /*send message */
        if(ret<0){
                printk("client: kernel_sendmsg error!\n");
                return ret;

        }else if(ret!=1024){
                printk("client: ret!=1024");
        }

        printk("client:send ok!\n");
        return 0;
}

void __exit libceph_exit(void)
{
        printk("exit blah blah ...\n");
}

module_init(libceph_init);
module_exit(libceph_exit);

MODULE_LICENSE("GPL");
