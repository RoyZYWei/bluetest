#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <pthread.h>

#define ClientMax 20
#define BUFSIZE 512
int c_fd[ClientMax];
char recBuf[BUFSIZE] = {0};		//用于记录接入的客户端的mac地址

/*******************
用于广播信息到各个蓝牙的线程，广播的消息这里通过终端直接输入
的形式，实际应用时，可自行修改为其他信息源
*******************/
void *sendmsg_func(void *p)
{
	int j;
	printf("启动信息发送线程:\n");
    printf("直接在空白处输入即可\n");

    char sendBuf[BUFSIZE] = {'\0'};		//用于存储要广播的消息
	while(1)
	{
        memset(sendBuf,0,BUFSIZE);
		fgets(sendBuf,BUFSIZE,stdin);	//用于用户输入要广播的消息
        
		//给所有在线的客户端发送信息
		for(j = 0;c_fd[j] > 0 && j < ClientMax;j++)
		{
			if (c_fd[j] == -1)
			{
				continue;	//如果是已退出或未使用的客户端，则不发送信息
			}
			else
			{
				if(write(c_fd[j],sendBuf,BUFSIZE) < 0 )
   				{
        			perror("write");
        			exit(-1);
    			}
			}
		}
	}
}


/*******************
用于接收新接入的蓝牙客户端消息
*******************/

void *recv_func(void *p)
{
    int tmp_c_fd = *((int *)p);		//拿到接入的客户端的套接字
    
    char nameBuf[BUFSIZE] = {0};	//存储接入的客户端的mac地址,用于区别不同客户端
    char readBuf[BUFSIZE] = {0};	//用于存储接收到对应客户端的消息
    int n_read = 0;
    
    //将全局变量recBuf接收到的mac地址，copy到nameBuf中
    strcpy(nameBuf,recBuf);    //这里其实最好要考虑线程并发对recBuf值的改变，可以考虑使用互斥量等方法
    pthread_t tid;
    tid = pthread_self();
    printf("启动线程tid:%lu,用于接收新蓝牙从机%s的信息\n" ,tid,nameBuf);
    
    while(1)
    {
        memset(readBuf,0,BUFSIZE);
        n_read = read(tmp_c_fd,readBuf,sizeof(readBuf));
		if(n_read <= 0)
		{
			//perror("read");	//调试语句
        	printf("%s中断或者下线了\n",nameBuf);
			tmp_c_fd = -1;		//如果对应的客户端退出，则令对应的c_fd的值为-1，表示掉线
			pthread_exit(NULL);	//如果客户端掉线，结束线程
		}
    	else 
   		{
        	printf("%s:#%s\n",nameBuf,readBuf);	//将用户发送的信息打印在服务端，若有数据库，这里可以将聊天记录存在数据库
		}
    }
    
}



int main()
{
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    
    int s,bytes_read,i,err,ret;
    pthread_t rec_tid[ClientMax] = {0};		
    pthread_t send_tid; 
    int opt = sizeof(rem_addr);
    //让本机蓝牙处于可见状态
	ret = system("hciconfig hci0 piscan");
	if(ret < 0)
	{
		perror("bluetooth discovering fail");
	}
    
    // allocate socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // bind socket to port 1 of the first available
    // local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;   //相当于tcp的ip地址
    loc_addr.rc_channel = (uint8_t) 1;  //这里的通道就是SPP的通道，相当于网络编程里的端口

    bind(s, (struct sockaddr *)&loc_addr, sizeof(loc_addr));

    // put socket into listening mode
    listen(s, ClientMax);
    printf("bluetooth_server listen success\n");

    //初始化客户端套接字
    for(i = 0;i < ClientMax;i++)
    {
        c_fd[i] = -1;
    }

    //创建线程用于广播消息
    err = pthread_create(&send_tid,NULL,sendmsg_func,NULL);
	if(err)
	{
		fprintf(stderr,"Create pthread fail:%s\n",strerror(err));
		exit(1);
	}

    //不断等待是否有新蓝牙接入
    while(1)
    {
        i = 0;
        
        //从数组中选取一个可用的客户端套接字，值等于-1即为可用的套接字
        while(1)
        {
            if((i < ClientMax) && (c_fd[i] != -1))
            {
                i++;
            }
            else if(i >= ClientMax)
            {
                fprintf(stderr,"client fd has more than 20\n");
                exit(-1);
            }
            else
            {
                break;
            }
        }

        //accept新的蓝牙接入
        c_fd[i] = accept(s, (struct sockaddr *)&rem_addr, &opt);
        if (c_fd[i] > 0){
            printf("client connected success\n");
        }
        else{
            printf("accept client fail\n");
            continue;
        }
        
        // ba2str把6字节的bdaddr_t结构
        //转为为形如XX:XX:XX:XX:XX:XX(XX标识48位蓝牙地址的16进制的一个字节)的字符串
        ba2str( &rem_addr.rc_bdaddr, recBuf);	
        fprintf(stdout, "accepted connection from %s\n", recBuf);


        //为每个新的客户端创建自己的线程用于接收信息
        err = pthread_create((rec_tid+i),NULL,recv_func,(c_fd+i));
		if (err)
		{
			fprintf(stderr,"Create pthread fail:%s\n",strerror(err));
			exit(1);
		}	
    }
    // close connection
    //close(client);
    close(s);
    return 0;
}


