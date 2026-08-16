/*
IVnet is a Linux-only program that utilises the OS to create an unsafe Access Point for a Nintendo DS Gen 4 Pokemon game to connect to.
It utilises the Pokemon Classic Network to allow for online access and Mystery Gift support.
Requires the use of an external dumb WiFI dongle that is supported by the Linux kernel.
For development, follow Vanilla by MattKC for Linux-to-WiFidongle support to update the main GH page.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>

unsigned char* ip_to_bytes(struct sockaddr* sa) {
    if (!sa) return NULL;
    

    //we can store both an ipv4 and ipv6 address into a 16 byte array.
    unsigned char* ip_num = (char*)calloc(16, 1);
    
    sa_family_t f = sa->sa_family;

    if (f == AF_INET) {
        struct sockaddr_in* sa_v4 = (struct sockaddr_in*)sa;
        memcpy(ip_num, &(sa_v4->sin_addr), 4);
    }
    else if (f == AF_INET6) {
        struct sockaddr_in6* sa_v6 = (struct sockaddr_in6*)sa;
        memcpy(ip_num, &(sa_v6->sin6_addr), 16);
    }
    else {
        free(ip_num);
        ip_num = NULL;
    }

    return ip_num;
}

typedef struct IpCandidate {
    unsigned char* ip;
    unsigned char* netmask;
    sa_family_t family;
    struct IpCandidate* next;
} IpCandidate;

void freeIpCandidates(IpCandidate* head) {
    if (!head) return;

    freeIpCandidates(head->next);

    if (head && head->ip) free(head->ip);
    if (head && head->netmask) free(head->netmask);
    free(head);
}

int main(int argc, char** argv) {
    //we need a couple things:
    //  the name of the dongle as it appears under the WiFi interface devices (use a command to get this)
    //  the ip address to assign to the dongle (if left blank, give a default one)
    //  the DNS to use 
    
    if (argc < 3) {
        printf("Sorry, IVnet requires 2 arguments: the name of the WiFi dongle and the DNS to connect to (0 for default).\n");
        return 1;
    }

    char* dongle = argv[1];
    //char* dongleIP = argv[2];
    char* DNS = argv[2];
    int status = 0;


    //actually, ignore providing an ip address, we can find one ourselves.
    struct ifaddrs* ifa_head = NULL;
    status = getifaddrs(&ifa_head);
    if (status != 0 || !ifa_head) {
        printf("Could not retreive list of IP addresses for binding with %s\n", dongle);
        return 1;
    }
    struct ifaddrs* cur_address = ifa_head;

    //char dongle_ip[50] = {0};

    IpCandidate* head = (IpCandidate*)malloc(sizeof(IpCandidate));
    memset(head, 0, sizeof(IpCandidate));
    IpCandidate* cur = head;

    int ipv4_count = 0;
    int ipv6_count = 0;

    do {
        if (!cur_address->ifa_addr || !cur_address->ifa_netmask) continue;

        sa_family_t family = cur_address->ifa_addr->sa_family; //assuming same family for both
        unsigned char* ip_num = ip_to_bytes(cur_address->ifa_addr);
        unsigned char* netmask_num = ip_to_bytes(cur_address->ifa_netmask); 

        if (ip_num && netmask_num) {
            cur->ip = ip_num;
            cur->netmask = netmask_num;
            cur->family = family;
            cur->next = (IpCandidate*)malloc(sizeof(IpCandidate));
            memset(cur->next, 0, sizeof(IpCandidate));
            cur = cur->next;
            
            if (family == AF_INET) ipv4_count++;
            else ipv6_count++;

        }


    } while((cur_address = cur_address->ifa_next) != NULL);

    if (cur) {
        if (cur->next) free(cur->next);
        cur->next = NULL;
        //free(cur);
        //cur = NULL;
    }


    cur = head;
    while (cur && cur->next) {

        printf("cur address: ");
        for (int j = 0; j < 16; j++) {
            if (cur->family == AF_INET) printf("%u ", cur->ip[j]);
            else if (cur->family == AF_INET6) printf("%02x ", cur->ip[j]);
        }
        printf("\ncur netmask: ");
        for (int j = 0; j < 16; j++) {
            if (cur->family == AF_INET) printf("%u ", cur->netmask[j]);
            else if (cur->family == AF_INET6) printf("%02x ", cur->netmask[j]);
        }
        printf("\n\n");
        cur = cur->next;
    }
    //return 0;
    //we now have a list of ip addresses
    //the standard for routers is something like 192.168.X.1/24
    //we will have to loop X from 1 to 255, and ensure that it doesnt collide with any of the already found ip addresses

    unsigned char dongle_ip[]      = {192, 168, 0,   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    //unsigned char dongle_netmask[] = {255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char* dongle_netmask = NULL;
    
    bool ipValid = false;
    for (unsigned int i = 1; i < 256; i++) {
        unsigned char i_char = (unsigned char)i;
        dongle_ip[2] = i_char;
        
        bool collided = false;
        cur = head;
        while (cur && cur->next) {
            //we only want to compare ipv4 addresses, to make life simpler
            if (cur->family == AF_INET) {
                int local_collisions = 0;
                for (int j = 0; j < 4; j++) {
                    if ((cur->ip[j] & cur->netmask[j]) == (dongle_ip[j] & cur->netmask[j])) {
                        local_collisions++;
                        //collided = true;
                    }
                }
                if (!collided) collided = (local_collisions == 4);

            }
            cur = cur->next;
        }

        if (!collided) {
            //valid ip
            printf("valid ip: %u.%u.%u.%u\n", dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
            ipValid = true;
            break;
        }

    }
    printf("loop complete\n");
    

    //perform cleanup
    freeIpCandidates(head);
    freeifaddrs(ifa_head);

    if (!ipValid) {
        printf("ip collision, error\n");
        return 1;
    }
    
    
    //with a valid ip address, we can do everything else.    

    //first, allow for NetworkManager to ignore the dongle.
    char nmcli_ignore[256] = {0};
    sprintf(nmcli_ignore, "nmcli device set %s managed no", dongle);
    system(nmcli_ignore);

    //second, assign the ip address
    char dongleIP_set[256] = {0};
    sprintf(dongleIP_set, "ip link set dev %1$s down && ip addr add %2$u.%3$u.%4$u.%5$u/24 dev %1$s && ip link set dev %1$s up", dongle, dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
    status = system(dongleIP_set);
    if (status == -1) return 1; //error
    else {
        int exit_status = WEXITSTATUS(status);
        //do something here
    }

    //third, write new config files
    //write to a folder
    status = mkdir("config", 0777);
    
    const char* hostapd_contents = 
    "interface=%s\n"
    "ssid=IVnet-Source\n"
    "hw_mode=g\n"
    "channel=6\n"
    "auth_algs=1";
    
    const char* dnsmasq_contents = 
    "interface=%1$s\n"
    "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,12h\n"
    "dhcp-options=6,%5$s\n";

    FILE* hostapd = fopen("config/hostapd.conf", "w");
    fprintf(hostapd, hostapd_contents, dongle);
    FILE* dnsmasq = fopen("config/dnsmasq.conf", "w");
    fprintf(dnsmasq, dnsmasq_contents, dongle, dongle_ip[0], dongle_ip[1], dongle_ip[2], DNS);

    fclose(hostapd);
    fclose(dnsmasq);

    //fourth, write to ip_forward and set traffic rule in iptables
    FILE* ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    fwrite("1", sizeof(char), 1, ip_forward);
    fclose(ip_forward);

    char traffic_rule[100] = {0};
    sprintf(traffic_rule, "iptables -t nat -A POSTROUTING -j MASQUERADE");
    system(traffic_rule);

    //fifth, fork two child processes
    pid_t p = fork();
    if (p < 0) return 1; //error
    else if (p == 0) { //child process
        char* hostapd_args[] = {
            "./config/dnsmasq.conf",
            NULL
        };

        execvp("hostapd", hostapd_args); 
    }
    else { //parent
        p = fork();
        if (p < 0) return 1; //error
        else if (p == 0) { //child
            char* dnsmasq_args[] = {
                "-C",
                "./config/dnsmasq.conf",
                "-d",
                NULL
            };
            execvp("dnsmasq", dnsmasq_args);
        }
    }
    //finally, wait for exit to gracefully clean up and close
}
