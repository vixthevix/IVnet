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
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>

//signal functionality

volatile sig_atomic_t running = 1;

//function to run when a signal is received
void handle_kill(int sig) {
    running = 0;
}


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
   
    
    //disable line buffering for printf messaging to work
    setvbuf(stdout, NULL, _IOLBF, 0);

    signal(SIGTERM, handle_kill); //signal for kill()
    signal(SIGINT, handle_kill);  //signal for CTRL+C
    
    

    //are we running as root?
    if (geteuid() != 0) {
        printf("0:Must run as root\n");
        return 1;
    }

    if (argc < 3) {
        printf("0:Sorry, IVnet requires 2 arguments: the name of the WiFi dongle and the DNS to connect to (0 for default).\n");
        return 1;
    }



    char* dongle = argv[1];
    //char* dongleIP = argv[2];
    char* DNS = argv[2];
    int status = 0;
    
    bool dns_default = false;
    if (strcmp(DNS, "0") == 0) {
        perror("Using default DNS...\n");
        DNS = (char*)calloc(strlen("178.62.43.212") + 1, 1);
        strcpy(DNS, "178.62.43.212");
        dns_default = true;
    }
    
    //verify dongle
    struct dirent* dentry;
    DIR* directory = opendir("/sys/class/net");
    if (!directory) {
        printf("0:could not open /sys/class/net to verify %s.\n", dongle);
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    bool dongle_valid = false;
    while ((dentry = readdir(directory)) != NULL) {
        if (strcmp(dongle, dentry->d_name) == 0) {
            dongle_valid = true;
            break;
        }
    }
    closedir(directory);
    if (!dongle_valid) {
        printf("0:%s is not a valid NIC.\n", dongle);
        if (dns_default && DNS) free(DNS);
        return 1;
    }

    //verify DNS
    if (!DNS) {
        printf("0:DNS invalid\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    char dns_valid_buffer[10] = {0};
    int dns_valid_index = 0;
    int dns_dot_count = 0;
    for (size_t i = 0; i < strlen(DNS); i++) {
        if (DNS[i] == '.') {
            dns_dot_count++;
            int num = atoi(dns_valid_buffer);
            if (num < 0 || num > 255) {
                printf("0:DNS invalid\n");
                if (dns_default && DNS) free(DNS);
                return 1;
            }
            memset(dns_valid_buffer, 0, dns_valid_index);
            dns_valid_index = 0;
            continue;
        }
        dns_valid_buffer[dns_valid_index++] = DNS[i];
        if (i == strlen(DNS) - 1) {
            int num = atoi(dns_valid_buffer);
            if (num < 0 || num > 255) {
                printf("0:DNS invalid\n");
                if (dns_default && DNS) free(DNS);
                return 1;
            }
        }
    }
    
    if (dns_dot_count != 3) {
        printf("0:DNS invalid\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }


    //actually, ignore providing an ip address, we can find one ourselves.
    struct ifaddrs* ifa_head = NULL;
    status = getifaddrs(&ifa_head);
    if (status != 0 || !ifa_head) {
        printf("0:Could not retreive list of IP addresses for binding with %s\n", dongle);
        if (dns_default && DNS) free(DNS);
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


//    cur = head;
//    while (cur && cur->next) {
//
//        printf("cur address: ");
//        for (int j = 0; j < 16; j++) {
//            if (cur->family == AF_INET) printf("%u ", cur->ip[j]);
//            else if (cur->family == AF_INET6) printf("%02x ", cur->ip[j]);
//        }
//        printf("\ncur netmask: ");
//        for (int j = 0; j < 16; j++) {
//            if (cur->family == AF_INET) printf("%u ", cur->netmask[j]);
//            else if (cur->family == AF_INET6) printf("%02x ", cur->netmask[j]);
//        }
//        printf("\n\n");
//        cur = cur->next;
//    }
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
            //printf("valid ip: %u.%u.%u.%u\n", dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
            ipValid = true;
            break;
        }

    }
    //printf("loop complete\n");
    

    //perform cleanup
    freeIpCandidates(head);
    freeifaddrs(ifa_head);

    if (!ipValid) {
        printf("0:ip collision, error\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    
    
    //with a valid ip address, we can do everything else.    

    //first, allow for NetworkManager to ignore the dongle.
//  char nmcli_ignore[256] = {0};
//  sprintf(nmcli_ignore, "nmcli device disconnect %s > /dev/null 2>&1", dongle);
//  system(nmcli_ignore);
    
//  sleep(2);

//  memset(nmcli_ignore, 0, 256);
//  sprintf(nmcli_ignore, "nmcli device set %1$s managed no", dongle);
//  system(nmcli_ignore);

//  sleep(2);
    
//  char wpa_kill[100] = {0};
//  sprintf(wpa_kill, "wpa_cli -p /run/wpa_supplicant -i %s interface_remove > /dev/null 2>&1", dongle);
//  system(wpa_kill);
    
//  sleep(2);

//  char iwctl_kill[100] = {0};
//  sprintf(iwctl_kill, "iwctl device %s disconnect > /dev/null 2>&1", dongle);
//  system(iwctl_kill);
    
//  sleep(2);

//  char iw_kill[100] = {0};
//  sprintf(iw_kill, "iw dev %s disconnect", dongle);
//  system(iw_kill);
    
//  sleep(2);
  
    
    //messing with an already configured dongle is making hostapd not work.
    //so lets just remove it and configure our own device, setting it up as an access point

    //first, get the physical identifier of the wifi dongle
    char phy[10] = {0};
    char cmd[256] = {0};
    //the awk command is like a more powerful cat. we can specifiy a pattern to print, in this case the second
    //word of the input.
    sprintf(cmd, "iw dev %s info | grep wiphy | awk '{print $2}'", dongle);
    
    //popen stands for "process open", it starts up a new process (executing param 1) and returns its file descriptor.
    FILE* phy_contents = popen(cmd, "r");
    if (phy_contents) {
        fgets(phy, sizeof(phy), phy_contents);
        pclose(phy_contents);
    }

    //if there is a newline, clear it up. strcspn returns the index where a substring first appears.
    phy[strcspn(phy, "\n")] = 0;

    if (strlen(phy) == 0) {
        printf("0:Could not get physical identifier for %s\n", dongle);
        if (dns_default && DNS) free(DNS);
        return 1;
    }


    sprintf(cmd, "nmcli device disconnect %s > /dev/null 2>&1", dongle);
    system(cmd);
    
    sleep(1);

    sprintf(cmd, "nmcli device set %1$s managed no", dongle);
    system(cmd);

    sleep(1);

    //remove the current dongle, and remake it as an access point
    //const char* dongle_old = dongle;
    const char* dongle_new = "ivnet0";

    sprintf(cmd, "iw dev %s del > /dev/null 2>&1", dongle);
    system(cmd);

    sleep(1);

    sprintf(cmd, "iw phy phy%s interface add %s type __ap", phy, dongle_new);
    system(cmd);

    sleep(1);


    //second, assign the ip address
    //char dongleIP_set[256] = {0};
    //sprintf(dongleIP_set, "ip link set dev %s down", dongle);
    //system(dongleIP_set);
    
    //sleep(2);



    system("rfkill unblock wifi");
    
    sleep(2);

    //memset(dongleIP_set, 0, 256);
    sprintf(cmd, "ip addr add %2$u.%3$u.%4$u.%5$u/24 dev %1$s", dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
    
    status = system(cmd);
    if (status == -1) {
        printf("0:could not set up %s with new ip address\n", dongle);
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    else {
        int exit_status = WEXITSTATUS(status);
        //do something here
        if (exit_status != 0) {
            printf("0:could not set up %s with new ip address\n", dongle);
            if (dns_default && DNS) free(DNS);
            return 1;
        }
    }
    
    sleep(1);

    //third, write new config files
    //write to a folder in tmp, because backend is running as root.
    //which starts in its own folder /root/
    //tmp is cleared automatically after a while so yeah.
    status = mkdir("/tmp/ivnet", 0777);
    
    //hostapd is a program that allows for a Network Interface Card to act like an Access Point, needed for the DS to connect to.
    const char* hostapd_contents = 
    "interface=%s\n" //dongle name
    "country_code=GB\n"
    "ieee80211d=1\n"
    "ssid=IVnet-Source\n"
    "hw_mode=g\n"
    "channel=6\n"
    "auth_algs=1";
    
    //dnsmasq is a program that, for our purposes, will allow the dongle to hand out ip addresses to connected devices to be recognised for communication, such as the Nintendo DS.
    const char* dnsmasq_contents = 
    "interface=%1$s\n" //dongle name
    "bind-interfaces\n"
    "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,12h\n" //dongle access point range
    "dhcp-option=6,%5$s\n"; //DNS

    FILE* hostapd = fopen("/tmp/ivnet/hostapd.conf", "w");
    if (!hostapd) {
        printf("0:could not open hostapd.conf\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    fprintf(hostapd, hostapd_contents, dongle_new);
    
    FILE* dnsmasq = fopen("/tmp/ivnet/dnsmasq.conf", "w");
    if (!dnsmasq) {
        printf("0:could not open dnsmasq.conf\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    fprintf(dnsmasq, dnsmasq_contents, dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], DNS);

    fclose(hostapd);
    fclose(dnsmasq);

    //fourth, write to ip_forward and set traffic rule in iptables
    //ip_forward is a parameter file that turns your Linux computer into a router
    FILE* ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (!ip_forward) {
        printf("0:could not open ip_forward file.\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    fwrite("1", sizeof(char), 1, ip_forward);
    fclose(ip_forward);
    
    //iptables is a program that configures the Linux Firewall.
    //iptables works with multiple tables, we are using the nat table, which means "network address translation" ie. port forwarding
    //each table has a set of rules to follow called chains. We are going to follow the POSTROUTING chain within nat, which deals with altering outgoing packets from the local network
    //the MASQUERADE jump option tells us that "if we get a matching valid packet, set the source address to the router connected to the internet", to allow for outgoing packets
    char traffic_rule[100] = {0};
    sprintf(traffic_rule, "iptables -t nat -A POSTROUTING -j MASQUERADE");
    system(traffic_rule);

    //fifth, fork two child processes
    pid_t hostapd_p = 0, dnsmasq_p = 0;

    hostapd_p = fork();
    if (hostapd_p < 0) {
        printf("0:could not fork into hostapd\n");
        if (dns_default && DNS) free(DNS);
        return 1;
    }
    else if (hostapd_p == 0) { //child process
        
        //process death if backend death
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        //redirect stdin to null to disconnect from frontend-backend communication
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd != -1) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        
        //set up a log
        int log_fd = open("/tmp/ivnet/hostapd.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd != -1) {
            //redirect stdout and stderr in the hostapd process to this file, so that we can read with cat
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        char* hostapd_args[] = {
            "hostapd",
            "-dd", //double debug
            "/tmp/ivnet/hostapd.conf",
            NULL
        };

        execvp("hostapd", hostapd_args); 
        perror("Failed to start hostpad\n");
        exit(1);
    }
    else { //parent
        dnsmasq_p = fork();
        if (dnsmasq_p < 0) {
            printf("0:could not fork into dnsmasq\n");
            if (dns_default && DNS) free(DNS);
            return 1;
        }
        else if (dnsmasq_p == 0) { //child
        
            //process death if backend death
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            
            char* dnsmasq_args[] = {
                "dnsmasq",
                "-C",
                "/tmp/ivnet/dnsmasq.conf",
                "-d", //no daemon mode, for debugging purposes (yeah its just debug mode)
                NULL
            };
            execvp("dnsmasq", dnsmasq_args);
            perror("Failed to start dnsmasq\n");
            exit(1);
        }
    }
    //finally, wait for exit to gracefully clean up and close
    
    printf("1:Success!\n");

    //to do nothing, all we need to do is wait for closure of stdin, due to pipe redirection in frontend
    //we will combine this with our standard signal so that backend can be run in terminal standalone.
    char wait_buffer;
    while (running && read(STDIN_FILENO, &wait_buffer, 1) > 0);

    perror("Killing backend...\n"); 
    //kill children
    kill(hostapd_p, SIGKILL);
    kill(dnsmasq_p, SIGKILL);

    //disable iproutes outgoing traffic
    memset(traffic_rule, 0, strlen(traffic_rule));
    sprintf(traffic_rule, "iptables -t nat -D POSTROUTING -j MASQUERADE");
    system(traffic_rule);

    //revert ip_forward
    ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (ip_forward) {
        fwrite("0", sizeof(char), 1, ip_forward);
        fclose(ip_forward);

    }
    
    //restore things
//    char ip_restore[100] = {0};
//    sprintf(ip_restore, "ip link set dev %s down", dongle);
//    system(ip_restore);

//    char iw_restore[100] = {0};
//    sprintf(iw_restore, "iw dev %s set type managed", dongle);
//    system(iw_restore);

//    char nmcli_restore[100] = {0};
//    sprintf(nmcli_restore, "nmcli device set %s managed yes", dongle);
//    system(nmcli_restore);


    sprintf(cmd, "iw dev %s del > /dev/null 2>&1", dongle_new);
    system(cmd);

    sprintf(cmd, "iw phy phy%s interface add %s type managed", phy, dongle);
    system(cmd);

    sprintf(cmd, "nmcli device set %s managed yes > /dev/null 2>&1", dongle);
    system(cmd);

    if (dns_default && DNS) free(DNS);
    
    return 0;
}
