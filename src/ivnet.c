/*
IVnet frontend.
Linux program for connecting the generation IV Pokemon games to the internet.

Visit https://github.com/vixthevix/IVnet for more info.
*/

#include <raylib.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

//Default font data to be used for all text.
Font font; 
const float font_spacing = 2.0f;

/*
Reads a file and returns its contents as a string.
@arg path -> path to the file.
@return -> string containing contents.
*/
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

/*
Enum for differentiating Sprite data contents.
*/
typedef enum SpriteType {
    IMAGE,
    TEXT,
} SpriteType;

/*
Holds data about something viewable in the opened window.
*/
typedef struct Sprite {
    void* data;
    SpriteType type;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    Color colour;
} Sprite;

/*
Returns appropriate size of Sprite data contents.
@arg type -> type of data Sprite is holding.
@return size of the data.
*/
size_t getSpriteSize(SpriteType type) {
    switch (type) {
        case IMAGE: return sizeof(Texture2D);
        case TEXT: return sizeof(char*);
    }
    return 0;
}
/*
Frees memory held by Sprite.
@arg sprite -> target to free.
@return free status.
*/
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

/*
Status values to allow an image to be loaded with its native
height and width, in pixels.
*/
#define IMAGE_WIDTH_NATIVE 0
#define IMAGE_HEIGHT_NATIVE 0
#define IMAGE_SIZE_NATIVE IMAGE_WIDTH_NATIVE, IMAGE_HEIGHT_NATIVE

/*
Macro that takes advantage of normal Sprite proportions to translate
into a font size. To be used with TEXT Sprites.
*/
#define TEXT_SIZE(size) size, size

/*
Creates a new Sprite.
@arg type -> type of data to be held.
@arg source -> where data is read from.
@arg x, y -> coordinates of Sprite.
@arg width, height -> proportions of Sprite.
@arg colour -> colour of Sprite.
@return generated Sprite or NULL.
*/
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
            //with IMAGE, treat source as a file path.
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
            //With TEXT, treat source as a raw string.
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

/*
Unused function for creating a memory-unique copy of a Sprite.
@arg sprite -> copy target.
@return new copy of sprite.
*/
/*
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
*/

/*
Displays a Sprite in the window.
@arg sprite -> target to draw.
@return status of display.
*/
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
            //use the set font and spacing to display text.
            DrawTextEx(font, *data, (Vector2){sprite->x, sprite->y}, sprite->width, font_spacing, sprite->colour);
            break;
        }
        default: return false;
    }
    return true;
}


/*
Checks if the mouse is hovering over an IMAGE Sprite.
@arg sprite -> target to check.
@return status of mouse hovering.
*/
bool imageHovering(Sprite* sprite) {
    if (!sprite || !sprite->data || sprite->type != IMAGE) return false;

    //we need to get the bounding x and y
    uint32_t
    x_low = sprite->x,
    y_low = sprite->y,
    x_high = sprite->x + sprite->width,
    y_high = sprite->y + sprite->height;

    Vector2 mouse = GetMousePosition();
    return ((x_low < mouse.x && mouse.x < x_high) && (y_low < mouse.y && mouse.y < y_high));
}

/*
Changes IMAGE sprite data depending on if mouse is hovering over it.
@arg sprite -> target to change.
@arg hovering_path -> path of image to change to if hovering
@arg not_hovering_path -> path of image to change to if not hovering
*/
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

/*
Updates data contents of TEXT Sprite.
@arg sprite -> target to update.
@arg new_text -> replaces target data.
*/
void textUpdate(Sprite* sprite, const char* new_text) {
    if (!sprite || !sprite->data || sprite->type != TEXT) return;

    char** data = (char**)sprite->data;
    if (*data) free(*data);
    else return;

    *data = (char*) calloc(strlen(new_text) + 1, sizeof(char));
    strcpy(*data, new_text);
}

/*
Checks if the mouse is hovering over a TEXT Sprite.
@arg sprite -> target to check.
@return status of mouse hovering.
*/
bool textHovering(Sprite* sprite) {
    if (!sprite || !sprite->data || sprite->type != TEXT) return false;
    
    char** data = (char**)sprite->data;
    if (!(*data)) return false;
    
    uint32_t font_size = sprite->width;
    
    //use Raylib function to get bounds
    Vector2 text_size = MeasureTextEx(font, *data, font_size, font_spacing);

    const uint32_t
    x_low = sprite->x,
    y_low = sprite->y,
    x_high = sprite->x + text_size.x,
    y_high = sprite->y + text_size.y;

    Vector2 mouse = GetMousePosition();
    return ((x_low < mouse.x && mouse.x < x_high) && (y_low < mouse.y && mouse.y < y_high));
}

/*
Updates TEXT sprite data and colour if mouse is hovering or not.
@arg sprite -> target to update.
@arg hovering_text -> data to set if mouse is hovering.
@arg hovering_colour -> Color to set if mouse is hovering.
@arg not_hovering_text -> data to set if mouse is not hovering.
@arg not_hovering_colour -> Color to set if mouse is not hovering.
*/
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

/*
Struct that holds a dynamic list of Sprite for a particular scenario.
*/
typedef struct Scene {
    Sprite** sprites;
    uint32_t len;
    uint32_t capacity;
} Scene;

/*
Intialises a base Scene.
@return empty Scene.
*/
Scene* newScene() {
    const uint32_t initCap = 8;
    Scene* target = (Scene*) malloc(sizeof(Scene));
    if (!target) return NULL;
    
    target->sprites = (Sprite**) calloc(initCap, sizeof(Sprite*));
    target->len = 0;
    target->capacity = initCap;

    return target;
}

/*
Adds a Sprite to a Scene.
@arg scene -> target Scene.
@arg sprite -> Sprite to add to scene.
@return success status.
*/
bool addScene(Scene* scene, Sprite* sprite) {
    if (!scene || !scene->sprites || !sprite) return false;
    const uint32_t load = scene->len / scene->capacity * 100;
    if (load > 60) { //is the list 60% full?
        scene->capacity <<= 1; //double
        scene->sprites = (Sprite**) realloc(scene->sprites, sizeof(Sprite*) * scene->capacity);
    }
    scene->sprites[scene->len++] = sprite;
    return true;
}

/*
Displays all of the Sprites in a Scene.
@arg scene -> target Scene to read Sprites from.
*/
void displayScene(Scene* scene) {
    if (!scene || !scene->sprites) return;

    for (uint32_t i = 0; i < scene->len; i++) {
        bool status = displaySprite(scene->sprites[i]);
        if (!status) printf("Could not display sprite #%u of current scene\n", i);
    }
}

/*
Frees a Scene and its contents from memory.
@arg scene -> Scene to free.
@return status of free.
*/
bool freeScene(Scene* scene) {
    if (!scene) return false;
    if (!scene->sprites) {
        free(scene);
        return true;
    }

    for (uint32_t i = 0; i < scene->len; i++) {
        bool status = freeSprite(scene->sprites[i]);
        //if (!status) printf("Could not free sprite #%u of current scene\n", i);
    }

    free(scene->sprites);
    free(scene);
    return true;
}

/*
Reads available Network Interface Devices.
@arg nic_list -> array of strings to hold NID names.
@arg nic_count -> pointer to hold NID count.
*/
bool updateNIC(char nic_list[256][75], uint32_t* nic_count) {
    //reset
    uint32_t count = 0;
    for (int i = 0; i < *nic_count; i++) {
        memset(nic_list[i], 0, strlen(nic_list[i]));
    }

    struct dirent* dentry;
    DIR* directory = opendir("/sys/class/net");
    if (!directory) return false;
    while ((dentry = readdir(directory)) != NULL) {
        //ignore '.' and '..' directories
        if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0) continue;
        strcpy(nic_list[count++], dentry->d_name);
    }
    closedir(directory);

    *nic_count = count;

    return true;
}

/*
Struct to hold user saved information for all sessions.
In the format:
    country_code -> stores IEEE 802.11d code 
    END
*/
typedef struct Config {
    char country_code[3];
} Config;

/*
Generates a configuration file, and saves its contents to Config.
@arg path -> name of configuration file.
@return Config holding path data.
*/
Config generateConfig(const char* path) {
    Config config = {0};

    if (!path) return config;
    FILE* file = fopen(path, "r");
    if (!file) {
        //file doesn't exist, so create it.
        file = fopen(path, "w");
        if (!file) return config;
        fclose(file);
        //if created, go back to reading it.
        file = fopen(path, "r");
        if (!file) return config;
    }

    //read line by line.
    int line = 0;
    char buffer[100] = {0};
    while ((fgets(buffer, sizeof(buffer), file)) != NULL) {
        if (buffer[strlen(buffer)] == '\n') buffer[strlen(buffer)] = 0; //remove newline
        if (line == 0) { // Country Code
            strcpy(config.country_code, buffer);
        }
        memset(buffer, 0, 100);
        line++;
    }
    fclose(file);
    return config;
}

/*
Writes Config to config file.
@arg config -> current Config data.
@arg path -> config file to write to.
@return status of write.
*/
bool saveConfig(Config config, const char* path) {
    if (!path) return false;
    FILE* file = fopen(path, "w");
    if (!file) return false;

    //build up a buffer and write it
    fprintf(file, "%s", config.country_code);
    fclose(file);

    return true;
}


int main() {
    //constants to use.
    const char* SSID = "IVnet";
    const char* config_path = "IVnet.conf";
    const char* font_path = "assets/font/EightBitDragon-anqx.ttf";

    //default pointer for loading in text from file.
    char* text = NULL;

    Config config = generateConfig(config_path);

    //IPC set up
    pid_t back_p = 0; //backend fd
    int pipefd[2] = {0}; //main backend->frontend communication
    int signalfd[2] = {0}; //signal-like frontend->backend pipe, to kill backend when needed.
    
    //disable RayLib messaging.
    SetTraceLogLevel(LOG_NONE);

    //Window setup
    const uint32_t 
    width = 500,
    height = 500;
    InitWindow(width, height, "IVnet");
    SetTargetFPS(60);
    
    //Font setup
    font = LoadFontEx(font_path, 100, 0, 250);
    if (font.texture.id == 0) {
        printf("Could not load custom font, resorting to default...\n");
    }

    //Current scene to display
    Scene* cur_scene = NULL;
    
    Scene* main_menu = newScene();
    
    Sprite* logo = newSprite(IMAGE, "assets/img/logo.png", 125, 0, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* info = newSprite(IMAGE, "assets/img/info.png", 425, 0, 75, 75, WHITE);

    Sprite* main_start = newSprite(IMAGE, "assets/img/start.png", 150, 225, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* main_help = newSprite(IMAGE, "assets/img/help.png", 150, 325, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* main_config = newSprite(IMAGE, "assets/img/config.png", 150, 425, IMAGE_SIZE_NATIVE, WHITE);
    
    //Set up error displaying service in main menu.
    bool error_received = false;
    char error_message[512] = {0};
    Sprite* main_error = newSprite(TEXT, " ", 15, 200, TEXT_SIZE(12), RED); 

    addScene(main_menu, logo);
    addScene(main_menu, info);
    addScene(main_menu, main_start);
    addScene(main_menu, main_help);
    addScene(main_menu, main_config);
    addScene(main_menu, main_error);
    
    
    Scene* info_menu = newScene();

    text = extractText("assets/text/info.txt");
    Sprite* info_text = newSprite(TEXT, text, 15, 150, TEXT_SIZE(15), BLACK);
    free(text);

    Sprite* info_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    
    Color teto_colour = {255, 255, 255, 100}; //slightly transparent
    Sprite* teto = newSprite(IMAGE, "assets/img/teto.png", 250, 0, 256, 256, teto_colour);
    
    addScene(info_menu, teto);
    addScene(info_menu, info_text);
    addScene(info_menu, info_return);
    
    
    Scene* instruction_menu = newScene();

    //array of instruction contents.
    char* instructions[] = {
        extractText("assets/text/instructions_0.txt"),
        extractText("assets/text/instructions_1.txt"),
        extractText("assets/text/instructions_2.txt"),
    };
    const uint16_t instruction_max = sizeof(instructions)/sizeof(instructions[0]);
    uint16_t instruction_index = 0;
    
    Sprite* instruction_text = newSprite(TEXT, " ", 10, 50, TEXT_SIZE(15), BLACK);

    Sprite* instruction_forward = newSprite(IMAGE, "assets/img/forward.png", 400, 400, 100, 100, WHITE);
    Sprite* instruction_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    
    addScene(instruction_menu, instruction_text);
    addScene(instruction_menu, instruction_forward);
    addScene(instruction_menu, instruction_return);

    Scene* config_menu = newScene();
    
    Sprite* config_logo = newSprite(IMAGE, "assets/img/config_logo.png", 125, 0, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* config_return = newSprite(IMAGE, "assets/img/return.png", 1, 1, 50, 50, WHITE);
    Sprite* config_info = newSprite(TEXT, " ", 250, 200, TEXT_SIZE(20), BLACK); 

    //contains buttons for selecting which config to choose.
    Sprite* config_country_select = newSprite(IMAGE, "assets/img/config_country_select.png", 75, 150, 150, 150, WHITE);

    addScene(config_menu, config_logo);
    addScene(config_menu, config_return);
    addScene(config_menu, config_info);
    addScene(config_menu, config_country_select);

    Scene* country_select_menu = newScene();
    
    Sprite* country_select_instructions = newSprite(TEXT, "Type out your country code\n(All caps, only 2 letters)", 75, 100, TEXT_SIZE(20), BLACK);
    Sprite* country_select_code = newSprite(TEXT, "", 200, 200, TEXT_SIZE(40), BLACK);

    //buffer to store contents of configured country code.
    char letter_buffer[10] = {0};
    int letter_count = 0;

    addScene(country_select_menu, country_select_instructions);
    addScene(country_select_menu, country_select_code);
    
    
    //Default constant data for NID and DNS selection screens.
    //This is because they are practically identical.
    const uint32_t option_x = 50, option_y = 200, option_size = 15;
    const Color option_chosen = BLUE, option_not_chosen = BLACK;
    const uint32_t forward_x = 350, backward_x = 50, forward_y = 350, backward_y = forward_y, arrow_w = 100, arrow_h = arrow_w;

    Scene* nic_menu = newScene();

    char nic_list[256][75] = {0};
    uint32_t nic_count = 0;
    const uint8_t nic_screen_count = 3; //how many NIDs to display on screen at once.
    uint32_t nic_index = 0;

    char chosen_nic[75] = {0};

    Sprite* nic_logo = newSprite(IMAGE, "assets/img/nic_logo.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);

    //text containing NID names.
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

    char* dns_list[] = {
        "178.62.43.212 - PokeClassicNetwork",
        "167.235.229.36 - PCN Backup",
    };
    uint32_t dns_count = sizeof(dns_list) / sizeof(dns_list[0]);
    uint32_t dns_index = 0;

    char chosen_dns[75] = {0};

    Sprite* dns_logo = newSprite(IMAGE, "assets/img/dns_logo.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);

    //same deal with NID, have 3 viewable at a time.
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

    Sprite* loading_display = newSprite(IMAGE, "assets/img/loading.png", 125, 100, IMAGE_SIZE_NATIVE, WHITE);

    addScene(loading_menu, loading_display);

    const long loading_time_max = 30; //seconds
    double loading_time_old = 0, loading_time_new = 0;
    
    //the actual scene when we can finally link up the ds
    Scene* connection_menu = newScene();
    
    Sprite* connection_logo = newSprite(IMAGE, "assets/img/success.png", 125, -25, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* connection_stop = newSprite(IMAGE, "assets/img/connection_stop.png", 125, 275, IMAGE_SIZE_NATIVE, WHITE);
    Sprite* connection_info = newSprite(TEXT, " ", 50, 200, TEXT_SIZE(20), BLUE);
    
    //add a timer
    const long connection_time_start = 3 * 60 * 60; //3 hours
    long connection_time_left = connection_time_start;
    double connection_time_old = 0, connection_time_new = 0;
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
            imageHoveringChange(main_help, "assets/img/help_pressed.png", "assets/img/help.png");
            imageHoveringChange(main_config, "assets/img/config_pressed.png", "assets/img/config.png");
            
            if (imageHovering(info) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                textUpdate(main_error, " ");
                cur_scene = info_menu;
            }
            else if (imageHovering(main_help) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                textUpdate(main_error, " ");
                instruction_index = 0;
                textUpdate(instruction_text, instructions[instruction_index]);
                cur_scene = instruction_menu;
            }
            else if (imageHovering(main_config) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                textUpdate(main_error, " ");
                //update the config info
                char config_info_buffer[100] = {0};
                sprintf(config_info_buffer, "CURRENT CONFIG:\n\nCOUNTRY CODE: %s", config.country_code);
                textUpdate(config_info, config_info_buffer);
                cur_scene = config_menu;
            }
            else if (imageHovering(main_start) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   
                if (strlen(config.country_code) < 2) {
                    textUpdate(main_error, "Error:Country code not configured properly");
                    goto screen_display;
                }
                textUpdate(main_error, " ");
                
                if (updateNIC(nic_list, &nic_count) == false) {
                    textUpdate(main_error, "Error: could not load network device list");
                    cur_scene = main_menu;
                    goto screen_display;
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
                    textUpdate(main_error, "Error: no network devices found");
                    cur_scene = main_menu;
                    goto screen_display;
                }
                memset(chosen_nic, 0, strlen(chosen_nic));
                nic_index = 0;
                cur_scene = nic_menu; //nic_menu
            }
        }
        else if (cur_scene == nic_menu) {
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
                    if (updateNIC(nic_list, &nic_count) == false) {
                        textUpdate(main_error, "Error: could not load network device list");
                        cur_scene = main_menu;
                        goto screen_display;
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
                        textUpdate(main_error, "Error: no network devices found");
                        cur_scene = main_menu;
                        goto screen_display;
                    }
                }

                if (choice != NULL) {
                    strcpy(chosen_nic, choice);
                    //we now have to load up our DNS stuff
                    if (dns_count > 0) {
                        textUpdate(dns_1, dns_list[0 % dns_count]);
                        textUpdate(dns_2, dns_list[1 % dns_count]);
                        textUpdate(dns_3, dns_list[2 % dns_count]);
                    }
                    memset(chosen_dns, 0, strlen(chosen_dns));
                    dns_index = 0;
                    cur_scene = dns_menu;
                }
            }
        }
        else if (cur_scene == dns_menu) {
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
                    
                    //with a chosen NID and DNS, we can start the backend

                    pipe(pipefd);
                    pipe(signalfd);

                    back_p = fork();
                    if (back_p == 0) { //child - backend
                        
                        //normal communication
                        close(pipefd[0]); //child does not input
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
                            config.country_code, //hardcoded for now
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
                        close(pipefd[1]); //parent doesnt output status
                        close(signalfd[0]); //parent does not input signal
                        //set read to be non-blocking
                        int pipe_flags = fcntl(pipefd[0], F_GETFL, 0);
                        fcntl(pipefd[0], F_SETFL, pipe_flags | O_NONBLOCK);
                        
                        //set up the loading time
                        loading_time_old = GetTime();
                    }
                    cur_scene = loading_menu; 
                }
            }
            
        }
        else if (cur_scene == loading_menu) {
            
            //has loading taken too long?
            loading_time_new = GetTime();
            double loading_time_passed = ((double)(loading_time_new - loading_time_old));
            if (loading_time_passed > loading_time_max) {
                perror("Backend returned an error.\n");
                
                sprintf(error_message, "Error:backend took too long");
                textUpdate(main_error, error_message);
                
                cur_scene = main_menu;
                
                kill(back_p, SIGKILL); //just kill the backend
                waitpid(back_p, NULL, 0);
                
                goto screen_display;
            }

            //also, check the status of the child process
            if (kill(back_p, 0) != 0) {
                if (errno == ESRCH) {
                    perror("Backend returned an error.\n");
                    
                    sprintf(error_message, "Error:backend terminated unexpectedly");
                    textUpdate(main_error, error_message);
                    
                    cur_scene = main_menu;
                    
                    goto screen_display;
                }
            }

            char buffer[256] = {0};
            int bytes_read = read(pipefd[0], buffer, sizeof(buffer));
            //the format is "status:message"
            if (bytes_read > 0) {
                if ((strncmp(buffer, "IVnet:1", strlen("IVnet:1")) == 0)) {
                    perror("Backend success!\n");
                    //updated connection info
                    char connection_info_string[512] = {0};
                    sprintf(connection_info_string, "You can now connect your DS!\nPrimary DNS:%s\nSSID:%s", chosen_dns, SSID);
                    textUpdate(connection_info, connection_info_string);
                    
                    //set up the connection time limit
                    connection_time_old = GetTime();
                    
                    cur_scene = connection_menu;
                }
                else if ((strncmp(buffer, "IVnet:0", strlen("IVnet:0")) == 0)) {
                    perror("Backend returned an error.\n");
                    sprintf(error_message, "Error:%s", &buffer[strlen("IVnet:0:")]);
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

            connection_time_new = GetTime();
            double connection_time_taken = ((double)(connection_time_new - connection_time_old));
                        
            connection_time_left = connection_time_start - (long)(connection_time_taken);
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
                //wait for backend to finish cleaning up
                waitpid(back_p, NULL, 0);
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
                if (strncmp(buffer, "IVnet:0", strlen("IVnet:0")) == 0) {
                    printf("backend ended things on its own terms.\n");
                    sprintf(error_message, "Error:%s", &buffer[strlen("IVnet:0:")]);
                    textUpdate(main_error, error_message);
                    goto connection_close;
                }
            }
        }
        else if (cur_scene == info_menu) {
            imageHoveringChange(info_return, "assets/img/return_pressed.png", "assets/img/return.png");
            if (imageHovering(info_return) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) cur_scene = main_menu;
        }
        else if (cur_scene == instruction_menu) {
            imageHoveringChange(instruction_return, "assets/img/return_pressed.png", "assets/img/return.png");
            imageHoveringChange(instruction_forward, "assets/img/forward_pressed.png", "assets/img/forward.png");
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (imageHovering(instruction_return)) cur_scene = main_menu;
                else if (imageHovering(instruction_forward)) {
                    //advance instructions
                    instruction_index = (instruction_index + 1) % instruction_max;
                    textUpdate(instruction_text, instructions[instruction_index]);
                }
            }            
        }
        else if (cur_scene == config_menu) {
            textUpdate(country_select_code, letter_buffer);

            imageHoveringChange(config_return, "assets/img/return_pressed.png", "assets/img/return.png");
            imageHoveringChange(config_country_select, "assets/img/config_country_select_pressed.png", "assets/img/config_country_select.png");
            if (imageHovering(config_return) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) cur_scene = main_menu;
            if (imageHovering(config_country_select) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                memset(letter_buffer, 0, sizeof(letter_buffer));
                letter_count = 0;
                textUpdate(country_select_code, letter_buffer);
                cur_scene = country_select_menu;
            } 
        }
        //config settings
        else if (cur_scene == country_select_menu) {
            //read keys, update the country code as reading, limit to two characters
            //ensure only letters are read, and auto convert to caps
            //ensure backspace works. ensure enter saves the config.

            if (strlen(letter_buffer) == 2) {
                country_select_code->colour = BLUE;
            }
            else country_select_code->colour = BLACK;
            
            int key = GetKeyPressed();
            if ('A' <= key && key <= 'Z' && letter_count < 2) { //uppercase
                letter_buffer[letter_count++] = key;
                textUpdate(country_select_code, letter_buffer);
            }
            else if ('a' <= key && key <= 'z' && letter_count < 2) { //lowercase
                key -= 32; //convert to uppercase
                letter_buffer[letter_count++] = key;
                textUpdate(country_select_code, letter_buffer);
            }
            
            if (IsKeyPressed(KEY_BACKSPACE)) { //backspace
                if (letter_count > 0) letter_count--;
                letter_buffer[letter_count] = 0;
                textUpdate(country_select_code, letter_buffer);
            }
            else if (IsKeyPressed(KEY_ENTER)) { //carriage return
                if (letter_count == 2) { //only if we have a complete country code
                    strcpy(config.country_code, letter_buffer);
                    saveConfig(config, config_path);
                    
                    char config_info_buffer[100] = {0};
                    sprintf(config_info_buffer, "CURRENT CONFIG:\n\nCOUNTRY CODE: %s", config.country_code);
                    textUpdate(config_info, config_info_buffer);
                    
                    cur_scene = config_menu;
                }
            }
        }

        screen_display:
        BeginDrawing();
        ClearBackground(WHITE);
        displayScene(cur_scene);
        EndDrawing();
    }
    UnloadFont(font);
    
    //cleanup
    freeScene(main_menu);
    freeScene(info_menu);
    freeScene(instruction_menu);
    freeScene(config_menu);
    freeScene(country_select_menu);
    freeScene(nic_menu);
    freeScene(dns_menu);
    freeScene(loading_menu);
    freeScene(connection_menu);
    
    cur_scene = NULL;
    CloseWindow();

    return 0;
}
