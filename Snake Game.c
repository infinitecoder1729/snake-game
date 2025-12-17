/**
 * ======================================================================================
 * PROJECT: SNAKE ENGINE
 * AUTHOR:  Nikhil Handa
 * SYSTEM:  Windows Console (Win32 API)
 * DATE:    2025
 * * DESCRIPTION:
 * A high-performance, double-buffered ASCII game engine specifically designed for Snake.
 * Features separate render/update threads simulation (via fixed time step), input
 * buffering, particle physics, and persistent save data.
 * * NEW FEATURES:
 * - [Dash]: Hold SHIFT to double speed and double score gain.
 * - [Combo]: Eat food quickly to build a Score Multiplier (up to 4x).
 * ======================================================================================
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <math.h>

// ======================================================================================
// CONFIGURATION & CONSTANTS
// ======================================================================================

#define APP_TITLE       "Snake Engine v1.0"
#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   30

// Game Balance
#define BASE_TICK_RATE  0.05    // 20 Ticks per second (Standard speed)
#define DASH_MULTIPLIER 2.0     // Speed multiplier when dashing
#define COMBO_WINDOW    60      // Ticks allowed between eats to keep combo
#define MAX_COMBO       4       // Maximum score multiplier

// Memory Limits
#define INPUT_QUEUE_SIZE 4
#define MAX_SNAKE_LEN    2048
#define MAX_PARTICLES    200
#define MAX_SCORES       5
#define SAVE_FILE        "snake_engine.dat"

// Colors (Foreground | Background)
#define COL_BLACK       0
#define COL_BLUE        1
#define COL_GREEN       2
#define COL_CYAN        3
#define COL_RED         4
#define COL_MAGENTA     5
#define COL_YELLOW      6
#define COL_WHITE       7
#define COL_GRAY        8

// ======================================================================================
// DATA STRUCTURES
// ======================================================================================

// 2D Integer Vector for grid coordinates
typedef struct {
    int x;
    int y;
} Vec2;

// Enumeration for Game Scenes
typedef enum {
    SCENE_MENU,         // Main Menu
    SCENE_GAME,         // Active Gameplay
    SCENE_GAMEOVER,     // Name Entry / Death Screen
    SCENE_SCORES        // Leaderboard
} SceneState;

// Enumeration for Level Generation Modes
typedef enum {
    MODE_CLASSIC,       // Open arena
    MODE_OBSTACLES      // Procedurally generated walls
} GameMode;

// Persistent High Score Entry
typedef struct {
    char name[16];
    int score;
    int max_combo;
} ScoreEntry;

// Visual Particle for explosions/effects
typedef struct {
    Vec2 pos;
    Vec2 vel;
    int life;           // Ticks remaining
    char icon;          // Character to render
    WORD color;         // Color attribute
} Particle;

// The Player Entity
typedef struct {
    Vec2 body[MAX_SNAKE_LEN];
    int length;
    Vec2 dir;           // Current movement vector
    int grow_pending;   // How many segments to add
    WORD color_theme;   // Current dynamic color
} Snake;

// Circular Buffer for Input (prevents missed keys on fast turns)
typedef struct {
    Vec2 queue[INPUT_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} InputBuffer;

// Main Game State Container
typedef struct {
    // Systems
    bool is_running;
    bool is_paused;
    bool debug_mode;    // Toggle with F3
    SceneState scene;
    InputBuffer input;
    
    // Gameplay
    GameMode mode;
    Snake snake;
    Vec2 food;
    
    // Stats
    int score;
    int combo_multiplier;
    int combo_timer;    // Decrements every tick
    bool is_dashing;    // True if Shift is held
    bool has_started;   // False until first input

    // World
    unsigned char map[SCREEN_WIDTH][SCREEN_HEIGHT]; // 1 = Wall, 0 = Empty
    Particle particles[MAX_PARTICLES];
    
    // UI/Meta
    char player_name[16];
    int name_cursor;
    
    // Timing
    double time_accumulator;
    double fps;
} GameState;

// Low-Level Renderer State
typedef struct {
    HANDLE hConsole;
    HANDLE hBuffer[2];      // Front and Back buffers
    int back_buffer_index;  // 0 or 1
    CHAR_INFO* pixel_data;  // Raw pixel array
} Renderer;

// ======================================================================================
// GLOBAL VARIABLES
// ======================================================================================

Renderer r;
GameState g;
ScoreEntry leaderboard[MAX_SCORES];
LARGE_INTEGER perf_freq;

// ======================================================================================
// FUNCTION PROTOTYPES
// ======================================================================================

// Core Engine
void Engine_Initialize();
void Engine_Shutdown();
void Engine_RunFrame(double dt);

// Rendering
void Render_Clear(WORD color);
void Render_Char(int x, int y, wchar_t ch, WORD attr);
void Render_String(int x, int y, const char* str, WORD attr);
void Render_Box(int x, int y, int w, int h, WORD attr);
void Render_Present(); // Swap buffers

// Game Logic
void Game_Reset();
void Game_UpdateFixed(); // Physics/Logic tick
void Game_Draw();        // Drawing routine
void Input_Process();
void Input_Enqueue(Vec2 dir);

// Systems
void Level_Generate(GameMode mode);
void Particles_Spawn(int x, int y, int count, WORD color);
void Particles_Update();
void Score_Load();
void Score_Save();
void Score_Add(const char* name, int score);

// ======================================================================================
// MAIN ENTRY POINT
// ======================================================================================

int main() {
    Engine_Initialize();
    Score_Load();

    // High Resolution Timer Setup
    QueryPerformanceFrequency(&perf_freq);
    LARGE_INTEGER last_tick;
    QueryPerformanceCounter(&last_tick);

    // Main Loop
    while (g.is_running) {
        // Calculate Delta Time
        LARGE_INTEGER current_tick;
        QueryPerformanceCounter(&current_tick);
        double frame_time = (double)(current_tick.QuadPart - last_tick.QuadPart) / perf_freq.QuadPart;
        last_tick = current_tick;

        // FPS Calculation (Simple moving average)
        static double fps_timer = 0;
        static int frames = 0;
        fps_timer += frame_time;
        frames++;
        if(fps_timer >= 1.0) {
            g.fps = frames;
            frames = 0;
            fps_timer = 0;
        }

        // Clamp delta time (prevents spiral of death if paused/lagging)
        if (frame_time > 0.25) frame_time = 0.25;

        Engine_RunFrame(frame_time);
        
        // Yield CPU to prevent 100% usage on a simple game
        Sleep(1); 
    }

    Engine_Shutdown();
    return 0;
}

// ======================================================================================
// ENGINE IMPLEMENTATION
// ======================================================================================

void Engine_Initialize() {
    // 1. Setup Console
    r.hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA(APP_TITLE);

    // 2. Setup Screen Buffers (Double Buffering)
    COORD buffer_size = { SCREEN_WIDTH, SCREEN_HEIGHT };
    SMALL_RECT window_rect = { 0, 0, (SHORT)(SCREEN_WIDTH - 1), (SHORT)(SCREEN_HEIGHT - 1) };

    for(int i=0; i<2; i++) {
        r.hBuffer[i] = CreateConsoleScreenBuffer(
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CONSOLE_TEXTMODE_BUFFER,
            NULL
        );
        SetConsoleScreenBufferSize(r.hBuffer[i], buffer_size);
        SetConsoleWindowInfo(r.hBuffer[i], TRUE, &window_rect);
        
        // Hide Cursor
        CONSOLE_CURSOR_INFO cursor = { 1, FALSE };
        SetConsoleCursorInfo(r.hBuffer[i], &cursor);
    }

    // 3. Allocate Memory
    r.pixel_data = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * SCREEN_WIDTH * SCREEN_HEIGHT);
    r.back_buffer_index = 0;

    // 4. Initialize State
    g.is_running = true;
    g.scene = SCENE_MENU;
    srand((unsigned int)time(NULL));
}

void Engine_Shutdown() {
    free(r.pixel_data);
    CloseHandle(r.hBuffer[0]);
    CloseHandle(r.hBuffer[1]);
}

void Engine_RunFrame(double dt) {
    g.time_accumulator += dt;

    Input_Process();

    // Fixed Time Step Update
    // Logic runs at a constant rate, regardless of FPS
    // If Dashing, logic runs faster relative to real time
    double current_tick_rate = BASE_TICK_RATE;
    if (g.is_dashing && g.scene == SCENE_GAME) {
        current_tick_rate /= DASH_MULTIPLIER;
    }

    while (g.time_accumulator >= current_tick_rate) {
        if (g.scene == SCENE_GAME && g.has_started && !g.is_paused) {
            Game_UpdateFixed();
        }
        
        // Particles update in all scenes if we wanted, but let's keep them in game
        if (g.scene == SCENE_GAME) {
            Particles_Update();
        }
        
        g.time_accumulator -= current_tick_rate;
    }

    Game_Draw();
    Render_Present();
}

// ======================================================================================
// RENDERER IMPLEMENTATION
// ======================================================================================

void Render_Clear(WORD color) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        r.pixel_data[i].Char.UnicodeChar = L' ';
        r.pixel_data[i].Attributes = color;
    }
}

void Render_Char(int x, int y, wchar_t ch, WORD attr) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    int idx = y * SCREEN_WIDTH + x;
    r.pixel_data[idx].Char.UnicodeChar = ch;
    r.pixel_data[idx].Attributes = attr;
}

void Render_String(int x, int y, const char* str, WORD attr) {
    while (*str) {
        Render_Char(x++, y, *str++, attr);
    }
}

void Render_Box(int x, int y, int w, int h, WORD attr) {
    // Unicode Box Drawing
    Render_Char(x, y, 0x2554, attr);            // ╔
    Render_Char(x + w - 1, y, 0x2557, attr);    // ╗
    Render_Char(x, y + h - 1, 0x255A, attr);    // ╚
    Render_Char(x + w - 1, y + h - 1, 0x255D, attr); // ╝
    
    for (int i = 1; i < w - 1; i++) {
        Render_Char(x + i, y, 0x2550, attr);         // ═
        Render_Char(x + i, y + h - 1, 0x2550, attr); // ═
    }
    for (int i = 1; i < h - 1; i++) {
        Render_Char(x, y + i, 0x2551, attr);         // ║
        Render_Char(x + w - 1, y + i, 0x2551, attr); // ║
    }
}

void Render_Present() {
    COORD size = { SCREEN_WIDTH, SCREEN_HEIGHT };
    COORD origin = { 0, 0 };
    SMALL_RECT region = { 0, 0, (SHORT)(SCREEN_WIDTH - 1), (SHORT)(SCREEN_HEIGHT - 1) };
    
    WriteConsoleOutputW(
        r.hBuffer[r.back_buffer_index],
        r.pixel_data,
        size,
        origin,
        &region
    );
    
    // Flip Buffers
    SetConsoleActiveScreenBuffer(r.hBuffer[r.back_buffer_index]);
    r.back_buffer_index = !r.back_buffer_index;
}

// ======================================================================================
// INPUT SYSTEM
// ======================================================================================

void Input_Enqueue(Vec2 dir) {
    if (g.input.count < INPUT_QUEUE_SIZE) {
        g.input.queue[g.input.tail] = dir;
        g.input.tail = (g.input.tail + 1) % INPUT_QUEUE_SIZE;
        g.input.count++;
    }
}

void Input_Process() {
    // Helper macro for key state
    #define KEY_PRESSED(vk) (GetAsyncKeyState(vk) & 0x8000)

    // Global Toggles
    if (KEY_PRESSED(VK_F3)) { g.debug_mode = !g.debug_mode; Sleep(200); }

    // Scene Specific Input
    switch (g.scene) {
        case SCENE_MENU:
            if (KEY_PRESSED('1')) { g.mode = MODE_CLASSIC; g.scene = SCENE_GAME; Game_Reset(); }
            if (KEY_PRESSED('2')) { g.mode = MODE_OBSTACLES; g.scene = SCENE_GAME; Game_Reset(); }
            if (KEY_PRESSED('H')) { g.scene = SCENE_SCORES; }
            if (KEY_PRESSED('Q')) { g.is_running = false; }
            Sleep(50); // Small debounce
            break;

        case SCENE_SCORES:
            if (KEY_PRESSED(VK_ESCAPE)) { g.scene = SCENE_MENU; Sleep(200); }
            break;

        case SCENE_GAMEOVER:
            // Name Entry Logic
            for (int k = 'A'; k <= 'Z'; k++) {
                if (KEY_PRESSED(k)) {
                    if (g.name_cursor < 10) {
                        g.player_name[g.name_cursor++] = (char)k;
                        g.player_name[g.name_cursor] = '\0';
                        Sleep(150);
                    }
                }
            }
            if (KEY_PRESSED(VK_BACK) && g.name_cursor > 0) {
                g.player_name[--g.name_cursor] = '\0';
                Sleep(150);
            }
            if (KEY_PRESSED(VK_RETURN) && g.name_cursor > 0) {
                Score_Add(g.player_name, g.score);
                Score_Save();
                g.scene = SCENE_SCORES;
                Sleep(200);
            }
            break;

        case SCENE_GAME:
            if (KEY_PRESSED(VK_ESCAPE)) { g.scene = SCENE_MENU; }
            if (KEY_PRESSED('P')) { g.is_paused = !g.is_paused; Sleep(200); }
            
            // Dash Logic
            g.is_dashing = KEY_PRESSED(VK_SHIFT);

            // Movement Logic
            Vec2 new_dir = {0, 0};
            bool input_found = false;

            if (KEY_PRESSED(VK_UP) || KEY_PRESSED('W'))    { new_dir.x = 0; new_dir.y = -1; input_found = true; }
            if (KEY_PRESSED(VK_DOWN) || KEY_PRESSED('S'))  { new_dir.x = 0; new_dir.y = 1; input_found = true; }
            if (KEY_PRESSED(VK_LEFT) || KEY_PRESSED('A'))  { new_dir.x = -1; new_dir.y = 0; input_found = true; }
            if (KEY_PRESSED(VK_RIGHT) || KEY_PRESSED('D')) { new_dir.x = 1; new_dir.y = 0; input_found = true; }

            // Handle "Press to Start"
            if (!g.has_started && input_found) {
                // Prevent starting by reversing into default body (assumed spawning left-to-right)
                if (new_dir.x != -1) {
                    g.has_started = true;
                    g.snake.dir = new_dir;
                    Input_Enqueue(new_dir);
                }
            }
            // Handle Active Gameplay Input
            else if (g.has_started && input_found) {
                // Peek last queued input to prevent 180 turns
                Vec2 last_dir = g.snake.dir;
                if (g.input.count > 0) {
                    int last_idx = (g.input.tail - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
                    last_dir = g.input.queue[last_idx];
                }

                // If not 180 degree turn and not same direction
                if ((new_dir.x != -last_dir.x || new_dir.y != -last_dir.y) &&
                    (new_dir.x != last_dir.x || new_dir.y != last_dir.y)) {
                    Input_Enqueue(new_dir);
                }
            }
            break;
    }
}

// ======================================================================================
// GAMEPLAY LOGIC
// ======================================================================================

void Level_Generate(GameMode mode) {
    memset(g.map, 0, sizeof(g.map));

    // 1. Draw Border
    for(int x=0; x<SCREEN_WIDTH; x++) { g.map[x][0] = 1; g.map[x][SCREEN_HEIGHT-1] = 1; }
    for(int y=0; y<SCREEN_HEIGHT; y++) { g.map[0][y] = 1; g.map[SCREEN_WIDTH-1][y] = 1; }

    // 2. Generate Obstacles (if mode selected)
    if (mode == MODE_OBSTACLES) {
        int count = 20 + rand() % 10;
        for (int i = 0; i < count; i++) {
            int w = 2 + rand() % 6;
            int h = 1 + rand() % 4;
            int x = 2 + rand() % (SCREEN_WIDTH - w - 2);
            int y = 2 + rand() % (SCREEN_HEIGHT - h - 2);
            
            // Simple block filling
            for(int bx=0; bx<w; bx++) {
                for(int by=0; by<h; by++) {
                    g.map[x+bx][y+by] = 1;
                }
            }
        }
    }
}

void Game_Reset() {
    g.score = 0;
    g.combo_multiplier = 1;
    g.combo_timer = 0;
    g.has_started = false;
    g.is_paused = false;
    g.is_dashing = false;
    g.input.count = g.input.head = g.input.tail = 0;

    // Reset Snake
    g.snake.length = 4;
    g.snake.grow_pending = 0;
    g.snake.dir = (Vec2){1, 0};
    g.snake.color_theme = COL_GREEN;
    
    // Spawn in center
    int sx = SCREEN_WIDTH / 2;
    int sy = SCREEN_HEIGHT / 2;
    
    Level_Generate(g.mode);
    
    // Clear Spawn Area
    for(int x=sx-5; x<=sx+5; x++)
        for(int y=sy-5; y<=sy+5; y++)
            if (x>0 && x<SCREEN_WIDTH-1 && y>0 && y<SCREEN_HEIGHT-1)
                g.map[x][y] = 0;

    for(int i=0; i<g.snake.length; i++) {
        g.snake.body[i].x = sx - i;
        g.snake.body[i].y = sy;
    }

    // Spawn First Food
    do {
        g.food.x = rand() % (SCREEN_WIDTH - 2) + 1;
        g.food.y = rand() % (SCREEN_HEIGHT - 2) + 1;
    } while (g.map[g.food.x][g.food.y] != 0);
}

void Game_UpdateFixed() {
    // 1. Process Queued Input
    if (g.input.count > 0) {
        g.snake.dir = g.input.queue[g.input.head];
        g.input.head = (g.input.head + 1) % INPUT_QUEUE_SIZE;
        g.input.count--;
    }

    // 2. Calculate Next Position
    Vec2 head = g.snake.body[0];
    Vec2 next = { head.x + g.snake.dir.x, head.y + g.snake.dir.y };

    // 3. Collision Detection
    bool collision = false;
    
    // Bounds & Walls
    if (next.x < 0 || next.x >= SCREEN_WIDTH || next.y < 0 || next.y >= SCREEN_HEIGHT) collision = true;
    else if (g.map[next.x][next.y] == 1) collision = true;
    
    // Self
    for (int i = 0; i < g.snake.length - 1; i++) { // -1 to allow tail chasing
        if (next.x == g.snake.body[i].x && next.y == g.snake.body[i].y) collision = true;
    }

    if (collision) {
        g.scene = SCENE_GAMEOVER;
        g.name_cursor = 0;
        memset(g.player_name, 0, 16);
        return;
    }

    // 4. Move Snake (Shift segments)
    for (int i = g.snake.length; i > 0; i--) {
        g.snake.body[i] = g.snake.body[i - 1];
    }
    g.snake.body[0] = next;

    // 5. Food Interaction
    if (next.x == g.food.x && next.y == g.food.y) {
        // Score Calculation
        int points = 10;
        points *= g.combo_multiplier;
        if (g.is_dashing) points *= 2; // Dashing Bonus
        
        g.score += points;
        g.snake.grow_pending++;
        
        // Combo Logic
        g.combo_multiplier++;
        if (g.combo_multiplier > MAX_COMBO) g.combo_multiplier = MAX_COMBO;
        g.combo_timer = COMBO_WINDOW;

        // Visuals
        Particles_Spawn(next.x, next.y, 10 + (g.combo_multiplier * 5), g.snake.color_theme);
        
        // Dynamic Difficulty (Color)
        if (g.score > 500) g.snake.color_theme = COL_MAGENTA;
        else if (g.score > 250) g.snake.color_theme = COL_CYAN;
        else if (g.score > 100) g.snake.color_theme = COL_YELLOW;

        // Respawn Food
        bool valid = false;
        while (!valid) {
            valid = true;
            g.food.x = rand() % (SCREEN_WIDTH - 2) + 1;
            g.food.y = rand() % (SCREEN_HEIGHT - 2) + 1;
            if (g.map[g.food.x][g.food.y] != 0) valid = false;
            for(int i=0; i<g.snake.length; i++) 
                if(g.snake.body[i].x == g.food.x && g.snake.body[i].y == g.food.y) valid = false;
        }
    }

    // 6. Growth Processing
    if (g.snake.grow_pending > 0) {
        g.snake.length++;
        g.snake.grow_pending--;
    }
    
    // 7. Combo Decay
    if (g.combo_timer > 0) {
        g.combo_timer--;
        if (g.combo_timer == 0) {
            g.combo_multiplier = 1; // Reset combo
        }
    }
}

// ======================================================================================
// PARTICLES & VISUALS
// ======================================================================================

void Particles_Spawn(int x, int y, int count, WORD color) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g.particles[i].life <= 0 && count > 0) {
            g.particles[i].pos = (Vec2){x, y};
            g.particles[i].vel.x = (rand() % 3) - 1;
            g.particles[i].vel.y = (rand() % 3) - 1;
            g.particles[i].life = 5 + rand() % 10;
            g.particles[i].color = color;
            g.particles[i].icon = (rand() % 2) ? '*' : '+';
            count--;
        }
    }
}

void Particles_Update() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g.particles[i].life > 0) {
            g.particles[i].pos.x += g.particles[i].vel.x;
            g.particles[i].pos.y += g.particles[i].vel.y;
            g.particles[i].life--;
        }
    }
}

void Game_Draw() {
    Render_Clear(COL_BLACK);

    if (g.scene == SCENE_MENU) {
        // Title Art
        Render_Box(15, 5, 50, 20, COL_BLUE);
        Render_String(32, 7, "SNAKE ENGINE", COL_CYAN | FOREGROUND_INTENSITY);
        Render_String(32, 8, "============", COL_CYAN);
        
        Render_String(28, 11, "[1] Classic Mode", COL_WHITE);
        Render_String(28, 12, "[2] Obstacle Mode", COL_WHITE);
        Render_String(28, 14, "[H] Leaderboard", COL_YELLOW);
        Render_String(28, 16, "[Q] Quit to Desktop", COL_RED);
        
        Render_String(20, 22, "Tip: Hold SHIFT to Dash (2x Points!)", COL_GRAY);
    }
    else if (g.scene == SCENE_GAMEOVER) {
        Render_Box(25, 10, 30, 10, COL_RED);
        Render_String(35, 12, "GAME OVER", COL_RED | FOREGROUND_INTENSITY);
        
        char buf[32];
        sprintf(buf, "Final Score: %d", g.score);
        Render_String(32, 14, buf, COL_WHITE);
        
        Render_String(27, 16, "Name: ", COL_YELLOW);
        Render_String(33, 16, g.player_name, COL_WHITE | FOREGROUND_INTENSITY);
        // Blinking cursor
        if ((clock() / 250) % 2 == 0) Render_Char(33 + g.name_cursor, 16, '_', COL_WHITE);
    }
    else if (g.scene == SCENE_SCORES) {
        Render_Box(20, 5, 40, 20, COL_YELLOW);
        Render_String(35, 7, "LEADERBOARD", COL_YELLOW | FOREGROUND_INTENSITY);
        
        Render_String(25, 9, "Name          Score", COL_GRAY);
        Render_String(25, 10, "--------------------", COL_GRAY);
        
        for(int i=0; i<MAX_SCORES; i++) {
            if (leaderboard[i].score > 0) {
                char buf[64];
                sprintf(buf, "%-12s  %6d", leaderboard[i].name, leaderboard[i].score);
                Render_String(25, 12 + i, buf, COL_WHITE);
            }
        }
        Render_String(25, 22, "[ESC] Return", COL_WHITE);
    }
    else if (g.scene == SCENE_GAME) {
        // 1. Draw Map
        for(int y=0; y<SCREEN_HEIGHT; y++) {
            for(int x=0; x<SCREEN_WIDTH; x++) {
                if(g.map[x][y]) Render_Char(x, y, 0x2588, COL_GRAY);
            }
        }

        // 2. Draw Food
        Render_Char(g.food.x, g.food.y, 0x2666, COL_RED | FOREGROUND_INTENSITY);

        // 3. Draw Snake
        for(int i=0; i<g.snake.length; i++) {
            WORD c = g.snake.color_theme;
            if (g.is_dashing) c = COL_RED | FOREGROUND_INTENSITY; // Turn red when dashing
            else if (i == 0) c |= FOREGROUND_INTENSITY; // Bright head
            
            Render_Char(g.snake.body[i].x, g.snake.body[i].y, (i==0)?0x2588:0x2592, c);
        }

        // 4. Draw Particles
        for(int i=0; i<MAX_PARTICLES; i++) {
            if (g.particles[i].life > 0) {
                Render_Char(g.particles[i].pos.x, g.particles[i].pos.y, g.particles[i].icon, g.particles[i].color);
            }
        }

        // 5. Draw UI / HUD
        char ui[128];
        sprintf(ui, " SCORE: %d | COMBO: x%d | DASH: %s ", 
            g.score, g.combo_multiplier, g.is_dashing ? "ON" : "OFF");
        Render_String(2, 0, ui, COL_BLACK | (COL_WHITE << 4));

        // Combo Bar visual
        if (g.combo_multiplier > 1) {
            for(int k=0; k<g.combo_timer/2; k++) 
                Render_Char(2 + k, 1, 0x2580, COL_YELLOW);
        }

        // Start Prompt
        if (!g.has_started) {
            Render_String(SCREEN_WIDTH/2 - 12, SCREEN_HEIGHT/2 - 5, "PRESS ARROWS TO START", COL_WHITE | FOREGROUND_INTENSITY);
        }

        // Debug Overlay
        if (g.debug_mode) {
            char dbg[64];
            sprintf(dbg, "FPS: %.0f | X:%d Y:%d", g.fps, g.snake.body[0].x, g.snake.body[0].y);
            Render_String(SCREEN_WIDTH - 25, 0, dbg, COL_GREEN);
        }
    }
}

// ======================================================================================
// PERSISTENCE
// ======================================================================================

void Score_Load() {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (f) {
        fread(leaderboard, sizeof(ScoreEntry), MAX_SCORES, f);
        fclose(f);
    } else {
        memset(leaderboard, 0, sizeof(leaderboard));
    }
}

void Score_Save() {
    FILE* f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(leaderboard, sizeof(ScoreEntry), MAX_SCORES, f);
        fclose(f);
    }
}

void Score_Add(const char* name, int score) {
    ScoreEntry entry;
    strncpy(entry.name, name, 15);
    entry.score = score;
    entry.max_combo = 0; // Not currently tracked in UI but stored

    for(int i=0; i<MAX_SCORES; i++) {
        if(score > leaderboard[i].score) {
            // Shift lower scores down
            for(int j=MAX_SCORES-1; j>i; j--) {
                leaderboard[j] = leaderboard[j-1];
            }
            leaderboard[i] = entry;
            break;
        }
    }
}