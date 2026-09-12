/*
IVnet backend.
Linux program for connecting the generation IV Pokemon games to the internet.

Visit https://github.com/vixthevix/IVnet for more info.
*/

// #include <openssl/evp.h>
// #include <openssl/prov_ssl.h>

#include "base64/base64.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/poll.h>
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
#include <poll.h>

#include <netinet/tcp.h>

//OpenSSL 3.0 libraries and cottage
#define ENABLE_LOCALHOST //TESTING DEFINITION
#if defined(ENABLE_LOCALHOST)
#include <openssl/ssl.h>
#include <openssl/err.h>

// //for legacy ciphers
// #include <openssl/provider.h> 
// extern OSSL_provider_init_fn ossl_legacy_provider_init;

#define COTTAGE_START
#include "cottage/cottage.h"
#endif

//TESTING DEFINITION
//#define ENABLE_PROXY_DEBUG
#if defined (ENABLE_PROXY_DEBUG)
const char* PCN_IP = "178.62.43.212";
const char* PCN_HTTPS_PORT = "443";
const char* PCN_RAWTCP_PORT = "29900";
#endif

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

#if defined(ENABLE_LOCALHOST)
/*
Creates the certificate chain file needed for initialising a local server.
@arg cert_path -> path to certificate.
@arg key_path -> path to key.
@return status of create.
*/
bool createLocalChain(const char* cert_path, const char* key_path) {
    //Files needed for chain creation
    const char* server_key = "/tmp/ivnet/server.key";
    const char* server_csr = "/tmp/ivnet/server.csr";
    const char* server_crt = "/tmp/ivnet/server.crt";
    const char* server_chain = "/tmp/ivnet/server.chain.crt";

    const char* csr_info = "\"/CN=nas.nintendowifi.net/O=Nintendo/C=JP\"";
    
    const unsigned int rsa_key_size = 1024; //in bits

    //Command buffer
    char cmd[1024] = {0};

    //Local server private key
    sprintf(cmd, "openssl genrsa -out %s %u 1>/dev/null", server_key, rsa_key_size);
    system(cmd);

    //Certificate Signing Request (contains data for the DS to verify)
    sprintf(cmd,
        "openssl req -new -sha1 -key %s -out %s -subj %s 1>/dev/null",
        server_key, server_csr, csr_info
    );
    system(cmd);

    //Server certificate, signed with function arguments.
    sprintf(cmd,
        "openssl x509 -req -sha1 -in %s -CA %s -CAkey %s -CAcreateserial -out %s -days 3650 1>/dev/null",
        server_csr, cert_path, key_path, server_crt
    );
    system(cmd); 

    //Create certificate chain file
    sprintf(cmd, "cat %s %s > %s", server_crt, cert_path, server_chain);
    system(cmd);

    //LET ME SEE THE CERTIFICATE FOR A BIT
    sprintf(cmd, "cp %s /home/vixthevix/Documents/code/personal/IVnet", server_chain);
    system(cmd);
    return true;
}

/*
Removes the temporary files used for chain creation.
*/
void cleanLocalChain(void) {
    const char* server_key = "/tmp/ivnet/server.key";
    const char* server_csr = "/tmp/ivnet/server.csr";
    const char* server_crt = "/tmp/ivnet/server.crt";
    const char* server_chain = "/tmp/ivnet/server.chain.crt";

    //Command buffer
    char cmd[1024] = {0};

    sprintf(cmd, "rm %s %s %s %s", server_key, server_csr, server_crt, server_chain);
    system(cmd);
}

/*
Encodes string into Nitro Base64 strings.
@arg plain -> string to encode.
@return encoded string.
*/
char* nitroBase64Encode(char* plain) {
    if (!plain) return NULL;
    char* cipher = base64_encode(plain);
    if (!cipher) return NULL;

    //replace '=' with '*'
    for (size_t i = 0; i < strlen(cipher); i++) {
        if (cipher[i] == '=') cipher[i] = '*'; 
    }

    return cipher;
}

/*
Decodes Nitro Base64 strings.
@arg cipher -> string to decode.
@return decoded string.
*/
char* nitroBase64Decode(char* cipher) {
    if (!cipher) return NULL;
    
    //replace '*' with '='
    for (size_t i = 0; i < strlen(cipher); i++) {
        if (cipher[i] == '*') cipher[i] = '='; 
    }
    
    char* plain = base64_decode(cipher);
    if (!plain) return NULL;

    return plain;
}

/*
Takes in contents of Nitro encoded data and
makes mapping of keys to plain text.
@arg data -> Nitro encoded data.
@return mapping of keys to plain text.
*/
stringMap* strMapNitroDecode(char* data) {
    if (!data) return NULL;

    stringMap* map = strMapInit();
    if (!map) return NULL;

    const int keyvalMax = 256;
    char key[256] = {0};
    char value[256] = {0};
    bool is_key = true;
    int keyval_index = 0;
    for (size_t i = 0; i < strlen(data); i++) {

        //Value can be URL decoded sometimes.
        //Due to nature of Nitro Base64 encoding, we cannot use cottage's urlDecode.
        //Instead, check for edge cases.
        if (data[i] == '%' && data[i + 1] && data[i + 2]) { 
            char check = 0;

            if (data[i+1] == '2' && (data[i+2] == 'A' || data[i+2] == 'a')) {
                // * symbol
                check = '*';
            }
            else if (data[i+1] == '2' && (data[i+2] == 'B' || data[i+2] == 'b')) {
                // + symbol
                check = '+';
            }
            else if (data[i+1] == '2' && (data[i+2] == 'F' || data[i+2] == 'f')) {
                // / symbol
                check = '/';
            }

            if (check) {
                if (keyval_index < keyvalMax - 1) { //Bounds check
                    if (is_key) key[keyval_index++] = check;
                    else value[keyval_index++] = check;
                }
                i += 2; //Skip "%__"
                continue;
            }
        }
        
        if (data[i] == '=') {
            //Switching to value
            is_key = false;
            keyval_index = 0;
            continue;
        }
        else if (data[i] == '&') {
            is_key = true;
            keyval_index = 0;

            //We have our key and value.
            //Decode, then store
            char* plain = nitroBase64Decode(value);
            if (plain) {
                fprintf(stderr, "%s: %s\n", key, plain);
                strMapInsert(&map, key, plain);
                free(plain);
            }

            memset(key, 0, keyvalMax);
            memset(value, 0, keyvalMax);
            continue;
        }
        if (is_key) key[keyval_index++] = data[i];
        else value[keyval_index++] = data[i];
    }
    //if key and value left over, put them in.
    if (!is_key && key[0] && value[0]) {
        char* plain = nitroBase64Decode(value);
        if (plain) {
            fprintf(stderr, "%s: %s\n", key, plain);
            strMapInsert(&map, key, plain);
            free(plain);
        }
    }

    return map;
}

/*
Takes in contents of a stringMap and converts 
into a Nitro encoded NWC standard payload.
@arg map -> mapping of keys to plaintext.
@return encoded string.
*/
char* strMapNitroEncode(stringMap* map) {
    if (!map) return NULL;

    dataVector vector = dataVectorInit(32);
    if (!vector.data) return NULL;

    size_t curCount = 0;
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->items[i]) {
            char* key = map->items[i]->key;
            char* value_plain = map->items[i]->value;
            if (!key || !value_plain) continue;
            curCount++;
            
            char* value_cipher = nitroBase64Encode(value_plain);
            if (!value_cipher) continue;

            dataVectorPushString(&vector, key);
            dataVectorPush(&vector, '=');
            dataVectorPushString(&vector, value_cipher);
            if (curCount < map->count) dataVectorPushString(&vector, "&");
            else                       dataVectorPushString(&vector, "\r\n");

            free(value_cipher);
        }
    }

    return vector.data;
}

/*
Generates a random string of a given length.
@arg buffer -> stores random string.
@arg len -> length of buffer.
*/
void generateRandomString(char *buffer, size_t len) {
    char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (len-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        *buffer++ = charset[index];
    }
    *buffer = '\0';
}

/*
Manages an IVnet HTTP local server.
@arg server -> HTTP ServerConfig to manage.
@arg timeout -> polling timeout limit.
*/
void HTTP_manage(ServerConfig* server, int timeout) {
    int ready_count = CotPollPoll(server->poll, timeout);
    for (int i = 0; i < ready_count; i++) {
        int cur_fd = CotPollAccess(server->poll, i);
        if (cur_fd == server->server_fd) {
            //new client
            int client_fd = serverAcceptClient(server);
            if (client_fd < 0) continue;
            CotPollPush(server->poll, client_fd); 
        }
        else {
            //existing client
            int bytes = 0;
            char* client_offload = serverRecvClient(cur_fd, &bytes);
            HttpRequest request;
            if (splitHttpRequest(&request, client_offload).status == COT_ERROR) {
                fprintf(stderr, "Could not split HTTP request\n");
                //goto cleanup;
            }
            fprintf(stderr, "DS REQUEST:\n%s\n", client_offload);

            if (HttpRequestValid(request) && request.type == GET) {
                //Assuming its the connection test, send a default response.
                fprintf(stderr, "GET HTTP request valid.\n\n");
                HttpResponse response;
                HttpResponseInit(&response, request.version, HttpStatus_OK);
                
                HttpResponseAddOption(&response, "Content-type", "text/html");
                HttpResponseAddOption(&response, "X-Organization", "Nintendo");
                HttpResponseAddOption(&response, "Server", "BigIp");
                HttpResponseAddOption(&response, "Content-length", "2");
                
                HttpResponseAddPayload(&response, "ok", strlen("ok"));

                char* response_string = buildHttpResponse(response);
                size_t response_size = HttpResponseTotalSize(response);
                send(cur_fd, response_string, response_size, 0);

                HttpResponseFree(response);
            }

            HttpRequestFree(request);
            if (client_offload) free(client_offload);
            CotPollPop(server->poll, cur_fd);
            serverCloseClient(cur_fd);
        }
    }
}

/*
Manages an IVnet HTTPS local server.
@arg server -> HTTPS ServerConfig to manage.
@arg timeout -> polling timeout limit.
@arg ctx -> OpenSSL Context for verifying packets.
*/
void HTTPS_manage(ServerConfig* server, int timeout, SSL_CTX* ctx) {
    int ready_count = CotPollPoll(server->poll, timeout);
    for (int index = 0; index < ready_count; index++) {
        int cur_fd = CotPollAccess(server->poll, index);
        if (cur_fd == server->server_fd) {
            //new client
            int client_fd = serverAcceptClient(server);
            if (client_fd < 0) continue;
            CotPollPush(server->poll, client_fd); 
        }
        else {
            //existing client

            //Ensure we send whole packets, instead of Linux default waiting
            int nodelay_flag = 1;
            setsockopt(cur_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay_flag, sizeof(int));

            //Allow SSL to decrypt the message.
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, cur_fd); //attach our current client to SSL.

            //Perform the TLS handshake
            //Due to CotPoll being non blocking, we need to account for this
            //using wait loops
            const int ssl_accept_timeout = 1000; //milliseconds
            int ssl_accept = 0;
            while ((ssl_accept = SSL_accept(ssl)) <= 0) {
                int ssl_err = SSL_get_error(ssl, ssl_accept);

                if (ssl_err == SSL_ERROR_WANT_READ) {
                    //Waiting to read from client, so pause
                    struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLIN};
                    if (poll(&ssl_pfd, 1, ssl_accept_timeout) <= 0) {
                        fprintf(stderr, "HANDSHAKE READ TIMEOUT\n");
                        break;
                    }
                    
                }
                else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                    //Waiting to write to client, so pause
                    struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLOUT};
                    if (poll(&ssl_pfd, 1, ssl_accept_timeout) <= 0) {
                        fprintf(stderr, "HANDSHAKE WRITE TIMEOUT\n");
                        break;
                    }
                }
                else {
                    //Actual error
                    ERR_print_errors_fp(stderr);
                    fprintf(stderr, "HANDSHAKE FAILED, CODE: %i\n", ssl_err);
                    break;
                }
            }
            if (ssl_accept == 1) { //success
                //We can now decrypt our NDS messages
                //Also use wait polling here
                char client_offload[2048] = {0}; 
                int bytes = 0;
                //while we think we are reading...
                while ((bytes = SSL_read(ssl, client_offload, sizeof(client_offload) - 1)) <= 0) {
                    int ssl_err = SSL_get_error(ssl, bytes);

                    if (ssl_err == SSL_ERROR_WANT_READ) {
                        //Waiting to read from client, so pause
                        struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLIN};
                        poll(&ssl_pfd, 1, ssl_accept_timeout); 
                    }
                    else {
                        //error
                        break;
                    }
                }

                //bytes = SSL_read(ssl, client_offload, sizeof(client_offload) - 1); //for null terminator
                if (bytes > 0) fprintf(stderr, "DECRYPTED DS REQUEST:\n%s\n", client_offload);
                else fprintf(stderr, "NO ENCRYPTED DS MESSAGE FOUND\n");

                //now with our client offload, we can wrap it in a HttpRequest
                HttpRequest request;
                memset(&request, 0, sizeof(HttpRequest));
                if (splitHttpRequest(&request, client_offload).status == COT_ERROR) {
                    fprintf(stderr, "Could not wrap HTTP request.");
                    goto cleanup;
                }
                
                //To uniquely identify HTTPS requests from the DS, we need to
                //get string maps of the Nitro encoded and decoded payload.

                stringMap* payload_vars = strMapNitroDecode(request.payload);
                if (payload_vars) fprintf(stderr, "PAYLOAD FULLY DECRYPTED\n\n");
                
                //Now, we can check the specific HTTPS request.
                HttpResponse response;
                char* response_string = NULL;
                if (HttpResponseInit(&response, 1.0, HttpStatus_OK).status == COT_ERROR) {
                    fprintf(stderr, "FAILED TO BUILD NAS RESPONSE.\n");
                    goto cleanup;
                }

                //constants for mystery gift
                const char* svchost = "local.ivnet.net";
                const char* test_gift_location = "/home/vixthevix/Documents/code/personal/IVnet/private/gifts/amemone_shroomish.myg";
                const char* test_gift = "amemone_shroomish.myg";

                char* check_buffer = NULL;

                //Authentication
                if (
                    (strMapGet(request.options, "Host")) && strcmp(strMapGet(request.options, "Host"), "nas.nintendowifi.net") == 0 &&
                    request.type == POST &&
                    strcmp(request.target, "/ac") == 0 &&
                    (strMapGet(payload_vars, "action")) && strcmp(strMapGet(payload_vars, "action"), "login") == 0
                ) {
                    fprintf(stderr, "AUTHENTICATION: start\n\n");
                    stringMap* response_vars = strMapInit();
                    if (!response_vars) goto cleanup;

                    strMapInsert(&response_vars, "retry", "0");
                    //001 -> success
                    strMapInsert(&response_vars, "returncd", "001");
                    strMapInsert(&response_vars, "locator", "gamespy.com");
                    //Challenge must be 8 characters long
                    strMapInsert(&response_vars, "challenge", "12345678");
                    strMapInsert(&response_vars, "datetime", "20260910143832");
                    //Token must be "NDS" + some number of characters.
                    //Not sure if these characters matter too much. so make it whatever you want.
                    strMapInsert(&response_vars, "token", "NDS/IVnet");

                    char* response_payload = strMapNitroEncode(response_vars);
                    if (!response_payload) goto cleanup;

                    fprintf(stderr, "AUTHENTICATION: response_payload\n\n");

                    char response_payload_size[100] = {0};
                    sprintf(response_payload_size, "%zu", strlen(response_payload));

                    //Options
                    HttpResponseAddOption(&response, "Content-Type", "text/plain;charset=UTF-8");
                    HttpResponseAddOption(&response, "Connection", "close");
                    HttpResponseAddOption(&response, "Content-Length", response_payload_size);
                    HttpResponseAddOption(&response, "NODE", "wifiappw3");
                    HttpResponseAddOption(&response, "Server", "IVnet");
                    HttpResponseAddOption(&response, "Date", "Christmas");
                    HttpResponseAddOption(&response, "Vary", "Accept-Encoding");
                    HttpResponseAddOption(&response, "Duration", "D=0 usec");

                    fprintf(stderr, "AUTHENTICATION: response options\n\n");

                    //Payload
                    HttpResponseAddPayload(&response, response_payload, strlen(response_payload));
                    fprintf(stderr, "\nRESPONSE PAYLOAD:\n%s\n\n", response.payload);

                    free(response_payload);
                    strMapFree(response_vars);
                    fprintf(stderr, "AUTHENTICATION: done\n\n");
                }
                //Mystery gift start request
                else if (
                    (strMapGet(request.options, "Host")) && strcmp(strMapGet(request.options, "Host"), "nas.nintendowifi.net") == 0 &&
                    request.type == POST &&
                    strcmp(request.target, "/ac") == 0 &&
                    (strMapGet(payload_vars, "action")) && strcmp(strMapGet(payload_vars, "action"), "SVCLOC") == 0
                ) {
                    /*
                    Response options look pretty much the same as login.
                    Payload options has two tokens, a returncd of 007, and statusdata of Y.
                    Also has a svchost, which can be whatever we want as far as im concerned.
                    */
                    stringMap* response_vars = strMapInit();
                    if (!response_vars) goto cleanup;

                    strMapInsert(&response_vars, "retry", "0");
                    //007 -> Server found (AKA mystery gifts exist.)
                    //Maybe check first if there are local gifts available first,
                    //to return either 000 or 007
                    strMapInsert(&response_vars, "returncd", "007");
                    strMapInsert(&response_vars, "datetime", "20260910143832");
                    //Token must be "NDS" + some number of characters.
                    //Not sure if these characters matter too much. so make it whatever you want.
                    //Servicetoken copies token.
                    strMapInsert(&response_vars, "token", "NDS/IVnet");
                    strMapInsert(&response_vars, "servicetoken", "NDS/IVnet");
                    //Status of server (active or not)
                    strMapInsert(&response_vars, "statusdata", "Y");
                    //Mystery gift host (VERY IMPORTANT)
                    strMapInsert(&response_vars, "svchost", svchost);

                    char* response_payload = strMapNitroEncode(response_vars);
                    if (!response_payload) goto cleanup;

                    char response_payload_size[100] = {0};
                    sprintf(response_payload_size, "%zu", strlen(response_payload));


                    HttpResponseAddOption(&response, "Content-Type", "text/plain;charset=UTF-8");
                    HttpResponseAddOption(&response, "Connection", "close");
                    HttpResponseAddOption(&response, "Content-Length", response_payload_size);
                    HttpResponseAddOption(&response, "NODE", "wifiappw3");
                    HttpResponseAddOption(&response, "Server", "IVnet");
                    HttpResponseAddOption(&response, "Date", "Christmas");
                    HttpResponseAddOption(&response, "Vary", "Accept-Encoding");
                    HttpResponseAddOption(&response, "Duration", "D=0 usec");
                    
                    HttpResponseAddPayload(&response, response_payload, strlen(response_payload));
                    fprintf(stderr, "\nRESPONSE PAYLOAD:\n%s\n\n", response.payload);
                }
                //Mystery gift "count" request
                else if (
                    request.type == POST &&
                    (request.target) && strcmp(request.target, "/download") == 0 &&
                    (strMapGet(request.options, "Host")) && strcmp(strMapGet(request.options, "Host"), svchost) == 0 &&
                    (strMapGet(payload_vars, "action")) && strcmp(strMapGet(payload_vars, "action"), "count") == 0
                ) {
                    //Very simple response, its the number of mystery gifts to be sent.
                    //Almost always respond with 1. Maybe experiment with this?
                    const unsigned gift_count = 1;
                    char gift_count_str[10] = {0};
                    sprintf(gift_count_str, "%u", gift_count);

                    const unsigned gift_count_len = strlen(gift_count_str);
                    char gift_count_len_str[10] = {0};
                    sprintf(gift_count_len_str, "%u", gift_count_len);

                    HttpResponseAddOption(&response, "Content-Type", "text/plain");
                    HttpResponseAddOption(&response, "Connection", "close");
                    HttpResponseAddOption(&response, "Content-Length", gift_count_len_str);
                    HttpResponseAddOption(&response, "Server", "IVnet");
                    HttpResponseAddOption(&response, "Date", "Christmas");

                    HttpResponseAddPayload(&response, gift_count_str, gift_count_len);
                    fprintf(stderr, "\nRESPONSE PAYLOAD:\n%s\n\n", response.payload);
                }
                //Mystery gift "list" request
                else if (
                    request.type == POST &&
                    (request.target) && strcmp(request.target, "/download") == 0 &&
                    (strMapGet(request.options, "Host")) && strcmp(strMapGet(request.options, "Host"), svchost) == 0 &&
                    (strMapGet(payload_vars, "action")) && strcmp(strMapGet(payload_vars, "action"), "list") == 0
                ) {
                    //We show all our mystery gifts
                    //Look through MysteryGift .myg folder, to find "count" random gifts.
                    /*
                    It appears the "num" parameter may be some sort of max buffer.
                    "offset" indicates how many gifts it has received so far.
                    this means we must keep track of "offset" and our previous "count"
                    and send a maximum of 10 gifts at a time.
                    For now, we just hardcode 1.
                    
                    202dppUNalarmclock.myg					936
                    each line of the gift ends with \r\n.
                    5 tab spaces between gift name and gift size.
                    In practice, use a dataVector for this most likely.
                    */
                    dataVector list_response = dataVectorInit(64);
                    dataVectorPushString(&list_response, test_gift);
                    dataVectorPushString(&list_response, "\t\t\t\t\t936");
                    dataVectorPushString(&list_response, "\r\n");

                    char list_response_size[10] = {0};
                    sprintf(list_response_size, "%lu", strlen(list_response.data));
                    
                    HttpResponseAddOption(&response, "Content-Type", "text/plain");
                    HttpResponseAddOption(&response, "Connection", "close");
                    HttpResponseAddOption(&response, "Content-Length", list_response_size);
                    HttpResponseAddOption(&response, "Server", "IVnet");
                    HttpResponseAddOption(&response, "Date", "Christmas");

                    HttpResponseAddPayload(&response, list_response.data, strlen(list_response.data));
                    fprintf(stderr, "\nRESPONSE PAYLOAD:\n%s\n\n", response.payload);
                }
                //Mystery gift "contents" request
                else if (
                    request.type == POST &&
                    (request.target) && strcmp(request.target, "/download") == 0 &&
                    (strMapGet(request.options, "Host")) && strcmp(strMapGet(request.options, "Host"), svchost) == 0 &&
                    (strMapGet(payload_vars, "action")) && strcmp(strMapGet(payload_vars, "action"), "contents") == 0
                ) {
                    //Finally, we send our .myg file.
                    FILE* shroom_file = fopen(test_gift_location, "rb");
                    if (!shroom_file) goto cleanup;

                    const int size = 936;
                    char buffer[1200] = {0};
                    fread(buffer, 1, size, shroom_file);
                    fclose(shroom_file);

                    //We ned a little something for content disposition option.
                    char content_disposition[100] = {0};
                    sprintf(content_disposition, "attachment; filename = \"%s\"", test_gift);
                    
                    HttpResponseAddOption(&response, "Content-Type", "application/x-dsdl");
                    HttpResponseAddOption(&response, "Connection", "close");
                    HttpResponseAddOption(&response, "Content-Length", "936");
                    HttpResponseAddOption(&response, "Server", "IVnet");
                    HttpResponseAddOption(&response, "Date", "Christmas");
                    HttpResponseAddOption(&response, "Content-Disposition", content_disposition);
                    
                    HttpResponseAddPayload(&response, buffer, size);
                    fprintf(stderr, "\nRESPONSE PAYLOAD:\n%s\n\n", response.payload);
                    fprintf(stderr, "%s has been sent off!\n\n", test_gift);
                }
                else {
                    //No case for this specific HTTPS request.
                    fprintf(stderr, "NO HTTPS REQUEST CASE FOUND.\n\n");
                    goto cleanup;
                }

                //Response set up
                response_string = buildHttpResponse(response);
                if (!response_string) {
                    fprintf(stderr, "FAILED TO BUILD NAS RESPONSE STRING.\n");
                    HttpResponseFree(response);
                    goto cleanup;
                }
                fprintf(stderr, "\nRESPONSE TO SEND:\n%s\n\n", response_string);


                //With our response string finished, we need to carefully write to the DS.
                int total_bytes = HttpResponseTotalSize(response);
                int cur_bytes = 0;
                while (cur_bytes < total_bytes) {
                    int written = SSL_write(ssl, response_string + cur_bytes, total_bytes - cur_bytes);
                    if (written <= 0) {
                        int err = SSL_get_error(ssl, written);
                        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                            //Buffer full, continue writing.
                            usleep(1000);
                            continue;
                        }
                        else break; //Error
                    }
                    cur_bytes += written;
                }
                fprintf(stderr, "HTTPS DELIVERED %d / %d BYTES\n\n", cur_bytes, total_bytes);
                
                cleanup:
                strMapFree(payload_vars);
                HttpRequestFree(request);
                HttpResponseFree(response);
                if (response_string) free(response_string);
            }

            //Shutdown gracefully.
            int shutdown_ret = SSL_shutdown(ssl);
            if (shutdown_ret == 0) {
                //Wait for the DS to confirm end of connection.
                int ret = 0;
                int timeout_count = 0;
                while ((ret = SSL_shutdown(ssl)) < 0 && timeout_count < 50) {
                    int err = SSL_get_error(ssl, ret);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                        //DS is still processing.
                        usleep(1000);
                        timeout_count++;
                    } else {
                        break;
                    }
                }
            }
            fprintf(stderr, "shut down\n\n");
            SSL_free(ssl);
            CotPollPop(server->poll, cur_fd);
            serverCloseClient(cur_fd);
        }
    }
}
#endif


#ifdef ENABLE_PROXY_DEBUG

/*
Debug function for capturing HTTP packets sent by the DS.
@arg server -> HTTP ServerConfig to manage.
@arg timeout -> polling timeout limit.
@arg output -> stream to print to.
*/
void HTTP_proxy(ServerConfig* server, int timeout, FILE** output) {
    if (!server || !output || !(*output)) {
        fprintf(stderr, "HTTP_proxy arguments invalid.\n");
        fprintf(*output, "HTTP_proxy arguments invalid.\n");
        return;
    } 
    
    int ready_count = CotPollPoll(server->poll, timeout);
    for (int i = 0; i < ready_count; i++) {
        int cur_fd = CotPollAccess(server->poll, i);
        if (cur_fd == server->server_fd) {
            //new client
            int client_fd = serverAcceptClient(server);
            if (client_fd < 0) continue;
            CotPollPush(server->poll, client_fd); 
        }
        else {
            //existing client
            fprintf(stderr, "----------HTTP PROXY DEBUG START----------\n\n");
            fprintf(*output, "----------HTTP PROXY DEBUG START----------\n\n");
            
            int bytes = 0;
            char* client_offload = serverRecvClient(cur_fd, &bytes);
            if (client_offload) {
                fprintf(stderr, "----------DS HTTP REQUEST START----------\n\n%s\n\n----------DS HTTP REQUEST END----------\n\n", client_offload);
                fprintf(*output, "----------DS HTTP REQUEST START----------\n\n%s\n\n----------DS HTTP REQUEST END----------\n\n", client_offload);
            }
            else {
                fprintf(stderr, "NO DS MESSAGE FOUND\n\n");
                fprintf(*output, "NO DS MESSAGE FOUND\n\n");
            }
            fprintf(stderr, "----------HTTP PROXY DEBUG END----------\n\n");
            fprintf(*output, "----------HTTP PROXY DEBUG END----------\n\n");
            

            //Default HTTP response code.
            HttpRequest request;
            if (splitHttpRequest(&request, client_offload).status == COT_ERROR) {
                fprintf(stderr, "Could not split HTTP request\n");
            }

            if (HttpRequestValid(request) && request.type == GET) {
                //Assuming its the connection test, send a default response.
                HttpResponse response;
                HttpResponseInit(&response, request.version, HttpStatus_OK);
                
                HttpResponseAddOption(&response, "Content-type", "text/html");
                HttpResponseAddOption(&response, "X-Organization", "Nintendo");
                HttpResponseAddOption(&response, "Server", "BigIp");
                HttpResponseAddOption(&response, "Content-length", "2");
                
                response.payload = (char*) calloc(3, sizeof(char));
                strcpy(response.payload, "ok");

                sendCustom(response, cur_fd);
            }

            HttpRequestFree(request);
            if (client_offload) free(client_offload);
            CotPollPop(server->poll, cur_fd);
            serverCloseClient(cur_fd);
        }
    }
}

/*
Debug function for capturing HTTPS packets sent between the DS and
the Pokemon Classic Network.
@arg server -> HTTPS ServerConfig proxy.
@arg timeout -> polling timeout limit.
@arg ctx -> OpenSSL Context for verifying packets.
@arg output -> stream to print to.
*/
void HTTPS_proxy(ServerConfig* server, int timeout, SSL_CTX* ctx, FILE** output) {
    if (!server || !ctx || !output || !(*output)) {
        fprintf(stderr, "HTTPS_proxy arguments invalid.\n");
        fprintf(*output, "HTTPS_proxy arguments invalid.\n");
        return;
    } 
    
    int ready_count = CotPollPoll(server->poll, timeout);
    for (int index = 0; index < ready_count; index++) {
        int cur_fd = CotPollAccess(server->poll, index);
        if (cur_fd == server->server_fd) {
            //new client
            int client_fd = serverAcceptClient(server);
            if (client_fd < 0) continue;
            CotPollPush(server->poll, client_fd); 
        }
        else {
            //existing client
            fprintf(stderr, "----------HTTPS PROXY DEBUG START----------\n\n");
            fprintf(*output, "----------HTTPS PROXY DEBUG START----------\n\n");

            //Ensure we send whole packets, instead of Linux default waiting
            int nodelay_flag = 1;
            setsockopt(cur_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay_flag, sizeof(int));

            //Allow SSL to decrypt the message.
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, cur_fd); //attach our current client to SSL.

            //Perform the TLS handshake
            //Due to CotPoll being non blocking, we need to account for this
            //using wait loops
            const int ssl_accept_timeout = 1000; //milliseconds
            int ssl_accept = 0;
            while ((ssl_accept = SSL_accept(ssl)) <= 0) {
                int ssl_err = SSL_get_error(ssl, ssl_accept);

                if (ssl_err == SSL_ERROR_WANT_READ) {
                    //Waiting to read from client, so pause
                    struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLIN};
                    if (poll(&ssl_pfd, 1, ssl_accept_timeout) <= 0) {
                        fprintf(stderr, "HANDSHAKE READ TIMEOUT\n");
                        fprintf(*output, "HANDSHAKE READ TIMEOUT\n");
                        break;
                    }
                }
                else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                    //Waiting to write to client, so pause
                    struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLOUT};
                    if (poll(&ssl_pfd, 1, ssl_accept_timeout) <= 0) {
                        fprintf(stderr, "HANDSHAKE WRITE TIMEOUT\n");
                        fprintf(*output, "HANDSHAKE WRITE TIMEOUT\n");
                        break;
                    }
                }
                else {
                    //Actual error
                    ERR_print_errors_fp(stderr);
                    fprintf(stderr, "HANDSHAKE FAILED, CODE: %i\n", ssl_err);
                    fprintf(*output, "HANDSHAKE FAILED, CODE: %i\n", ssl_err);
                    break;
                }
            }
            if (ssl_accept == 1) { //success
                //We can now decrypt our NDS messages
                //Also use wait polling here
                char client_offload[2048] = {0}; 
                int bytes = 0;
                //while we think we are reading...
                while ((bytes = SSL_read(ssl, client_offload, sizeof(client_offload) - 1)) <= 0) {
                    int ssl_err = SSL_get_error(ssl, bytes);

                    if (ssl_err == SSL_ERROR_WANT_READ) {
                        //Waiting to read from client, so pause
                        struct pollfd ssl_pfd = {.fd = cur_fd, .events=POLLIN};
                        poll(&ssl_pfd, 1, ssl_accept_timeout); 
                    }
                    else {
                        //error
                        break;
                    }
                }

                //bytes = SSL_read(ssl, client_offload, sizeof(client_offload) - 1); //for null terminator
                
                //Log the DS output in plain text
                if (bytes > 0) {
                    fprintf(stderr, "----------DS HTTPS REQUEST START----------\n\n%s\n\n----------DS HTTPS REQUEST END----------\n\n", client_offload);
                    fprintf(*output, "----------DS HTTPS REQUEST START----------\n\n%s\n\n----------DS HTTPS REQUEST END----------\n\n", client_offload);
                }
                else {
                    fprintf(stderr, "NO ENCRYPTED DS MESSAGE FOUND\n\n");
                    fprintf(*output, "NO ENCRYPTED DS MESSAGE FOUND\n\n");
                }

                //Deal with ilostmymind.xyz
                char target_ip[50] = {0};
                if (strstr(client_offload, "Host: dls1.ilostmymind.xyz")) strcpy(target_ip, "195.201.236.139");
                else strcpy(target_ip, PCN_IP);
                
                //Set up a connection to PCN
                int pcn_status = 0;
                struct addrinfo pcn_hints;
                struct addrinfo* pcn_servinfo;
                memset(&pcn_hints, 0, sizeof(struct addrinfo));
                pcn_hints.ai_family = AF_UNSPEC;
                pcn_hints.ai_socktype = SOCK_STREAM;

                //PCN probably listens on port 443 directly.
                pcn_status = getaddrinfo(target_ip, PCN_HTTPS_PORT, &pcn_hints, &pcn_servinfo);
                if (pcn_status != 0) {
                    fprintf(stderr, "PCN GETADDRINFO FAILED:\n%s\n\n", gai_strerror(pcn_status));
                    fprintf(*output, "PCN GETADDRINFO FAILED:\n%s\n\n", gai_strerror(pcn_status));
                    goto cleanup;
                }

                int pcn_fd = socket(pcn_servinfo->ai_family, pcn_servinfo->ai_socktype, pcn_servinfo->ai_protocol);
                if (pcn_fd <= -1) {
                    freeaddrinfo(pcn_servinfo);
                    fprintf(stderr, "PCN SOCKET FAILED\n\n");
                    fprintf(*output, "PCN SOCKET FAILED\n\n");
                    goto cleanup;
                }

                pcn_status = connect(pcn_fd, pcn_servinfo->ai_addr, pcn_servinfo->ai_addrlen);
                if (pcn_status <= -1) {
                    close(pcn_fd);
                    freeaddrinfo(pcn_servinfo);
                    fprintf(stderr, "PCN CONNECT FAILED\n\n");
                    fprintf(*output, "PCN CONNECT FAILED\n\n");
                    goto cleanup;
                }

                //Once done, wrap the socket in SSL.
                //First, generate a new context, that will accept PCN's public key.
                //Ensure it matches the same config as ctx
                const SSL_METHOD* pcn_method = TLS_client_method();
                SSL_CTX* pcn_ctx = SSL_CTX_new(pcn_method);
                
                SSL_CTX_set_security_level(pcn_ctx, 0);
                SSL_CTX_set_min_proto_version(pcn_ctx, SSL3_VERSION);
                SSL_CTX_set_max_proto_version(pcn_ctx, SSL3_VERSION);
                SSL_CTX_set_cipher_list(pcn_ctx, "RC4-MD5:RC4-SHA:DES-CBC3-SHA:@SECLEVEL=0");
                SSL_CTX_set_verify(pcn_ctx, SSL_VERIFY_NONE, NULL);                
                
                //Then, set up the handshake.
                SSL* pcn_ssl = SSL_new(pcn_ctx);
                SSL_set_fd(pcn_ssl, pcn_fd);
                pcn_status = SSL_connect(pcn_ssl);
                if (pcn_status <= 0) {
                    SSL_shutdown(pcn_ssl);
                    SSL_free(pcn_ssl);
                    close(pcn_fd);
                    freeaddrinfo(pcn_servinfo);
                    fprintf(stderr, "PCN HANDSHAKE FAILED\n\n");
                    fprintf(*output, "PCN HANDSHAKE FAILED\n\n");
                    goto cleanup;
                }

                //Also, set the HTTP version to 1.0
                //Thats just how the DS does things
                char* http_ver = strstr(client_offload, "HTTP/1.1");
                if (http_ver) {
                    http_ver[7] = '0'; // 1.1 -> 1.0
                }

                //Success! Write the DS output to PCN.
                SSL_write(pcn_ssl, client_offload, bytes);

                //Listen to PCN's response and log it.
                memset(client_offload, 0, sizeof(client_offload));
                int pcn_total_bytes = 0;
                while ((bytes = SSL_read(pcn_ssl, client_offload + pcn_total_bytes, sizeof(client_offload) - 1)) > 0) {
                    pcn_total_bytes += bytes;
                }
                if (pcn_total_bytes > 0) {
                    fprintf(stderr, "----------PCN HTTPS RESPONSE START----------\n\n%s\n\n----------PCN HTTPS RESPONSE END----------\n\n", client_offload);
                    fprintf(*output, "----------PCN HTTPS RESPONSE START----------\n\n%s\n\n----------PCN HTTPS RESPONSE END----------\n\n", client_offload);
                    
                    //CAPTURE THE MYG FILE (for IVgift analysis)
                    if (strstr(client_offload, "filename") != NULL) {
                        //The only packet with the .myg file, has "filename" in the HTTP raw buffer.
                        //We use strstr to get the Content-Length, use atoi to convert into number,
                        //then start reading client_offload from pcn_total_bytes for Content-Length
                        char* content_length_ptr = strstr(client_offload, "Content-Length: ");
                        if (content_length_ptr) {
                            content_length_ptr += strlen("Content-Length: ");
                            char content_length_str[10] = {0};
                            int i = 0;
                            while (*(content_length_ptr) != '\r') {
                                content_length_str[i++] = *content_length_ptr;
                                content_length_ptr++;
                            }
                            int content_length = atoi(content_length_str);
                            fprintf(stderr,"CLSTR: %s, CLVAL: %i\n\n", content_length_str, content_length);
                            int myg_pointer = pcn_total_bytes - content_length, myg_stride = content_length;
                            fprintf(stderr, "MYG POINTER: %i, MYG STRIDE: %i\n\n", myg_pointer, myg_stride);
                            //Open up a new binary file.
                            FILE* myg_file = fopen("/tmp/ivnet/gift.myg", "wb");
                            fwrite(&client_offload[myg_pointer], 1, myg_stride, myg_file);
                            fclose(myg_file);
                        }
                    }

                    //Forward this to the DS.
                    int pcn_cur_bytes = 0;
                    while (pcn_cur_bytes < pcn_total_bytes) {
                        int written = SSL_write(ssl, client_offload + pcn_cur_bytes, pcn_total_bytes - pcn_cur_bytes);
                        if (written <= 0) {
                            int err = SSL_get_error(ssl, written);
                            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                                //Buffer full, continue reading.
                                usleep(1000);
                                continue;
                            }
                            else break; //drop connection.
                        }
                        pcn_cur_bytes += written;
                    }
                    fprintf(stderr, "PROXY DELIVERED %d / %d BYTES\n\n", pcn_cur_bytes, pcn_total_bytes);
                    fprintf(*output, "PROXY DELIVERED %d / %d BYTES\n\n", pcn_cur_bytes, pcn_total_bytes);
                    
                    //Shutdown gracefully.
                    int shutdown_ret = SSL_shutdown(ssl);
                    if (shutdown_ret == 0) {
                        // 2. Wait for the DS to send its Close Notify alert back
                        int ret = 0;
                        while ((ret = SSL_shutdown(ssl)) < 0) {
                            int err = SSL_get_error(ssl, ret);
                            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                                usleep(1000); // Wait for the DS to acknowledge
                            } else {
                                break;
                            }
                        }
                    }

                }
                else {
                    fprintf(stderr, "PCN NO MESSAGE\n\n");
                    fprintf(*output, "PCN NO MESSAGE\n\n");
                }

                //Cleanup PCN SSL
                SSL_shutdown(pcn_ssl);
                SSL_free(pcn_ssl);
                SSL_CTX_free(pcn_ctx);
                close(pcn_fd);
            }

            cleanup:
            fprintf(stderr, "----------HTTPS PROXY DEBUG END----------\n\n");
            fprintf(*output, "----------HTTPS PROXY DEBUG END----------\n\n");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            CotPollPop(server->poll, cur_fd);
            serverCloseClient(cur_fd);
        }
    }
}

/*
Debug function for capturing GameSpy packets sent between the DS and
the Pokemon Classic Network.
@arg server -> HTTP ServerConfig to manage.
@arg timeout -> polling timeout limit.
@arg output -> stream to print to.
*/
void RAWTCP_proxy(ServerConfig* server, int timeout, FILE** output) {
    if (!server || !output || !(*output)) {
        fprintf(stderr, "HTTP_proxy arguments invalid.\n");
        fprintf(*output, "HTTP_proxy arguments invalid.\n");
        return;
    }

    //Due to multiple DS's being able to connect at once,
    //we need to link each DS socket to a PCN socket.
    //For debugging purposes, first work with 1 DS.
    static int ds_fd = 0;
    static int pcn_fd = 0;

    int ready_count = CotPollPoll(server->poll, timeout);
    for (int i = 0; i < ready_count; i++) {
        int cur_fd = CotPollAccess(server->poll, i);
        if (cur_fd == server->server_fd) {
            //new client
            int client_fd = serverAcceptClient(server);
            if (client_fd < 0) continue;
            CotPollPush(server->poll, client_fd); 
            //Update links
            ds_fd = client_fd;
            fprintf(stderr, "///////RAWTCP PROXY CONNECTION STARTED/////////\n\n");
            fprintf(*output, "///////RAWTCP PROXY CONNECTION STARTED/////////\n\n");
            
            //Set up a new PCN socket.
            int pcn_status = 0;
            struct addrinfo pcn_hints;
            struct addrinfo* pcn_servinfo;
            memset(&pcn_hints, 0, sizeof(struct addrinfo));
            pcn_hints.ai_family = AF_UNSPEC;
            pcn_hints.ai_socktype = SOCK_STREAM;

            //PCN probably listens on port 443 directly.
            pcn_status = getaddrinfo(PCN_IP, PCN_RAWTCP_PORT, &pcn_hints, &pcn_servinfo);
            if (pcn_status != 0) {
                fprintf(stderr, "PCN GETADDRINFO FAILED:\n%s\n\n", gai_strerror(pcn_status));
                fprintf(*output, "PCN GETADDRINFO FAILED:\n%s\n\n", gai_strerror(pcn_status));
                CotPollPop(server->poll, ds_fd);
                close(ds_fd);
                continue;
            }

            pcn_fd = socket(pcn_servinfo->ai_family, pcn_servinfo->ai_socktype, pcn_servinfo->ai_protocol);
            if (pcn_fd <= -1) {
                freeaddrinfo(pcn_servinfo);
                fprintf(stderr, "PCN SOCKET FAILED\n\n");
                fprintf(*output, "PCN SOCKET FAILED\n\n");
                CotPollPop(server->poll, ds_fd);
                close(ds_fd);
                continue;
            }

            pcn_status = connect(pcn_fd, pcn_servinfo->ai_addr, pcn_servinfo->ai_addrlen);
            if (pcn_status <= -1) {
                close(pcn_fd);
                freeaddrinfo(pcn_servinfo);
                fprintf(stderr, "PCN CONNECT FAILED\n\n");
                fprintf(*output, "PCN CONNECT FAILED\n\n");
                CotPollPop(server->poll, ds_fd);
                close(ds_fd);
                close(pcn_fd);
                continue;
            }

            //Add it to our polling.
            CotPollPush(server->poll, pcn_fd);

            fprintf(stderr, "///////RAWTCP PROXY PCN STARTED/////////\n\n");
            fprintf(*output, "///////RAWTCP PROXY PCN STARTED/////////\n\n");
        }
        else {
            //existing client
            fprintf(stderr, "----------RAWTCP PROXY DEBUG START----------\n\n");
            fprintf(*output, "----------RAWTCP PROXY DEBUG START----------\n\n");
            

            int bytes = 0;
            char* client_offload = serverRecvClient(cur_fd, &bytes);

            //Check who sent it.
            if (cur_fd == ds_fd) {
                if (client_offload) {
                    fprintf(stderr, "----------DS RAWTCP REQUEST START----------\n\n%s\n\n----------DS RAWTCP REQUEST END----------\n\n", client_offload);
                    fprintf(*output, "----------DS RAWTCP REQUEST START----------\n\n%s\n\n----------DS RAWTCP REQUEST END----------\n\n", client_offload);
                
                    //Forward to PCN
                    send(pcn_fd, client_offload, bytes, 0);
                }
                else {
                    fprintf(stderr, "NO DS MESSAGE FOUND\n\n");
                    fprintf(*output, "NO DS MESSAGE FOUND\n\n");
                    
                    //Close the connection.
                    CotPollPop(server->poll, ds_fd);
                    serverCloseClient(ds_fd);
                    CotPollPop(server->poll, pcn_fd);
                    serverCloseClient(pcn_fd);
                }
            }
            else if (cur_fd == pcn_fd) {
                if (client_offload) {
                    fprintf(stderr, "----------PCN RAWTCP REQUEST START----------\n\n%s\n\n----------PCN RAWTCP REQUEST END----------\n\n", client_offload);
                    fprintf(*output, "----------PCN RAWTCP REQUEST START----------\n\n%s\n\n----------PCN RAWTCP REQUEST END----------\n\n", client_offload);
                
                    //Forward to DS
                    send(ds_fd, client_offload, bytes, 0);
                }
                else {
                    fprintf(stderr, "NO PCN MESSAGE FOUND\n\n");
                    fprintf(*output, "NO PCN MESSAGE FOUND\n\n");
                
                    //Close the connection.
                    CotPollPop(server->poll, ds_fd);
                    serverCloseClient(ds_fd);
                    CotPollPop(server->poll, pcn_fd);
                    serverCloseClient(pcn_fd);
                }
            }


            fprintf(stderr, "----------RAWTCP PROXY DEBUG END----------\n\n");
            fprintf(*output, "----------RAWTCP PROXY DEBUG END----------\n\n");
            

            if (client_offload) free(client_offload);
        }
    }
}

#endif

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
    #if defined(ENABLE_LOCALHOST)
    //Enable cottage
    cottageInit();
    #endif

    #if defined(ENABLE_LOCALHOST)
    ServerConfig* server_http = NULL;
    ServerConfig* server_https = NULL;
    //ServerConfig* server_rawtcp = NULL;
    #endif

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



    //Are we hosting locally?
    bool localhost_comp = false;
    #if defined(ENABLE_LOCALHOST)
    localhost_comp = true;
    #endif

    bool localhost = strcmp(DNS, "0.0.0.0") == 0;
    char local_address[50] = {0};
    const char* port_http = "8080";
    const char* port_https = "8443";
    const char* port_rawtcp = "29900";

    if (localhost && !localhost_comp) {
        printf("IVnet:0:ENABLE_LOCALHOST comp flag not set.");
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


    //Update the frontend with the new selected DNS, due to localhost.
    if (localhost) printf("IVnet:DNS:%u.%u.%u.%u\n", dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
    else printf("IVnet:DNS:%s\n", DNS);

    //Update our local address.
    sprintf(local_address, "%u.%u.%u.%u", dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);



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
    const char* dnsmasq_contents_foreign = 
    "port=0\n"
    "interface=%1$s\n" //dongle name
    "bind-interfaces\n"
    "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,3h\n" //dongle access point range and timer
    "dhcp-option=6,%5$s\n"; //DNS

    const char* dnsmasq_contents_local = 
    "interface=%1$s\n" //dongle name
    "bind-interfaces\n"
    "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,3h\n" //dongle access point range and timer
    "dhcp-option=3,%2$u.%3$u.%4$u.%5$u\n" // <--- ADD THIS: Default Gateway
    "dhcp-option=6,%2$u.%3$u.%4$u.%5$u\n" //DNS
    "address=/#/%2$u.%3$u.%4$u.%5$u\n"; //spoofing rule: intercept every request to the server

    //USED FOR PROXY
    // const char* dnsmasq_contents_proxy = 
    // "interface=%1$s\n"
    // "bind-interfaces\n"
    // "server=178.62.43.212\n" // Use Wiimmfi's real DNS for GameSpy routing
    // "dhcp-range=%2$u.%3$u.%4$u.10,%2$u.%3$u.%4$u.50,3h\n"
    // "dhcp-option=3,%2$u.%3$u.%4$u.%5$u\n" // Gateway (crucial for routing)
    // "dhcp-option=6,%2$u.%3$u.%4$u.%5$u\n"
    // // --- REPLACE THE WILDCARD WITH THESE TWO SPECIFIC LINES ---
    // "address=/dls1.ilostmymind.xyz/%2$u.%3$u.%4$u.%5$u\n"
    // "address=/nas.nintendowifi.net/%2$u.%3$u.%4$u.%5$u\n"
    // "address=/conntest.nintendowifi.net/%2$u.%3$u.%4$u.%5$u\n";


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
    if (localhost) fprintf(dnsmasq, dnsmasq_contents_local, dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], dongle_ip[3]);
    else fprintf(dnsmasq, dnsmasq_contents_foreign, dongle_new, dongle_ip[0], dongle_ip[1], dongle_ip[2], DNS);
    fclose(dnsmasq);

    //ip_forward is a parameter file that turns your Linux computer into a router
    system("sysctl -w net.ipv4.ip_forward=1");
    // FILE* ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    // if (!ip_forward) {
    //     printf("IVnet:0:could not open ip_forward file.\n");
    //     return 1;
    // }
    // fwrite("1", sizeof(char), 1, ip_forward);
    // fclose(ip_forward);
    
    //iptables is a program that configures the Linux Firewall.
    //iptables works with multiple tables, we are using the nat table, which means "network address translation" ie. port forwarding
    //each table has a set of rules to follow called chains. We are going to follow the POSTROUTING chain within nat, which deals with altering outgoing packets from the local network
    //the MASQUERADE jump option tells us that "if we get a matching valid packet, set the source address to the router connected to the internet", to allow for outgoing packets
    sprintf(cmd, "iptables -t nat -A POSTROUTING -j MASQUERADE");
    system(cmd);

    if (localhost) {
        //we have to redirect all traffic to port 8080 for localhost.
        
        //HTTP requests
        sprintf(cmd, "iptables -t nat -A PREROUTING -i %s -d %s -p tcp --dport 80 -j REDIRECT --to-port %s", dongle_new, local_address, port_http);
        system(cmd);
        //HTTPS requests
        sprintf(cmd, "iptables -t nat -A PREROUTING -i %s -d %s -p tcp --dport 443 -j REDIRECT --to-port %s", dongle_new, local_address, port_https);
        system(cmd);

        //RAWTCP requests
        sprintf(cmd, "iptables -t nat -A PREROUTING -i %s -p tcp --dport %s -j REDIRECT --to-port %s", dongle_new, port_rawtcp, port_rawtcp);
        system(cmd);
    }


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


        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd != -1) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }

        //Debug code for hostapd
        //redirect stdin to null to disconnect from frontend-backend communication
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
        goto cleanup;
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
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd != -1) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
            
            char* dnsmasq_args[] = {
                "dnsmasq",
                "-C",
                "/tmp/ivnet/dnsmasq.conf",
                "-d", //no daemon mode, for debugging purposes (yeah its just debug mode)
                NULL
            };
            execvp("dnsmasq", dnsmasq_args);
            perror("Failed to start dnsmasq\n");
            goto cleanup;
        }
    }

    usleep(100000); // Give children 100ms to throw an error if they fail
    if (kill(dnsmasq_p, 0) != 0) {
        printf("IVnet:0:dnsmasq failed to start. Check configuration syntax.\n");
        kill(hostapd_p, SIGKILL);
        goto cleanup;
    }
    if (kill(hostapd_p, 0) != 0) {
        printf("IVnet:0:hostapd failed to start.\n");
        goto cleanup;
    }
    
    //If child processes did not fail to start, we're golden.
    printf("IVnet:1:Success!\n");


    if (localhost) {
        #if defined(ENABLE_LOCALHOST)
        //We need an IP address and port.

        const int client_max = 64; //64 systems should be good.

        //Set up our servers
        server_http = serverInit(local_address, port_http, client_max);
        if (!server_http) {
            printf("IVnet:0:could not set up cotttage HTTP server.");
            goto cleanup;   
        }
        server_https = serverInit(local_address, port_https, client_max);
        if (!server_https) {
            printf("IVnet:0:could not set up cotttage HTTPS server.");
            goto cleanup;   
        }
        // server_rawtcp = serverInit(local_address, port_rawtcp, client_max);
        // if (!server_https) {
        //     printf("IVnet:0:could not set up cotttage HTTPS server.");
        //     goto cleanup;   
        // }
        //Set up OpenSSL

        //Normal OpenSSL setup
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        //SSL context
        //const SSL_METHOD* method = TLS_server_method();
        const SSL_METHOD* method = SSLv23_server_method();
        SSL_CTX* ctx = SSL_CTX_new(method);
        if (!ctx) {
            ERR_print_errors_fp(stderr); //OpenSSL error handling
            printf("IVnet:0:could not set up OpenSSL.");
            goto cleanup;
        }

        //Drop security to accept SSLv3 (needed for DS)
        SSL_CTX_set_security_level(ctx, 0);
        
        //ONLY SSLv3
        SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
        SSL_CTX_set_max_proto_version(ctx, SSL3_VERSION);
        
        //Update cipher list to include older ciphers
        //SSL_CTX_set_cipher_list(ctx, "ALL:@SECLEVEL=0");
        if (SSL_CTX_set_cipher_list(ctx, "RC4-MD5:RC4-SHA:DES-CBC3-SHA:@SECLEVEL=0") != 1) {
            printf("IVnet:0:failed to set cipher list");
            goto cleanup;
        }
        //For compatibility with possibly broken SSL implementations
        SSL_CTX_set_options(ctx, SSL_OP_ALL);

        //Create the chain file, using certificate and key.
        //Should be passed in as arguments to ivnetback.
        // if (!createLocalChain("", "")) {
        if (!createLocalChain("/home/vixthevix/Documents/code/personal/IVnet/private/nwc2.crt", "/home/vixthevix/Documents/code/personal/IVnet/private/nwc2.key")) {
            printf("IVnet:0:could not create files needed for localhost");
            goto cleanup;
        }

        //Load chain file
        if (SSL_CTX_use_certificate_chain_file(ctx, "/tmp/ivnet/server.chain.crt") <= 0) {
            printf("IVnet:0:failed to load chain file");
            goto cleanup;
        }
        //Load server private key
        if (SSL_CTX_use_PrivateKey_file(ctx, "/tmp/ivnet/server.key", SSL_FILETYPE_PEM) <= 0) {
            printf("IVnet:0:failed to load server private key");
            goto cleanup;
        }
        //Verify server private key with certificate public key
        if (!SSL_CTX_check_private_key(ctx)) {
            printf("IVnet:0:could not verify private key");
            goto cleanup;
        }

        //Success! We can now decrypt NDS messages.
        cleanLocalChain();

        #ifdef ENABLE_PROXY_DEBUG
        FILE* proxy_output = fopen("/home/vixthevix/Documents/code/personal/IVnet/PROXY.debug", "w");
        #endif

        char wait_buffer;
        while (running) {
            //Because we are running a server, we have to have non-blocking checks for frontend connection status.
            struct pollfd stdin_state = {.fd = STDIN_FILENO, .events=POLLIN};
            if (poll(&stdin_state, 1, 0) > 0) { //last parameter is timeout. 0 means insant.
                if (read(STDIN_FILENO, &wait_buffer, 1) <= 0) break;
            }

            //Server polling
            const int timeout = 100; //milliseconds
            #ifndef ENABLE_PROXY_DEBUG
            HTTP_manage(server_http, timeout);
            HTTPS_manage(server_https, timeout, ctx);
            #else
            HTTP_proxy(server_http, timeout, &proxy_output);
            HTTPS_proxy(server_https, timeout, ctx, &proxy_output);
            //RAWTCP_proxy(server_rawtcp, timeout, &proxy_output);
            #endif
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

        #ifdef ENABLE_PROXY_DEBUG
        fclose(proxy_output);
        #endif

        #endif
    }
    else {
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
    }

    cleanup:
    perror("Killing backend...\n");
    
    #if defined(ENABLE_LOCALHOST)
    if (server_http) serverClose(server_http);
    if (server_https) serverClose(server_https);
    #endif

    if (hostapd_p > 0) kill(hostapd_p, SIGKILL);
    if (dnsmasq_p > 0) kill(dnsmasq_p, SIGKILL);

    //Disable iproutes outgoing traffic
    sprintf(cmd, "iptables -t nat -D POSTROUTING -j MASQUERADE");
    system(cmd);

    if (localhost) {
        //Stop rerouting to ports
        sprintf(cmd, "iptables -t nat -D PREROUTING -i %s -d %s -p tcp --dport 80 -j REDIRECT --to-port %s", dongle_new, local_address, port_http);
        system(cmd);
        sprintf(cmd, "iptables -t nat -D PREROUTING -i %s -d %s -p tcp --dport 443 -j REDIRECT --to-port %s", dongle_new, local_address, port_https);
        system(cmd);
        sprintf(cmd, "iptables -t nat -D PREROUTING -i %s -p tcp --dport %s -j REDIRECT --to-port %s", dongle_new, port_rawtcp, port_rawtcp);
        system(cmd);
    }

    //revert ip_forward
    system("sysctl -w net.ipv4.ip_forward=0");
    // ip_forward = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    // if (ip_forward) {
    //     fwrite("0", sizeof(char), 1, ip_forward);
    //     fclose(ip_forward);

    // }

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
