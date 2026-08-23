

#include "raylib/src/raylib.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

char* extractText(const char* path) {
    if (!path) return NULL;
    FILE* file = fopen(path, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size == 0) {
        fclose(file);
        return NULL;
    }

    char* text = (char*) calloc(size + 1, sizeof(char));
    if (!text) {
        fclose(file);
        return NULL;
    }

    fread(text, size, sizeof(char), file);

    fclose(file);
    return text;
}


//to make life a little easier with multiple scenes, im gonna make some structs and functions

typedef enum SpriteType {
    IMAGE,
    TEXT,
} SpriteType;

typedef struct Sprite {
    void* data;
    SpriteType type;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    Color colour;
} Sprite;


size_t getSpriteSize(SpriteType type) {
    switch (type) {
        case IMAGE: return sizeof(Texture2D);
        case TEXT: return sizeof(char*);
    }
    return 0;
}

bool freeSprite(Sprite* sprite) {
    if (!sprite) return false;
    if (!(sprite->data)) {
        free(sprite);
        return true;
    }
    
    switch (sprite->type) {
        case IMAGE: {
            Texture2D* data = (Texture2D*)sprite->data;
            UnloadTexture(*data);
            break;
        }
        case TEXT: {
            char** data = (char**)sprite->data;
            if (*data) free(*data);
            break;
        }
    }
    free(sprite->data);
    free(sprite);
    return true;
}

#define IMAGE_WIDTH_NATIVE 0
#define IMAGE_HEIGHT_NATIVE 0
#define IMAGE_SIZE_NATIVE IMAGE_WIDTH_NATIVE, IMAGE_HEIGHT_NATIVE

#define TEXT_SIZE(size) size, size

Sprite* newSprite(SpriteType type, const char* source, int32_t x, int32_t y, uint32_t width, uint32_t height, Color colour) {
    Sprite* target = (Sprite*) calloc(1, sizeof(Sprite));
    target->x = x;
    target->y = y;
    target->width = width;
    target->height = height;
    target->colour = colour;
    target->type = type;
    
    size_t type_size = getSpriteSize(type);
    if (!type_size) goto failure;
    
    target->data = calloc(1, type_size);
    switch (type) {
        case IMAGE: {
            if (!source) goto failure;
            Texture2D* data = (Texture2D*)target->data;
            Image img = LoadImage(source);
            
            if (width == IMAGE_WIDTH_NATIVE) {
                target->width = img.width;
            }
            if (height == IMAGE_HEIGHT_NATIVE) {
                target->height = img.height;
            }

            ImageResize(&img, target->width, target->height);
            *data = LoadTextureFromImage(img);
            UnloadImage(img);
            break;
        }
        case TEXT: {
            if (!source) goto failure;
            //expect the source to be an actual string and not a filepath
            char** data = (char**)target->data;
            *data = (char*) calloc(strlen(source) + 1, sizeof(char));
            strcpy(*data, source);
            break;
        }
        default: goto failure;
    }

    return target;
    

    failure:
    freeSprite(target);
    return NULL;
}

Sprite* cloneSprite(Sprite* sprite) {
    Sprite* new = (Sprite*) malloc(sizeof(Sprite));
    new->x = sprite->x;
    new->y = sprite->y;
    new->width = sprite->width;
    new->height = sprite->height;
    new->colour = sprite->colour;
    new->type = sprite->type;
        
    //may have trouble cloning the data

    return NULL;
}

bool displaySprite(Sprite* sprite) {
    if (!sprite || !(sprite->data)) return false;
    
    switch (sprite->type) {
        case IMAGE: {
            Texture2D* data = (Texture2D*)sprite->data;
            DrawTexture(*data, sprite->x, sprite->y, sprite->colour);
            break;
        }
        case TEXT: {
            char** data = (char**)sprite->data;
            if (!(*data)) return false;
            DrawText(*data, sprite->x, sprite->y, sprite->width, sprite->colour);
            break;
        }
        default: return false;
    }
    
    return true;
}


//IMAGE specific function
bool imageHovering(Sprite* sprite) {
    if (!sprite || !sprite->data) return false;

    //we need to get the bounding x and y
    uint32_t
    x_low = sprite->x,
    y_low = sprite->y,
    x_high = sprite->x + sprite->width,
    y_high = sprite->y + sprite->height;

    Vector2 mouse = GetMousePosition();
    return ((x_low <= mouse.x && mouse.x <= x_high) && (y_low <= mouse.y && mouse.y <= y_high));
}

void imageHoveringChange(Sprite* sprite, const char* hovering_path, const char* not_hovering_path) {
    //if (!sprite || !sprite->data || sprite->type != IMAGE || !IsImageValid(hovering) || !IsImageValid(not_hovering)) return;
    if (!sprite || !sprite->data || sprite->type != IMAGE || !hovering_path || !not_hovering_path) return;
    
    if (imageHovering(sprite)) {
        //printf("hovering\n");
        Image hovering = LoadImage(hovering_path);
        ImageResize(&hovering, sprite->width, sprite->height);
        Texture2D* data = (Texture2D*)sprite->data;
        if (data) UnloadTexture(*data);
        if (data) *data = LoadTextureFromImage(hovering);
        UnloadImage(hovering);
    }
    else {
        //printf("not hovering\n");
        Image not_hovering = LoadImage(not_hovering_path);
        ImageResize(&not_hovering, sprite->width, sprite->height);
        Texture2D* data = (Texture2D*)sprite->data;
        if (data) UnloadTexture(*data);
        if (data) *data = LoadTextureFromImage(not_hovering);
        UnloadImage(not_hovering);
    }
}

void textUpdate(Sprite* sprite, const char* new_text) {
    if (!sprite || !sprite->data || sprite->type != TEXT) return;

    char** data = (char**)sprite->data;
    if (*data) free(*data);
    else return;

    *data = (char*) calloc(strlen(new_text) + 1, sizeof(char));
    strcpy(*data, new_text);
}

bool textHovering(Sprite* sprite) {
    if (!sprite || !sprite->data || sprite->type != TEXT) return false;

    //to check, we need a height and width.
    //for this, we need to count the maximum num of chars before a newline (width)
    //and number of newlines (height)
    
    uint32_t font_size = sprite->width;
    
    uint32_t width = font_size;
    uint32_t width_cur = 0;
    uint32_t height = font_size;
    char** data = (char**)sprite->data;

    for (uint32_t i = 0; i < strlen(*data); i++) {
        if ((*data)[i] != '\n') {
            char letter[2] = {(*data)[i], 0};
            width_cur += MeasureText(letter, font_size);
        }
        else {
            height += font_size;
            if (width_cur > width) width = width_cur;
            width_cur = 0;
        }
    }
    if (width_cur > width) width = width_cur;
    
    
    

    uint32_t
    x_low = sprite->x,
    y_low = sprite->y,
    x_high = sprite->x + width,
    y_high = sprite->y + height;

    Vector2 mouse = GetMousePosition();
    return ((x_low <= mouse.x && mouse.x <= x_high) && (y_low <= mouse.y && mouse.y <= y_high));

}

void textHoveringChange(Sprite* sprite, const char* hovering_text, Color hovering_colour, const char* not_hovering_text, Color not_hovering_colour) {
    if (!sprite || !sprite->data || sprite->type != TEXT) return;

    //only change text if neither is NULL
    
    if (textHovering(sprite)) {
        if (hovering_text) textUpdate(sprite, hovering_text);
        sprite->colour = hovering_colour;
    }
    else {
        if (not_hovering_text) textUpdate(sprite, not_hovering_text);
        sprite->colour = not_hovering_colour;
    }

}


typedef struct Scene {
    Sprite** sprites;
    uint32_t len;
    uint32_t capacity;
} Scene;

Scene* newScene() {
    const uint32_t initCap = 8;
    Scene* target = (Scene*) malloc(sizeof(Scene));
    if (!target) return NULL;
    
    target->sprites = (Sprite**) calloc(initCap, sizeof(Sprite*));
    target->len = 0;
    target->capacity = initCap;

    return target;
}

bool addScene(Scene* scene, Sprite* sprite) {
    if (!scene || !scene->sprites || !sprite) return false;
    const uint32_t load = scene->len / scene->capacity * 100;
    if (load > 60) {
        scene->capacity <<= 1; //double
        scene->sprites = (Sprite**) realloc(scene->sprites, sizeof(Sprite*) * scene->capacity);
    }
    scene->sprites[scene->len++] = sprite;
    return true;
}

void displayScene(Scene* scene) {
    if (!scene || !scene->sprites) return;

    for (uint32_t i = 0; i < scene->len; i++) {
        bool status = displaySprite(scene->sprites[i]);
        if (!status) printf("Could not display sprite #%u of current scene\n", i);
    }
}

bool freeScene(Scene* scene) {
    if (!scene) return false;
    if (!scene->sprites) {
        free(scene);
        return true;
    }

    for (uint32_t i = 0; i < scene->len; i++) {
        bool status = freeSprite(scene->sprites[i]);
        if (!status) printf("Could not free sprite #%u of current scene\n", i);
    }

    free(scene);
    return true;
}


int main() {
    
    const char* SSID = "IVnet";

    //IPC stuff
    pid_t back_p = 0;

    //set up 2 pipes
    //one is for backend to frontend communication, to ensure the AP has been set up
    //the other acts like a signal from frontend to backend, to turn off the backend.
    //we cant use actual signals because ivnet is not meant to run as root.
    int pipefd[2] = {0};
    int signalfd[2] = {0};
   
    SetTraceLogLevel(LOG_NONE);

    const uint32_t 
    width = 500,
    height = 500;
    InitWindow(width, height, "IVnet");
    SetTargetFPS(60);

    Scene* cur_scene = NULL;
    
    Scene* main_menu = newScene();
    
    Sprite* logo = newSprite(IMAGE, "assets/img/logo.png", 125, 0, IMAGE_SIZE_NATIVE, WHITE);

    Sprite* info = newSprite(IMAGE, "assets/img/info.png", 425, 0, 75, 75, WHITE);
    //Image info_unpressed = LoadImage("assets/img/info.png");
    //ImageResize(&info_unpressed, info->width, info->height);
    //Image info_pressed = LoadImage("assets/img/info_pressed.png");
    //ImageResize(&info_pressed, info->width, info->height);
   
    Sprite* main_start = newSprite(IMAGE, "assets/img/start.png", 125, 150, IMAGE_SIZE_NATIVE, WHITE);
    
    bool error_received = false;
    char error_message[512] = {0};
    Sprite* main_error = newSprite(TEXT, " ", 50, 200, TEXT_SIZE(20), RED); 
    //testing an animated gif
    //Sprite* teto_dance = newSprite(IMAGE, "teto_dance.gif", 0, 0, IMAGE_SIZE_NATIVE, WHITE);
    //Texture2D* teto_dance_data = (Texture2D*)teto_dance->data;
    //UnloadTexture(*teto_dance_data);
    //Image teto_dance_gif = LoadImageAnim("teto_dance.gif", &(int){ 11 });
    //*teto_dance_data = LoadTextureFromImage(teto_dance_gif);


    addScene(main_menu, logo);
    addScene(main_menu, info);
    addScene(main_menu, main_start);
    addScene(main_menu, main_error);
    //addScene(main_menu, teto_dance);
    
    
    Scene* info_menu = newScene();

    char* text = extractText("assets/text/info.txt");
    printf("TEXT IS %s\n", text);
    Sprite* info_text = newSprite(TEXT, text, 15, 150, TEXT_SIZE(20), BLACK);
    free(text);

    Sprite* info_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    
    Color teto_colour = {255, 255, 255, 100};
    Sprite* teto = newSprite(IMAGE, "assets/img/teto.png", 250, 0, 256, 256, teto_colour);
    
    addScene(info_menu, teto);
    addScene(info_menu, info_text);
    addScene(info_menu, info_return);
    
    //with the setup, we need to pick a NIC and a DNS, and then we can start connecting
    const uint32_t option_x = 100, option_y = 200, option_size = 20;
    const Color option_chosen = BLUE, option_not_chosen = BLACK;
    const uint32_t forward_x = 350, backward_x = 50, forward_y = 350, backward_y = forward_y, arrow_w = 100, arrow_h = arrow_w;

    Scene* nic_menu = newScene();

    char nic_list[256][75] = {0};
    uint32_t nic_count = 0;
    const uint8_t nic_screen_count = 3;
    uint32_t nic_index = 0;

    char chosen_nic[75] = {0};

    Sprite* nic_logo = newSprite(IMAGE, "assets/img/nic_logo.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);

    //we need a series of buttons for each string in nic_list. go with 3, and then add an forward if needed.

    Sprite* nic_1 = newSprite(TEXT, "Placeholder", option_x, option_y, TEXT_SIZE(option_size), option_not_chosen);
    Sprite* nic_2 = newSprite(TEXT, "Placeholder", option_x, option_y + 50, TEXT_SIZE(option_size), option_not_chosen);
    Sprite* nic_3 = newSprite(TEXT, "Placeholder", option_x, option_y + 100, TEXT_SIZE(option_size), option_not_chosen);

    Sprite* nic_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    
    Sprite* nic_forward = newSprite(IMAGE, "assets/img/forward.png", forward_x, forward_y, arrow_w, arrow_h, WHITE);
    Sprite* nic_backward = newSprite(IMAGE, "assets/img/backward.png", backward_x, backward_y, arrow_w, arrow_h, WHITE);
    
    Sprite* nic_reload = newSprite(IMAGE, "assets/img/reload.png", 400, 75, 75, 75, WHITE);
    
    addScene(nic_menu, nic_logo);
    addScene(nic_menu, nic_1);
    addScene(nic_menu, nic_2);
    addScene(nic_menu, nic_3);
    addScene(nic_menu, nic_return);
    addScene(nic_menu, nic_forward);
    addScene(nic_menu, nic_backward);
    addScene(nic_menu, nic_reload);

    Scene* dns_menu = newScene();

    //lets use a set amount of DNS

    char* dns_list[] = {
        "178.62.43.212 - PokeClassicNetwork",
        //"100.100.100.100",
        //"45.6.3.1 - my network",
    };
    uint32_t dns_count = sizeof(dns_list) / sizeof(dns_list[0]);
    uint32_t dns_index = 0;

    printf("dns count: %u\n", dns_count);
    char chosen_dns[20] = {0};

    Sprite* dns_logo = newSprite(IMAGE, "assets/img/dns_logo.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);

    //same deal with nic, have 3 viewable at a time
    Sprite* dns_1 = newSprite(TEXT, "Placeholder", option_x, option_y, TEXT_SIZE(option_size), option_not_chosen);
    Sprite* dns_2 = newSprite(TEXT, "Placeholder", option_x, option_y + 50, TEXT_SIZE(option_size), option_not_chosen);
    Sprite* dns_3 = newSprite(TEXT, "Placeholder", option_x, option_y + 100, TEXT_SIZE(option_size), option_not_chosen);
    
    Sprite* dns_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    
    Sprite* dns_forward = newSprite(IMAGE, "assets/img/forward.png", forward_x, forward_y, arrow_w, arrow_h, WHITE);
    Sprite* dns_backward = newSprite(IMAGE, "assets/img/backward.png", backward_x, backward_y, arrow_w, arrow_h, WHITE);
    
    addScene(dns_menu, dns_logo);
    addScene(dns_menu, dns_1);
    addScene(dns_menu, dns_2);
    addScene(dns_menu, dns_3);
    addScene(dns_menu, dns_return);
    addScene(dns_menu, dns_forward);
    addScene(dns_menu, dns_backward);

    //loading screen (before receiving confirmation from child process)

    Scene* loading_menu = newScene();

    Sprite* loading_display = newSprite(IMAGE, "assets/img/loading.png", 100, 100, IMAGE_SIZE_NATIVE, WHITE);

    addScene(loading_menu, loading_display);
    
    //the actual scene when we can finally link up the ds
    Scene* connection_menu = newScene();
    
    Sprite* connection_logo = newSprite(IMAGE, "assets/img/success.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);

    //add a back button
    Sprite* connection_stop = newSprite(IMAGE, "assets/img/connection_stop.png", 125, 275, IMAGE_SIZE_NATIVE, WHITE);

    Sprite* connection_info = newSprite(TEXT, " ", 50, 200, TEXT_SIZE(20), BLUE);
    
    //add a timer
    const long connection_time_start = 3 * 60 * 60; //3 hours
    long connection_time_left = connection_time_start;
    clock_t connection_time_old = 0, connection_time_new = 0;
    Sprite* connection_time = newSprite(TEXT, " ", 50, 300, TEXT_SIZE(20), RED);

    addScene(connection_menu, connection_logo);
    addScene(connection_menu, connection_stop);
    addScene(connection_menu, connection_info);
    addScene(connection_menu, connection_time);    

    cur_scene = main_menu;
    
    while (!WindowShouldClose()) {
        if (cur_scene == main_menu) {
            imageHoveringChange(info, "assets/img/info_pressed.png", "assets/img/info.png");
            imageHoveringChange(main_start, "assets/img/start_pressed.png", "assets/img/start.png");
            
            if (imageHovering(info) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                textUpdate(main_error, " ");
                cur_scene = info_menu;
            }
            else if (imageHovering(main_start) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   
                textUpdate(main_error, " ");
                
                nic_count = 0;
                for (int i = 0; i < nic_count; i++) {
                    memset(nic_list[i], 0, strlen(nic_list[i]));
                }
                
                struct dirent* dentry;
                DIR* directory = opendir("/sys/class/net");
                if (!directory) {
                    printf("could not open /sys/class/net to verify\n");
                    cur_scene = main_menu;
                    continue;
                }
                while ((dentry = readdir(directory)) != NULL) {
                    if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0) continue;
                    strcpy(nic_list[nic_count++], dentry->d_name);
                }
                closedir(directory);
                
                printf("found all nics:\n");
                for (uint32_t i = 0; i < nic_count; i++) {
                    printf("%s\n", nic_list[i]);
                }
                
                if (nic_count > 0) {
                    //get the first 3
                    uint8_t i_1 = 0 % nic_count;
                    uint8_t i_2 = 1 % nic_count;
                    uint8_t i_3 = 2 % nic_count;

                    textUpdate(nic_1, nic_list[i_1]);
                    textUpdate(nic_2, nic_list[i_2]);
                    textUpdate(nic_3, nic_list[i_3]);
                }
                else {
                    cur_scene = main_menu;
                    continue;
                }


                cur_scene = nic_menu; //nic_menu
            }
        }
        else if (cur_scene == nic_menu) {
            
            memset(chosen_nic, 0, strlen(chosen_nic));
            nic_index = 0;

            textHoveringChange(nic_1, NULL, option_chosen, NULL, option_not_chosen);
            textHoveringChange(nic_2, NULL, option_chosen, NULL, option_not_chosen);
            textHoveringChange(nic_3, NULL, option_chosen, NULL, option_not_chosen);
            imageHoveringChange(nic_return, "assets/img/return_pressed.png", "assets/img/return.png");
            imageHoveringChange(nic_forward, "assets/img/forward_pressed.png", "assets/img/forward.png");
            imageHoveringChange(nic_backward, "assets/img/backward_pressed.png", "assets/img/backward.png");
            imageHoveringChange(nic_reload, "assets/img/reload_pressed.png", "assets/img/reload.png");
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                char* choice = NULL;
                if (textHovering(nic_1)) {
                    char** data = (char**)nic_1->data;
                    choice = *data;
                }
                else if (textHovering(nic_2)) {
                    char** data = (char**)nic_2->data;
                    choice = *data;
                }
                else if (textHovering(nic_3)) {
                    char** data = (char**)nic_3->data;
                    choice = *data;
                }
                else if (imageHovering(nic_return)) {
                    printf("nic returning to main\n");
                    cur_scene = main_menu;
                }
                else if (imageHovering(nic_forward)) {
                    nic_index = (nic_index + 3) % nic_count;
                    textUpdate(nic_1, nic_list[(nic_index + 0) % nic_count]);
                    textUpdate(nic_2, nic_list[(nic_index + 1) % nic_count]);
                    textUpdate(nic_3, nic_list[(nic_index + 2) % nic_count]);
                }
                else if (imageHovering(nic_backward)) {
                    nic_index = (nic_index - 3) % nic_count;
                    textUpdate(nic_1, nic_list[(nic_index + 0) % nic_count]);
                    textUpdate(nic_2, nic_list[(nic_index + 1) % nic_count]);
                    textUpdate(nic_3, nic_list[(nic_index + 2) % nic_count]);
                }
                else if (imageHovering(nic_reload)) {
                    nic_index = 0;
                    nic_count = 0;
                    for (int i = 0; i < nic_count; i++) {
                        memset(nic_list[i], 0, strlen(nic_list[i]));
                    }
                    
                    struct dirent* dentry;
                    DIR* directory = opendir("/sys/class/net");
                    if (!directory) {
                        printf("could not open /sys/class/net to verify\n");
                        cur_scene = main_menu;
                        continue;
                    }
                    while ((dentry = readdir(directory)) != NULL) {
                        if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0) continue;
                        strcpy(nic_list[nic_count++], dentry->d_name);
                    }
                    closedir(directory);
                    
                    printf("found all nics:\n");
                    for (uint32_t i = 0; i < nic_count; i++) {
                        printf("%s\n", nic_list[i]);
                    }
                    
                    if (nic_count > 0) {
                        //get the first 3
                        uint8_t i_1 = 0 % nic_count;
                        uint8_t i_2 = 1 % nic_count;
                        uint8_t i_3 = 2 % nic_count;

                        textUpdate(nic_1, nic_list[i_1]);
                        textUpdate(nic_2, nic_list[i_2]);
                        textUpdate(nic_3, nic_list[i_3]);
                    }
                }

                if (choice != NULL) {
                    strcpy(chosen_nic, choice);
                    printf("chosen nic: %s\n", chosen_nic);
                    
                    //we now have to load up our DNS stuff

                    if (dns_count > 0) {
                        printf("all dns: %s\n", dns_list[0]);
                        textUpdate(dns_1, dns_list[0 % dns_count]);
                        textUpdate(dns_2, dns_list[1 % dns_count]);
                        textUpdate(dns_3, dns_list[2 % dns_count]);
                    }

                    cur_scene = dns_menu;
                }
            }
        }
        else if (cur_scene == dns_menu) {
            if (chosen_dns) memset(chosen_dns, 0, strlen(chosen_dns));
            dns_index = 0;
            
            textHoveringChange(dns_1, NULL, option_chosen, NULL, option_not_chosen);
            textHoveringChange(dns_2, NULL, option_chosen, NULL, option_not_chosen);
            textHoveringChange(dns_3, NULL, option_chosen, NULL, option_not_chosen);
            
            imageHoveringChange(dns_return, "assets/img/return_pressed.png", "assets/img/return.png");
            imageHoveringChange(dns_forward, "assets/img/forward_pressed.png", "assets/img/forward.png");
            imageHoveringChange(dns_backward, "assets/img/backward_pressed.png", "assets/img/backward.png");
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                char* choice = NULL;
                if (textHovering(dns_1)) {
                    char** data = (char**)dns_1->data;
                    choice = *data;
                }
                else if (textHovering(dns_2)) {
                    char** data = (char**)dns_2->data;
                    choice = *data;
                }
                else if (textHovering(dns_3)) {
                    char** data = (char**)dns_3->data;
                    choice = *data;
                }
                else if (imageHovering(dns_return)) {
                    printf("dns returning to nic\n");
                    cur_scene = nic_menu;
                }
                else if (imageHovering(dns_forward)) {
                    dns_index = (dns_index + 3) % dns_count;
                    textUpdate(dns_1, dns_list[(dns_index + 0) % dns_count]);
                    textUpdate(dns_2, dns_list[(dns_index + 1) % dns_count]);
                    textUpdate(dns_3, dns_list[(dns_index + 2) % dns_count]);
                }
                else if (imageHovering(dns_backward)) {
                    dns_index = (dns_index - 3) % dns_count;
                    textUpdate(dns_1, dns_list[(dns_index + 0) % dns_count]);
                    textUpdate(dns_2, dns_list[(dns_index + 1) % dns_count]);
                    textUpdate(dns_3, dns_list[(dns_index + 2) % dns_count]);
                }

                if (choice != NULL) {
                    //choice contains our data. we need to remove anything after and including a dash
                    //(the description of the dns, if it exists)
                    size_t dash_index = strlen(choice);
                    char* dash = strstr(choice, "-");
                    if (dash != NULL) {
                        dash_index -= strlen(dash);
                        //we should also check for any whitespace from dash_index backwards;
                        dash_index--;
                        while (choice[dash_index] == ' ') dash_index--;
                        dash_index++;
                    }

                    strncpy(chosen_dns, choice, dash_index);
                    printf("chosen dns: %s\n", chosen_dns);


                    //we now have a chosen dns and nic.
                    //fork and set up a child process.
                    //set up the pipes

                    pipe(pipefd);
                    pipe(signalfd);

                    back_p = fork();
                    if (back_p == 0) { //child - backend
                        
                        //normal communication
                        close(pipefd[0]); //child does not read
                        dup2(pipefd[1], STDOUT_FILENO); //redirect output to stdout
                        close(pipefd[1]); //no longer using this output
                        
                        //signal communication
                        close(signalfd[1]); //child does not output signal
                        dup2(signalfd[0], STDIN_FILENO); //redirect input to stdin
                        close(signalfd[0]); //no longer using this input

                        char* backend_args[] = {
                            "pkexec", //run as root
                            "bin/ivnetback",
                            chosen_nic,
                            chosen_dns,
                            "GB", //hardcoded for now
                            SSID,
                            NULL,
                        };
                        execvp("pkexec", backend_args);
                        perror("Could not start backend\n");
                        //send an error message via printf
                        printf("0:Could not start backend\n");
                        fflush(stdout);
                        exit(1);
                    }
                    else { //parent - frontend
                        //parent doesnt write
                        close(pipefd[1]);
                        //parent does not read signal
                        close(signalfd[0]);
                        //set read to be non-blocking
                        int pipe_flags = fcntl(pipefd[0], F_GETFL, 0);
                        fcntl(pipefd[0], F_SETFL, pipe_flags | O_NONBLOCK);
                    }
                    

                    cur_scene = loading_menu; 
                }
            }
            
        }
        else if (cur_scene == loading_menu) {
            //preferably, we want the pipe to be non-blocking, so that we can check and display at the same time.
            //do this once the dongle arrives
            char buffer[256] = {0};
            int bytes_read = read(pipefd[0], buffer, sizeof(buffer));
            //the format is "status:message"
            if (bytes_read > 0) {
                if (buffer[0] == '1') {
                    perror("Backend success!\n");
                    //updated connection info
                    char connection_info_string[512] = {0};
                    sprintf(connection_info_string, "You can now connect your DS!\nPrimary DNS:%s\nSSID:%s", chosen_dns, SSID);
                    textUpdate(connection_info, connection_info_string);
                    connection_time_old = clock();
                    cur_scene = connection_menu;
                }
                else if (buffer[0] == '0') {
                    perror("Backend returned an error.\n");
                    sprintf(error_message, "Error:%s", &buffer[2]);
                    textUpdate(main_error, error_message);
                    cur_scene = main_menu;
                    
                }
            }

        }
        else if (cur_scene == connection_menu) {
            //update the timer
            char connection_time_buffer[512] = {0};
            sprintf(connection_time_buffer, "Time left: %li", connection_time_left);
            textUpdate(connection_time, connection_time_buffer);

            connection_time_new = clock();
            double connection_time_taken = ((double)(connection_time_new - connection_time_old))/CLOCKS_PER_SEC;
            
            //printf("connection_time_new: %u\n", connection_time_new);
            //printf("connection_time_old: %u\n", connection_time_old);
            //printf("connection_time_taken: %lf\n", 10 * connection_time_taken);
            
            connection_time_left = connection_time_start - (long)(10* connection_time_taken);
            if (connection_time_left <= 0) {
                //stop everything
                sprintf(error_message, "Error:IVnet connection out of time");
                textUpdate(main_error, error_message);
                goto connection_close;
            }

            imageHoveringChange(connection_stop, "assets/img/connection_stop_pressed.png", "assets/img/connection_stop.png");
            if (imageHovering(connection_stop) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                connection_close:
                //send the signal to stop using pipe
                close(signalfd[1]);
                //kill(back_p, SIGTERM);
                printf("killed backend\n");
                //wait for backend to finish cleaning up
                waitpid(back_p, NULL, 0);
                printf("waited for backend to finish\n");
                //reset it
                back_p = -1;

                cur_scene = main_menu;
            }
            
            //read for any messages from the backend for early termination.
            char buffer[256] = {0};
            int bytes_read = read(pipefd[0], buffer, sizeof(buffer));
            //the format is "status:message"
            if (bytes_read > 0) {
                //at this point, if we get any kind of message, its a bad sign.
                if (buffer[0] == '0') {
                    printf("backend ended things on its own terms.\n");
                    sprintf(error_message, "Error:%s", &buffer[2]);
                    textUpdate(main_error, error_message);
                    goto connection_close;
                }
            }
        }
        else if (cur_scene == info_menu) {
            imageHoveringChange(info_return, "assets/img/return_pressed.png", "assets/img/return.png");
            if (imageHovering(info_return) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) cur_scene = main_menu;
            
        }


        BeginDrawing();
        ClearBackground(WHITE);
        displayScene(cur_scene);
        //DrawTexture(logo, logo_x, logo_y, WHITE);
        //DrawText(text, text_x, text_y, text_size, text_colour);
        EndDrawing();
    }
    CloseWindow();
    
    freeScene(main_menu);
    freeScene(info_menu);
    freeScene(nic_menu);
    freeScene(dns_menu);
    freeScene(connection_menu);
    cur_scene = NULL;
    //UnloadImage(info_unpressed);
    //UnloadImage(info_pressed);
    //UnloadImage(return_unpressed);
    //UnloadImage(return_pressed);

    //UnloadTexture(logo);
    //free(text);
    return 0;
}
