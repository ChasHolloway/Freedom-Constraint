// fc_gol_runtime_v5.c
// FC-controlled Game of Life with runtime grid size
// + NON-toroidal viewport camera that stops at edges
// + X/Y sliders under the right panel
// + Mini-map in left panel showing viewport position as a white square

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>

// Default grid size (if no args are provided)
#define DEFAULT_GRID_WIDTH   100
#define DEFAULT_GRID_HEIGHT  100

// Viewport size in cells (camera window size)
#define VIEW_W 100
#define VIEW_H 100

// Simulation parameters
#define UPDATE_HZ 10.0f
#define UPDATE_DT (1.0f / UPDATE_HZ)

// Cell states
#define DEAD  0
#define ALIVE 1

typedef enum {
    SIM_STOPPED = 0,
    SIM_RUNNING,
    SIM_PAUSED
} SimState;

// Global grid buffers (allocated at runtime)
static unsigned char *grid = NULL;
static unsigned char *nextGrid = NULL;
static int gridWidth  = DEFAULT_GRID_WIDTH;
static int gridHeight = DEFAULT_GRID_HEIGHT;

// Viewport top-left (in cell coordinates)
static int viewX = 0;
static int viewY = 0;

// For sliders (float representation of viewport position)
static float sliderViewX = 0.0f;
static float sliderViewY = 0.0f;

// Simulation state
static SimState simState = SIM_STOPPED;
static long long cycleCount = 0;
static double elapsedSeconds = 0.0;

// FC / RTC thresholds
static const float F_HIGH = 0.55f;
static const float F_LOW  = 0.45f;

// --- Helpers for 1D indexing ---
static inline int Index(int x, int y) {
    return y * gridWidth + x;
}

// Toroidal wrapping for physics (neighbors only)
static inline int WrapX(int x) {
    if (x < 0) return gridWidth - 1;
    if (x >= gridWidth) return 0;
    return x;
}

static inline int WrapY(int y) {
    if (y < 0) return gridHeight - 1;
    if (y >= gridHeight) return 0;
    return y;
}

// Allocate grids for given dimensions
static bool AllocateGrids(int w, int h) {
    if (grid)     { free(grid);     grid = NULL; }
    if (nextGrid) { free(nextGrid); nextGrid = NULL; }

    gridWidth  = w;
    gridHeight = h;

    size_t total = (size_t)gridWidth * (size_t)gridHeight;
    grid     = (unsigned char *)malloc(total * sizeof(unsigned char));
    nextGrid = (unsigned char *)malloc(total * sizeof(unsigned char));

    if (!grid || !nextGrid) {
        fprintf(stderr, "Error: Failed to allocate grid memory for %dx%d\n", w, h);
        return false;
    }

    return true;
}

// Initialize grid with random 50/50 alive/dead
static void RandomizeGrid(void) {
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            grid[Index(x, y)] = (rand() % 2) ? ALIVE : DEAD;
        }
    }
}

// Reset simulation state (used by STOP)
static void ResetSim(void) {
    RandomizeGrid();
    cycleCount = 0;
    elapsedSeconds = 0.0;
    simState = SIM_STOPPED;

    // Reset viewport and sliders
    viewX = 0;
    viewY = 0;
    sliderViewX = 0.0f;
    sliderViewY = 0.0f;
}

// Count live neighbors (toroidal physics)
static int CountNeighbors(int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = WrapX(x + dx);
            int ny = WrapY(y + dy);
            if (grid[Index(nx, ny)] == ALIVE) count++;
        }
    }
    return count;
}

// One FC-controlled GOL step
static void StepSim(void) {
    // 1. Measure C (live cells)
    int R = gridWidth * gridHeight;
    int C = 0;
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            if (grid[Index(x, y)] == ALIVE) C++;
        }
    }

    // 2. Compute F = (R - C) / R
    float F = (float)(R - C) / (float)R;

    // 3. RTC: choose birth rule based on F
    int birthNeighbors = 3; // default B3
    if (F > F_HIGH) {
        birthNeighbors = 2; // B2: more permissive
    } else if (F < F_LOW) {
        birthNeighbors = 4; // B4: more restrictive
    }

    // 4. Apply GOL rules with FC-constrained Birth
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            int neighbors = CountNeighbors(x, y);

            if (grid[Index(x, y)] == ALIVE) {
                if (neighbors == 2 || neighbors == 3)
                    nextGrid[Index(x, y)] = ALIVE;
                else
                    nextGrid[Index(x, y)] = DEAD;
            } else {
                if (neighbors == birthNeighbors)
                    nextGrid[Index(x, y)] = ALIVE;
                else
                    nextGrid[Index(x, y)] = DEAD;
            }
        }
    }

    // 5. Swap grids
    for (int y = 0; y < gridHeight; y++) {
        for (int x = 0; x < gridWidth; x++) {
            grid[Index(x, y)] = nextGrid[Index(x, y)];
        }
    }

    cycleCount++;
}

// Format time as days:hours:minutes:seconds
static void FormatElapsedTime(char *buffer, int bufSize, double seconds) {
    long long total = (long long)seconds;
    int s = (int)(total % 60);
    int m = (int)((total / 60) % 60);
    int h = (int)((total / 3600) % 24);
    long long d = total / 86400;

    if (d > 0)
        snprintf(buffer, bufSize, "%lldd %02dh %02dm %02ds", d, h, m, s);
    else
        snprintf(buffer, bufSize, "%02dh %02dm %02ds", h, m, s);
}

int main(int argc, char **argv) {
    // Parse command-line grid size: ./fc_gol_runtime_v5 width height
    int w = DEFAULT_GRID_WIDTH;
    int h = DEFAULT_GRID_HEIGHT;

    if (argc >= 3) {
        int argW = atoi(argv[1]);
        int argH = atoi(argv[2]);
        if (argW > 0 && argH > 0) {
            w = argW;
            h = argH;
        } else {
            fprintf(stderr, "Warning: invalid grid size arguments; using default %dx%d\n",
                    DEFAULT_GRID_WIDTH, DEFAULT_GRID_HEIGHT);
        }
    }

    srand((unsigned int)time(NULL));

    if (!AllocateGrids(w, h)) {
        return 1;
    }

    ResetSim();

    const int screenWidth  = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "FC-controlled Game of Life");

    // Layout
    Rectangle leftPanel  = (Rectangle){ 40, 40, 480, 480 };   // big black box
    Rectangle rightPanel = (Rectangle){ 700, 40, 480, 480 };  // viewport view

    Rectangle btnStart = (Rectangle){  80, 540, 120, 40 };
    Rectangle btnStop  = (Rectangle){ 220, 540, 120, 40 };
    Rectangle btnPause = (Rectangle){ 360, 540, 120, 40 };

    Rectangle boxCycles = (Rectangle){  80, 600, 260, 40 };
    Rectangle boxTime   = (Rectangle){ 360, 600, 260, 40 };

    // Slider panel under the right viewport
    Rectangle sliderPanel = (Rectangle){
        rightPanel.x,
        rightPanel.y + rightPanel.height + 20,
        rightPanel.width,
        80
    };

    Rectangle sliderXRect = (Rectangle){
        sliderPanel.x + 10,
        sliderPanel.y + 15,
        sliderPanel.width - 20,
        20
    };

    Rectangle sliderYRect = (Rectangle){
        sliderPanel.x + 10,
        sliderPanel.y + 45,
        sliderPanel.width - 20,
        20
    };

    // Mini-map rectangle inside the left panel (represents entire grid),
    // white square inside it will represent the current viewport.
    Rectangle miniMapRect = (Rectangle){
        leftPanel.x + 40,
        leftPanel.y + 140,
        320,
        320
    };

    float accumulator = 0.0f;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Simulation timing ---
        if (simState == SIM_RUNNING) {
            accumulator += dt;
            while (accumulator >= UPDATE_DT) {
                StepSim();
                elapsedSeconds += UPDATE_DT;
                accumulator -= UPDATE_DT;
            }
        } else {
            accumulator = 0.0f;
        }

        // Effective viewport size (in case grid smaller than VIEW_W/H)
        int viewW = (gridWidth  < VIEW_W) ? gridWidth  : VIEW_W;
        int viewH = (gridHeight < VIEW_H) ? gridHeight : VIEW_H;

        int maxStartX = (gridWidth  > viewW) ? (gridWidth  - viewW) : 0;
        int maxStartY = (gridHeight > viewH) ? (gridHeight - viewH) : 0;

        // --- Begin drawing ---
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // LEFT PANEL BACKGROUND
        DrawRectangleRec(leftPanel, BLACK);

        // Status text at top of left panel
        char buf[128];
        snprintf(buf, sizeof(buf), "Grid Size: %d x %d", gridWidth, gridHeight);
        DrawText(buf, (int)leftPanel.x + 20, (int)leftPanel.y + 20, 24, RAYWHITE);

        snprintf(buf, sizeof(buf), "%d cells", gridWidth * gridHeight);
        DrawText(buf, (int)leftPanel.x + 20, (int)leftPanel.y + 50, 24, RAYWHITE);

        const char *statusText =
            (simState == SIM_RUNNING) ? "running" :
            (simState == SIM_PAUSED)  ? "paused"  :
                                        "stopped";
        snprintf(buf, sizeof(buf), "Status: %s", statusText);
        DrawText(buf, (int)leftPanel.x + 20, (int)leftPanel.y + 80, 20, RAYWHITE);

        // --- Mini-map: shows entire grid and viewport position ---
        // Background of mini-map
        DrawRectangleRec(miniMapRect, (Color){40, 40, 40, 255});
        DrawRectangleLinesEx(miniMapRect, 2, RAYWHITE);

        if (gridWidth > 0 && gridHeight > 0) {
            float scaleX = miniMapRect.width  / (float)gridWidth;
            float scaleY = miniMapRect.height / (float)gridHeight;

            // Current viewport in mini-map coordinates
            float vpX = miniMapRect.x + viewX * scaleX;
            float vpY = miniMapRect.y + viewY * scaleY;
            float vpW = viewW * scaleX;
            float vpH = viewH * scaleY;

            Rectangle vpRect = (Rectangle){ vpX, vpY, vpW, vpH };

            // White square (outline) indicating where the camera/viewport is
            DrawRectangleLinesEx(vpRect, 2, RAYWHITE);
        }

        // Buttons
        if (GuiButton(btnStart, "START")) {
            if (simState == SIM_STOPPED || simState == SIM_PAUSED) {
                simState = SIM_RUNNING;
            }
        }

        if (GuiButton(btnStop, "STOP")) {
            ResetSim();
        }

        if (GuiButton(btnPause, "PAUSE")) {
            if (simState == SIM_RUNNING) {
                simState = SIM_PAUSED;
            }
        }

        // Cycles box
        DrawRectangleRec(boxCycles, LIGHTGRAY);
        DrawRectangleLinesEx(boxCycles, 2, DARKGRAY);
        snprintf(buf, sizeof(buf), "cycles: %lld", cycleCount);
        DrawText(buf, (int)boxCycles.x + 10, (int)boxCycles.y + 10, 20, BLACK);

        // Time box
        DrawRectangleRec(boxTime, LIGHTGRAY);
        DrawRectangleLinesEx(boxTime, 2, DARKGRAY);
        char timeStr[64];
        FormatElapsedTime(timeStr, sizeof(timeStr), elapsedSeconds);
        snprintf(buf, sizeof(buf), "time: %s", timeStr);
        DrawText(buf, (int)boxTime.x + 10, (int)boxTime.y + 10, 20, BLACK);

        // RIGHT PANEL BORDER
        DrawRectangleLinesEx(rightPanel, 2, BLACK);

        // --- Slider panel under right viewport ---
        DrawRectangleRec(sliderPanel, BLACK);

        float maxSliderX = (float)maxStartX;
        float maxSliderY = (float)maxStartY;

        DrawText("Viewport X", (int)sliderXRect.x, (int)sliderXRect.y - 18, 16, RAYWHITE);
        GuiSliderBar(sliderXRect, NULL, NULL, &sliderViewX, 0.0f, maxSliderX);

        DrawText("Viewport Y", (int)sliderYRect.x, (int)sliderYRect.y - 18, 16, RAYWHITE);
        GuiSliderBar(sliderYRect, NULL, NULL, &sliderViewY, 0.0f, maxSliderY);

        // Convert slider floats to integer viewport position, clamped
        viewX = (int)(sliderViewX + 0.5f);
        viewY = (int)(sliderViewY + 0.5f);

        if (viewX < 0) viewX = 0;
        if (viewX > maxStartX) viewX = maxStartX;

        if (viewY < 0) viewY = 0;
        if (viewY > maxStartY) viewY = maxStartY;

        // --- Draw viewport (no wrapping for camera) ---
        if (viewW > 0 && viewH > 0) {
            float cellSizeX = rightPanel.width  / (float)viewW;
            float cellSizeY = rightPanel.height / (float)viewH;
            float cellSize  = (cellSizeX < cellSizeY) ? cellSizeX : cellSizeY;

            for (int vy = 0; vy < viewH; vy++) {
                for (int vx = 0; vx < viewW; vx++) {
                    int gx = viewX + vx;
                    int gy = viewY + vy;

                    int state = grid[Index(gx, gy)];
                    Color col = (state == ALIVE) ? BLACK : RAYWHITE;

                    float px = rightPanel.x + vx * cellSize;
                    float py = rightPanel.y + vy * cellSize;

                    DrawRectangle((int)px, (int)py,
                                  (int)(cellSize + 1), (int)(cellSize + 1),
                                  col);
                }
            }
        }

        EndDrawing();
    }

    if (grid)     free(grid);
    if (nextGrid) free(nextGrid);

    CloseWindow();
    return 0;
}
