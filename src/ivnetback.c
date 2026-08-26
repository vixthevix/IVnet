/*
IVnet backend.
Linux program for connecting the generation IV Pokemon games to the internet.

Visit https://github.com/vixthevix/IVnet for more info.
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
#include <errno.h>


//Signal data for proper process-end cleanup
volatile sig_atomic_t running = 1;

//function to run when a signal is received
void handle_kill(int sig) {
    running = 0;
}

/*
Transforms data in a sockaddr into an array of bytes,
representing IP address.
@arg sa -> sockaddr to read from.
@return IP address in byte array form.
*/
unsigned char* ip_to_bytes(struct sockaddr* sa) {
    if (!sa) return NULL;
    
    //we can store both an ipv4 and ipv6 address into a 16 byte array.
    unsigned char* ip_num = (unsigned char*)calloc(16, 1);
    
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

/*
Struct to form linked list of IP data.
*/
typedef struct IpCandidate {
    unsigned char* ip;
    unsigned char* netmask;
    sa_family_t family; //ipv4 or ipv6
    struct IpCandidate* next;
} IpCandidate;

/*
Frees a linked list of IPCandidates from data.
@arg head -> start of linked list.
*/
void freeIpCandidates(IpCandidate* head) {
    if (!head) return;

    freeIpCandidates(head->next);

    if (head && head->ip) free(head->ip);
    if (head && head->netmask) free(head->netmask);
    free(head);
}


/*
The role of the backend is to:
    Recognise and set a Network Interface Device as an Access Point.
    Set up the NID as a DHCP server to hand out IP addresses to 
    connecting devices (i.e. the DS).
    Set the target DNS to connect to.
    Run hostapd and dnsmasq to manage these.
    Clean up and restore the NID once complete.
*/
int main(int argc, char** argv) {
    //disable line buffering for printf messaging to work
    setvbuf(stdout, NULL, _IOLBF, 0);

    signal(SIGTERM, handle_kill); //signal for kill()
    signal(SIGINT, handle_kill);  //signal for CTRL+C
    
    //are we running as root?
    if (geteuid() != 0) {
        printf("IVnet:0:Must run as root\n");
        return 1;
    }
    
    /*
    arguments consist of 
        NIC, 
        DNS, 
        country code, 
        and SSID.
    */
    const int 
    arg_max = 4 + 1;
    if (argc < arg_max) {
        printf("IVnet:0:Not enough parameters\n");
        perror("IVnet requires:\nname of NIC,\nDNS to connect to,\nISO 3166-1 alpha-2 country code,\nSSID to assign\n");
        return 1;
    }



    char* dongle = argv[1];
    char* DNS = argv[2];
    char* country_code = argv[3];
    char* SSID = argv[4];
    
    int status = 0;

    //verify dongle
    struct dirent* dentry;
    DIR* directory = opendir("/sys/class/net");
    if (!directory) {
        printf("IVnet:0:could not open /sys/class/net to verify %s.\n", dongle);
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
        printf("IVnet:0:%s is not a valid NIC.\n", dongle);
        return 1;
    }

    //verify DNS
    if (!DNS) {
        printf("IVnet:0:DNS invalid\n");
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
                printf("IVnet:0:DNS invalid\n");
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
                printf("IVnet:0:DNS invalid\n");
                return 1;
            }
        }
    }
    
    if (dns_dot_count != 3) {
        printf("IVnet:0:DNS invalid\n");
        return 1;
    }
    
    //verify countrycode
    //must be 2 characters long, and be in all caps
    if (!country_code) {
        printf("IVnet:0:Country code invalid\n");
        return 1;
    }
    if (strlen(country_code) != 2) {
        printf("IVnet:0:Country code invalid, must be 2 characters long and in all caps\n");
        return 1;
    }
    if ((country_code[0] < 'A' || country_code[0] > 'Z') || (country_code[1] < 'A' || country_code[1] > 'Z')) {
        printf("IVnet:0:Country code invalid, must be 2 characters long and in all caps\n");
        return 1;
    }

    //Get all in-use IP addresses on your computer.
    struct ifaddrs* ifa_head = NULL;
    status = getifaddrs(&ifa_head);
    if (status != 0 || !ifa_head) {
        printf("IVnet:0:Could not retreive list of IP addresses for binding with %s\n", dongle);
        return 1;
    }
    struct ifaddrs* cur_address = ifa_head;


    IpCandidate* head = (IpCandidate*)malloc(sizeof(IpCandidate));
    memset(head, 0, sizeof(IpCandidate));
    IpCandidate* cur = head;

    //assign each cur_address to an IpCandidate
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
        }
    } while((cur_address = cur_address->ifa_next) != NULL);

    if (cur) {
        if (cur->next) free(cur->next);
        cur->next = NULL;
    }

    //we now have a list of ip addresses
    //the standard for routers is something like 192.168.X.1/24
    //192.168 means "home network", and routers are usually the first device on the network. 
    //we will have to loop X from 1 to 255, and ensure that it doesnt collide with any of the already found ip addresses

    unsigned char dongle_ip[] = {192, 168, 0,   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    
    bool ipValid = false;
    for (unsigned int i = 1; i < 256; i++) {
        unsigned char i_char = (unsigned char)i;
        dongle_ip[2] = i_char; //192.168.i.1
        
        bool collided = false;
        cur = head;
        while (cur && cur->next) {
            //we only want to compare ipv4 addresses, to make life simpler
            if (cur->family == AF_INET) {
                int local_collisions = 0;
                for (int j = 0; j < 4; j++) {
                    if ((cur->ip[j] & cur->netmask[j]) == (dongle_ip[j] & cur->netmask[j])) {
                        local_collisions++;
                    }
                }
                if (!collided) collided = (local_collisions == 4);
            }
            cur = cur->next;
        }

        if (!collided) {
            ipValid = true;
            break;
        }
    }
    
    //perform cleanup
    freeIpCandidates(head);
    freeifaddrs(ifa_head);

    if (!ipValid) {
        printf("IVnet:0:ip collision, error\n");
        return 1;
    }
    
    //Temporarily remove current WiFi setup of dongle,
    //and replace it as an acces point.

    const char* dongle_new = "ivnet0"; //new on-system identifier

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
        printf("IVnet:0:Could not get physical identifier for %s\n", dongle);
        return 1;
    }

    //Detach the device from NetworkManager
    sprintf(cmd, "nmcli device disconnect %s > /dev/null 2>&1", dongle);
    system(cmd);
    
    sleep(1);

    sprintf(cmd, "nmcli device set %1$s managed no", dongle);
    system(cmd);

    sleep(1);

    //Remove the dongle from the system
    

    sprintf(cmd, "iw dev %s del > /dev/null 2>&1", dongle);
    system(cmd);

    sleep(1);

    //Add it back, with new identifier and as an access point
    sprintf(cmd, "iw phy phy%s interface add %s type __ap", phy, dongle);
    system(cmd);

    sleep(1);
    
    sprintf(cmd, "ip link set dev %s down", dongle);
    system(cmd);

    sleep(1);
    
    sprintf(cmd, "ip link set dev %s name %s", dongle, dongle_new);
    system(cmd);

    sleep(1);
    
    //sprintf(cmd, "iw dev %s set down", dongle);
    //system(cmd);

    //sleep(1);



    //Clear software WiFi blocks, just in case 
    system("rfkill unblock wifi");
    
    sleep(2);

    //Attach the previously formed IP address to the dongle
    sprintf(cmd, "ip addr add %2$u.%3$u.%4$u.%5$u/24 dev %1$s", dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
    
    status = system(cmd);
    if (status == -1) {
        printf("IVnet:0:could not set up %s with new ip address\n", dongle);
        return 1;
    }
    else {
        int exit_status = WEXITSTATUS(status);
        if (exit_status != 0) {
            printf("IVnet:0:could not set up %s with new ip address\n", dongle);
            return 1;
        }
    }
    
    sleep(1);

    //third, write new config files
    //write to a folder in tmp, because backend is running as root.
    //which starts in its own folder /root/
    //tmp is cleared automatically after a while so yeah.
    status = mkdir("/tmp/ivnet", 0777);
    
    //hostapd is a program that allows for a Network Interface Device to act like an Access Point, needed for the DS to connect to.
    const char* hostapd_contents = 
    "interface=%s\n" //dongle name
    "country_code=%s\n"
    "ieee80211d=1\n"
    "ssid=%s\n"
    "hw_mode=g\n"
    "channel=6\n"
    "auth_algs=1";
    
    //dnsmasq is a program that, for our purposes, will allow the dongle to hand out ip addresses to connected devices to be recognised for communication, such as the Nintendo DS.
    //to be running for 3 hours only.
    //hands out ip address in the range 10 to 50, so a max of 40 Devices can connect at once.
    const char* dnsmasq_contents = 
    "port=0\n"
    "interface=%1$s\n" //dongle name
    "bind-interfaces\n"
    "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,3h\n" //dongle access point range and timer
    "dhcp-option=6,%5$s\n"; //DNS

    //Write out the config files
    FILE* hostapd = fopen("/tmp/ivnet/hostapd.conf", "w");
    if (!hostapd) {
        printf("IVnet:0:could not open hostapd.conf\n");
        return 1;
    }
    fprintf(hostapd, hostapd_contents, dongle_new, country_code, SSID);
    fclose(hostapd);
    FILE* dnsmasq = fopen("/tmp/ivnet/dnsmasq.conf", "w");
    if (!dnsmasq) {
        printf("IVnet:0:could not open dnsmasq.conf\n");
        return 1;
    }
    fprintf(dnsmasq, dnsmasq_contents, dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], DNS);
    fclose(dnsmasq);

    //ip_forward is a parameter file that turns your Linux computer into a router
    FILE* ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (!ip_forward) {
        printf("IVnet:0:could not open ip_forward file.\n");
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

    //Create the hostapd and dnsmasq child proceses
    pid_t hostapd_p = 0, dnsmasq_p = 0;

    hostapd_p = fork();
    if (hostapd_p < 0) {
        printf("IVnet:0:could not fork into hostapd\n");
        return 1;
    }
    else if (hostapd_p == 0) { //child process
        
        //process death if backend death
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        //Debug code for hostapd
        // //redirect stdin to null to disconnect from frontend-backend communication
        // int null_fd = open("/dev/null", O_RDONLY);
        // if (null_fd != -1) {
        //     dup2(null_fd, STDIN_FILENO);
        //     close(null_fd);
        // }
        
        // //set up a log
        // int log_fd = open("/tmp/ivnet/hostapd.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        // if (log_fd != -1) {
        //     //redirect stdout and stderr in the hostapd process to this file, so that we can read with cat
        //     dup2(log_fd, STDOUT_FILENO);
        //     dup2(log_fd, STDERR_FILENO);
        //     close(log_fd);
        // }

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
            printf("IVnet:0:could not fork into dnsmasq\n");
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
    
    //If child processes did not fail to start, we're golden.
    printf("IVnet:1:Success!\n");

    //We wait for either a termination signal (running)
    //Or for the frontend to close the signal pipe
    char wait_buffer;
    while (running && read(STDIN_FILENO, &wait_buffer, 1) > 0) {
        //check if dnsmasq or hostapd have failed
        //kill command can check status of process when signal is 0
        if (kill(hostapd_p, 0) != 0) {
            if (errno == ESRCH) {
                printf("IVnet:0:hostapd terminated early\n");
                break;
            }
        }
        if (kill(dnsmasq_p, 0) != 0) {
            if (errno == ESRCH) {
                printf("IVnet:0:dnsmasq terminated early\n");
                break;
            }
        }
    }

    perror("Killing backend...\n"); 

    kill(hostapd_p, SIGKILL);
    kill(dnsmasq_p, SIGKILL);

    //Disable iproutes outgoing traffic
    sprintf(cmd, "iptables -t nat -D POSTROUTING -j MASQUERADE");
    system(cmd);

    //revert ip_forward
    ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (ip_forward) {
        fwrite("0", sizeof(char), 1, ip_forward);
        fclose(ip_forward);

    }

    //Remove the new dongle from the system
    sprintf(cmd, "iw dev %s del > /dev/null 2>&1", dongle_new);
    system(cmd);

    //Reattach the former dongle to the system, as a managed device.
    sprintf(cmd, "iw phy phy%s interface add %s type managed", phy, dongle);
    system(cmd);

    //Give NetworkManager access again.
    sprintf(cmd, "nmcli device set %s managed yes > /dev/null 2>&1", dongle);
    system(cmd);

    return 0;
}
