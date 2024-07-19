#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <set>
#include <unordered_set>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <pthread.h>
#include <vector>
#include <queue>
#include <fstream>
#include "Condition.h"
#include "Mutex.h"
#include "pthread.h"
#include "json.hpp"

#define ClientMax 20
#define BUFSIZE 512

using nlohmann::json;

std::vector<int> c_fd;
std::queue<std::string> message_queue;
bool conn = false;
char recBuf[BUFSIZE] = {0};  // 用于记录接入的客户端的mac地址
Mutex lock;
Condition cond(lock);
std::string localmac = "";

const char* SCAN_CMD = "nmcli -t -f SSID dev wifi list";

std::string get_mac_address() {
    std::string mac_address;
    std::ifstream file("/sys/class/net/eth0/address");
    if (file.is_open()) {
        std::getline(file, mac_address);
        file.close();
    }
    if(!mac_address.empty()){
        for(int i = 0; i < mac_address.size(); i++){
            if(mac_address[i] == ':'){
                mac_address.erase(i, 1);
            }
            
        }
    }
    std::cout << mac_address << std::endl;
    return mac_address;
}

std::string executeCMD(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    std::string result;
    if (!pipe) {
        std::cerr << "Error: popen failed" << std::endl;
        return "";
    }
    char buffer[1024];
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != NULL) {
            result += buffer;
        }
    }
    pclose(pipe);
    return result;
}

void* sendmsg_func(void* arg) {
    std::cout << "启动信息发送线程\n";
    char sendBuf[BUFSIZE] = {'\0'};  // 用于存储要广播的消息
    while (true) {
        std::memset(sendBuf, 0, BUFSIZE);
        std::string msg;
        {
            MutexLockGuard lockguard(lock);
            while (message_queue.empty()) {
                cond.wait();
            }
            msg = message_queue.front();
            message_queue.pop();
        }
        
        // 给所有在线的客户端发送信息
        for (int j = 0; c_fd[j] > 0 && j < ClientMax; j++) {
            if (c_fd[j] == -1) {
                continue;  // 如果是已退出或未使用的客户端，则不发送信息
            } else {
                std::cout << "write to client: " << c_fd[j] << std::endl;
                if (write(c_fd[j], msg.c_str(), msg.size() + 1) < 0) {
                    perror("write");
                    exit(-1);
                }
            }
        }
    }
}

void onMessageCallBack(const std::string& message) {
    if (message.empty()) return;

    try {
        nlohmann::json js_message = nlohmann::json::parse(message);
        std::cout << "enter json check" << std::endl;
        std::string type = js_message["type"].get<std::string>();

        if (type == "list") {
            std::string wifilist = executeCMD(SCAN_CMD);    // 得到 wifi 列表
            std::vector<std::string> ssid_list;
            std::unordered_set<std::string> ssid_set;
            std::istringstream ss(wifilist);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && ssid_set.find(line) == ssid_set.end()) {
                    ssid_list.push_back(line);
                    ssid_set.insert(line);
                }
            }
            for (const auto& x : ssid_list) {
                std::cout << x << std::endl;
            }
            nlohmann::json send_msg = {
                {"type", "wifilist"},
                {"data", ssid_list}
            };
            std::string strmsg = send_msg.dump();
            // std::cout << send_msg.dump() << std::endl;
            std::cout << "last char: " << strmsg.back() << std::endl;
            {
                MutexLockGuard lk(lock);
                message_queue.push(send_msg.dump());
                cond.signal();
            }
        } else if (type == "connect") {
            std::string ssid = js_message["data"]["ssid"].get<std::string>();
            std::string password = js_message["data"]["password"].get<std::string>();
            std::cout << "ssid: " << ssid << " password: " << password << std::endl;
            std::string cmd = "nmcli dev wifi connect " + ssid + " password " + password ;
            std::string result = executeCMD(cmd.c_str());
            std::cout << "connect result: " << result << std::endl;
            bool connection_successful = result.find("successfully activated") != std::string::npos;
            nlohmann::json send_msg = {
                {"type", "connect"},
                {"data", connection_successful}
            };
        
            std::cout << send_msg.dump() << std::endl;
            {
                MutexLockGuard lk(lock);
                message_queue.push(send_msg.dump());
                cond.signal();
            }
            if (connection_successful) {
                conn = true;
            }
        }

    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void* recv_func(void* p) {
    int tmp_c_fd = *static_cast<int*>(p);  // 拿到接入的客户端的套接字

    char nameBuf[BUFSIZE] = {0};  // 存储接入的客户端的mac地址,用于区别不同客户端
    char readBuf[BUFSIZE] = {0};  // 用于存储接收到对应客户端的消息
    int n_read = 0;

    std::strcpy(nameBuf, recBuf);  
    pthread_t tid = pthread_self();
    std::cout << "启动线程tid:" << tid << ",用于接收新蓝牙从机" << nameBuf << "的信息\n";

    while (true) {
        std::memset(readBuf, 0, BUFSIZE);
        n_read = read(tmp_c_fd, readBuf, sizeof(readBuf));
        if (n_read <= 0) {
            std::cout << nameBuf << "中断或者下线了\n";
            close(tmp_c_fd);
            *static_cast<int*>(p) = -1;  // 如果对应的客户端退出，则令对应的c_fd的值为-1，表示掉线
            pthread_exit(nullptr);  // 如果客户端掉线，结束线程
        } else {
            std::cout << "get user: " << nameBuf << "message" << ": " << readBuf << "\n";  // 将用户发送的信息打印在服务端，若有数据库，这里可以将聊天记录存在数据库
            onMessageCallBack(readBuf);
        }
    }
}

void* accept_func(void* arg) {
    int s = *static_cast<int*>(arg);
    struct sockaddr_rc rem_addr = { 0 };
    int opt = sizeof(rem_addr);

    while (true) {
        if (conn == true) {
            close(s);
            return nullptr;
        }
        int i = 0;
        while (true) {
            if ((i < ClientMax) && (c_fd[i] != -1)) {
                i++;
            } else if (i >= ClientMax) {
                std::cerr << "client fd has more than 20\n";
                exit(-1);
            } else {
                break;
            }
        }

        // accept新的蓝牙接入
        c_fd[i] = accept(s, (struct sockaddr*)&rem_addr, reinterpret_cast<socklen_t*>(&opt));
        std::cout << "NEW FD: " << c_fd[i] << std::endl;
        if (c_fd[i] > 0) {
            std::cout << "client connected success\n";
            json initmsg;
            initmsg["type"] = "bind";
            initmsg["data"] = localmac;
            {
                MutexLockGuard lk(lock);
                message_queue.push(initmsg.dump());
                cond.signal();
            }
        } else {
            std::cout << "accept client fail\n";
            continue;
        }

        // ba2str把6字节的bdaddr_t结构
        // 转为为形如XX:XX:XX:XX:XX:XX(XX标识48位蓝牙地址的16进制的一个字节)的字符串
        ba2str(&rem_addr.rc_bdaddr, recBuf);
        std::cout << "accepted connection from " << recBuf << "\n";

        // 为每个新的客户端创建自己的线程用于接收信息
        pthread_t rec_tid;
        int err = pthread_create(&rec_tid, nullptr, recv_func, &c_fd[i]);
        if (err) {
            std::cerr << "Create pthread fail:" << strerror(err) << "\n";
            exit(1);
        }
    }
}

int main() {
    struct sockaddr_rc loc_addr = { 0 };

    int s, err;
    pthread_t send_tid, accept_tid;

    // 让本机蓝牙处于可见状态
    int ret = system("hciconfig hci0 piscan");
    if (ret < 0) {
        perror("bluetooth discovering fail");
    }
    localmac = get_mac_address();
    // allocate socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // bind socket to port 1 of the first available
    // local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = {0, 0, 0, 0, 0, 0};  // 相当于tcp的ip地址
    loc_addr.rc_channel = (uint8_t)1;  // 这里的通道就是SPP的通道，相当于网络编程里的端口

    bind(s, (struct sockaddr*)&loc_addr, sizeof(loc_addr));

    // put socket into listening mode
    listen(s, ClientMax);
    std::cout << "bluetooth_server listen success\n";
    c_fd = std::vector<int>(ClientMax, -1);

    // 创建线程用于广播消息
    err = pthread_create(&send_tid, nullptr, sendmsg_func, nullptr);
    if (err) {
        std::cerr << "Create pthread fail:" << strerror(err) << "\n";
        exit(1);
    }

    // 创建线程用于接受客户端连接
    err = pthread_create(&accept_tid, nullptr, accept_func, &s);
    if (err) {
        std::cerr << "Create pthread fail:" << strerror(err) << "\n";
        exit(1);
    }

    // 主线程等待连接成功
    while (!conn) {
        sleep(1);
    }

    std::cout << "Connection successful, exiting...\n";
    pthread_cancel(accept_tid);
    pthread_join(accept_tid, nullptr);
    pthread_cancel(send_tid);
    pthread_join(send_tid, nullptr);

    close(s);
    return 0;
}
