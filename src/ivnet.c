

#include "raylib/src/raylib.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

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
    if (!sprite || !sprite->data || sprite->type != IMAGE) return false;

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

    addScene(main_menu, logo);
    addScene(main_menu, info);
    addScene(main_menu, main_start);
    
    
    Scene* info_menu = newScene();

    char* text = extractText("assets/text/info.txt");
    printf("TEXT IS %s\n", text);
    Sprite* info_text = newSprite(TEXT, text, 15, 150, TEXT_SIZE(20), BLACK);
    free(text);

    Sprite* info_return = newSprite(IMAGE, "assets/img/return.png", 0, 0, 50, 50, WHITE);
    //Image return_unpressed = LoadImage("assets/img/return.png");
    //ImageResize(&return_unpressed, info_return->width, info_return->height);
    //Image return_pressed = LoadImage("assets/img/return_pressed.png");
    //ImageResize(&return_pressed, info_return->width, info_return->height);

    
    addScene(info_menu, info_text);
    addScene(info_menu, info_return);

    
    //with the setup, we need to pick a NIC and a DNS, and then we can start connecting


    Scene* nic_menu = newScene();

    char nic_list[256][75] = {0};
    uint32_t nic_count = 0;
    const uint8_t nic_screen_count = 3;
    //we need a series of buttons for each string in nic_list. go with 3, and then add an arrow if needed.

    Sprite* nic_1 = newSprite(TEXT, "Placeholder", 250, 100, TEXT_SIZE(20), BLACK);
    Sprite* nic_2 = newSprite(TEXT, "Placeholder", 250, 150, TEXT_SIZE(20), BLACK);
    Sprite* nic_3 = newSprite(TEXT, "Placeholder", 250, 200, TEXT_SIZE(20), BLACK);
    
    addScene(nic_menu, nic_1);
    addScene(nic_menu, nic_2);
    addScene(nic_menu, nic_3);

    cur_scene = main_menu;
    
    while (!WindowShouldClose()) {
        if (cur_scene == main_menu) {
            imageHoveringChange(info, "assets/img/info_pressed.png", "assets/img/info.png");
            if (imageHovering(info) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) cur_scene = info_menu;
            
            imageHoveringChange(main_start, "assets/img/start_pressed.png", "assets/img/start.png");
            if (imageHovering(main_start) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                
                nic_count = 0;
                for (int i = 0; i < nic_count; i++) {
                    memset(nic_list[i], 0, strlen(nic_list[i]));
                }
                
                struct dirent* dentry;
                DIR* directory = opendir("/sys/class/net");
                if (!directory) {
                    printf("could not open /sys/class/net to verify\n");
                    cur_scene = info_menu;
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

                cur_scene = info_menu; //nic_menu
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
    cur_scene = NULL;
    //UnloadImage(info_unpressed);
    //UnloadImage(info_pressed);
    //UnloadImage(return_unpressed);
    //UnloadImage(return_pressed);

    //UnloadTexture(logo);
    //free(text);
    return 0;
}
