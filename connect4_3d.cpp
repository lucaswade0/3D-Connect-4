#include "raylib.h"
#include "raymath.h"
#include <array>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <algorithm>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define MCTS_ITERS 15000
#else
#define MCTS_ITERS 50000
#endif

// ─── Game Constants ─────────────────────────────────────────────────────────
constexpr int SZ = 4;
constexpr int TOTAL_CELLS = SZ * SZ * SZ;
constexpr float SPACING = 2.0f;
constexpr float SPHERE_RAD = 0.4f;

enum CellState : int8_t { EMPTY = 0, PLAYER = 1, AI_PIECE = 2 };
enum GameState { PLAYING, AI_THINKING, GAME_OVER };

constexpr int DIRS[13][3] = {
    {1,0,0},{0,1,0},{0,0,1},
    {1,1,0},{1,-1,0},{1,0,1},{1,0,-1},
    {0,1,1},{0,1,-1},
    {1,1,1},{1,1,-1},{1,-1,1},{1,-1,-1}
};

// ─── Board ──────────────────────────────────────────────────────────────────
struct Board {
    int8_t cells[SZ][SZ][SZ] = {};
    int moveCount = 0;

    static bool inBounds(int v) { return v >= 0 && v < SZ; }

    int drop(int row, int col, int8_t piece) {
        for (int l = 0; l < SZ; l++) {
            if (cells[l][row][col] == EMPTY) {
                cells[l][row][col] = piece;
                moveCount++;
                return l;
            }
        }
        return -1;
    }

    bool columnFull(int row, int col) const { return cells[SZ-1][row][col] != EMPTY; }
    bool isFull() const { return moveCount >= TOTAL_CELLS; }

    bool checkWin(int8_t piece) const {
        for (int l = 0; l < SZ; l++)
            for (int r = 0; r < SZ; r++)
                for (int c = 0; c < SZ; c++) {
                    if (cells[l][r][c] != piece) continue;
                    for (auto& d : DIRS) {
                        int count = 0;
                        for (int s = 0; s < SZ; s++) {
                            int nl=l+d[0]*s, nr=r+d[1]*s, nc=c+d[2]*s;
                            if (!inBounds(nl)||!inBounds(nr)||!inBounds(nc)) break;
                            if (cells[nl][nr][nc] != piece) break;
                            count++;
                        }
                        if (count == SZ) return true;
                    }
                }
        return false;
    }

    std::vector<int> getValidMoves() const {
        std::vector<int> moves;
        for (int r = 0; r < SZ; r++)
            for (int c = 0; c < SZ; c++)
                if (!columnFull(r, c)) moves.push_back(r * SZ + c);
        return moves;
    }
};

// ─── MCTS ───────────────────────────────────────────────────────────────────
struct MCTSNode {
    int move;
    int8_t player;
    int visits = 0;
    float wins = 0.0f;
    MCTSNode* parent = nullptr;
    std::vector<MCTSNode*> children;
    std::vector<int> untriedMoves;

    MCTSNode(int mv, int8_t pl, MCTSNode* par, const std::vector<int>& valid)
        : move(mv), player(pl), parent(par), untriedMoves(valid) {}
    ~MCTSNode() { for (auto c : children) delete c; }

    MCTSNode* bestChild(float expW) const {
        MCTSNode* best = nullptr;
        float bestVal = -1e9f, logV = logf((float)visits);
        for (auto c : children) {
            float val = c->wins/(float)c->visits + expW*sqrtf(2.0f*logV/(float)c->visits);
            if (val > bestVal) { bestVal = val; best = c; }
        }
        return best;
    }

    MCTSNode* expand(Board& board) {
        int idx = rand() % untriedMoves.size();
        int mv = untriedMoves[idx];
        untriedMoves.erase(untriedMoves.begin() + idx);
        int8_t next = (player == PLAYER) ? AI_PIECE : PLAYER;
        board.drop(mv/SZ, mv%SZ, next);
        auto child = new MCTSNode(mv, next, this, board.getValidMoves());
        children.push_back(child);
        return child;
    }
};

int mctsSearch(Board board, int8_t aiP) {
    auto valid = board.getValidMoves();
    if (valid.empty()) return -1;
    if (valid.size() == 1) return valid[0];

    int8_t humanP = (aiP == PLAYER) ? AI_PIECE : PLAYER;
    auto root = new MCTSNode(-1, humanP, nullptr, valid);

    for (int i = 0; i < MCTS_ITERS; i++) {
        Board sim = board;
        MCTSNode* node = root;
        while (node->untriedMoves.empty() && !node->children.empty()) {
            node = node->bestChild(1.41f);
            sim.drop(node->move/SZ, node->move%SZ, node->player);
        }
        if (!node->untriedMoves.empty() && !sim.checkWin(PLAYER) && !sim.checkWin(AI_PIECE) && !sim.isFull())
            node = node->expand(sim);

        int8_t sp = node->player;
        while (!sim.checkWin(PLAYER) && !sim.checkWin(AI_PIECE) && !sim.isFull()) {
            auto moves = sim.getValidMoves();
            if (moves.empty()) break;
            int mv = moves[rand() % moves.size()];
            sp = (sp == PLAYER) ? AI_PIECE : PLAYER;
            sim.drop(mv/SZ, mv%SZ, sp);
        }

        float result = sim.checkWin(aiP) ? 1.0f : sim.checkWin(humanP) ? 0.0f : 0.5f;
        while (node) {
            node->visits++;
            node->wins += (node->player == aiP) ? result : (1.0f - result);
            node = node->parent;
        }
    }

    MCTSNode* best = nullptr;
    int bestV = -1;
    for (auto c : root->children)
        if (c->visits > bestV) { bestV = c->visits; best = c; }
    int bestMove = best ? best->move : valid[0];
    delete root;
    return bestMove;
}

// ─── Rendering ──────────────────────────────────────────────────────────────
Vector3 cellPos(int layer, int row, int col) {
    return {
        (col - (SZ-1)/2.0f) * SPACING,
        layer * SPACING + SPHERE_RAD,
        (row - (SZ-1)/2.0f) * SPACING
    };
}

void drawGridFrame() {
    Color fc = {100, 100, 120, 80};
    for (int r = 0; r < SZ; r++)
        for (int c = 0; c < SZ; c++) {
            Vector3 bot = cellPos(0, r, c); bot.y -= SPHERE_RAD;
            Vector3 top = cellPos(SZ-1, r, c); top.y += SPHERE_RAD;
            DrawCylinderEx(bot, top, 0.05f, 0.05f, 8, fc);
        }
    for (int l = 0; l < SZ; l++) {
        for (int r = 0; r < SZ; r++) DrawLine3D(cellPos(l,r,0), cellPos(l,r,SZ-1), fc);
        for (int c = 0; c < SZ; c++) DrawLine3D(cellPos(l,0,c), cellPos(l,SZ-1,c), fc);
    }
}

// ─── Game State (global for emscripten callback) ────────────────────────────
struct Game {
    Board board;
    Camera3D camera;
    GameState state = PLAYING;
    int8_t winner = EMPTY;
    int hoverRow = -1, hoverCol = -1;
    int aiMoveResult = -1;
    bool aiDone = false;
    float aiThinkTimer = 0.0f;
    float camAngle = 0.7f, camHeight = 10.0f, camDist = 16.0f;
    std::string statusMsg = "Your turn! Click a column to drop.";
    Color statusColor = WHITE;
    int screenW, screenH;

    struct { int row, col, targetLayer; int8_t piece; float y, targetY; bool active = false; } falling;
    struct { Vector3 positions[SZ]; bool active = false; } winLine;

    void updateCam() {
        camera.position = { cosf(camAngle)*camDist, camHeight, sinf(camAngle)*camDist };
    }

    void findWinLine(int8_t piece) {
        for (int l = 0; l < SZ; l++)
            for (int r = 0; r < SZ; r++)
                for (int c = 0; c < SZ; c++) {
                    if (board.cells[l][r][c] != piece) continue;
                    for (auto& d : DIRS) {
                        int count = 0;
                        for (int s = 0; s < SZ; s++) {
                            int nl=l+d[0]*s, nr=r+d[1]*s, nc=c+d[2]*s;
                            if (nl<0||nl>=SZ||nr<0||nr>=SZ||nc<0||nc>=SZ) break;
                            if (board.cells[nl][nr][nc] != piece) break;
                            count++;
                        }
                        if (count == SZ) {
                            for (int s = 0; s < SZ; s++)
                                winLine.positions[s] = cellPos(l+d[0]*s, r+d[1]*s, c+d[2]*s);
                            winLine.active = true;
                            return;
                        }
                    }
                }
    }

    void reset() {
        board = {};
        state = PLAYING;
        winner = EMPTY;
        winLine.active = false;
        falling.active = false;
        statusMsg = "Your turn! Click a column to drop.";
        statusColor = WHITE;
    }
};

Game game;

void gameFrame() {
    float dt = GetFrameTime();
    auto& g = game;

    // Camera controls — mouse
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        g.camAngle -= GetMouseDelta().x * 0.005f;
        g.camHeight -= GetMouseDelta().y * 0.05f;
        g.camHeight = Clamp(g.camHeight, 2.0f, 20.0f);
    }
    g.camDist -= GetMouseWheelMove() * 1.0f;
    g.camDist = Clamp(g.camDist, 8.0f, 30.0f);
    // Camera controls — arrow keys
    if (IsKeyDown(KEY_LEFT))  g.camAngle += 2.0f * dt;
    if (IsKeyDown(KEY_RIGHT)) g.camAngle -= 2.0f * dt;
    if (IsKeyDown(KEY_UP))    g.camHeight = Clamp(g.camHeight + 5.0f * dt, 2.0f, 20.0f);
    if (IsKeyDown(KEY_DOWN))  g.camHeight = Clamp(g.camHeight - 5.0f * dt, 2.0f, 20.0f);
    g.updateCam();

    // Hover detection
    g.hoverRow = -1; g.hoverCol = -1;
    if (g.state == PLAYING && !g.falling.active) {
        Ray ray = GetScreenToWorldRay(GetMousePosition(), g.camera);
        float bestDist = 1e9f;
        for (int r = 0; r < SZ; r++)
            for (int c = 0; c < SZ; c++) {
                if (g.board.columnFull(r, c)) continue;
                Vector3 center = cellPos(SZ/2, r, c);
                center.y = SZ * SPACING / 2.0f;
                BoundingBox bb = {
                    {center.x - SPACING*0.4f, 0, center.z - SPACING*0.4f},
                    {center.x + SPACING*0.4f, SZ*SPACING, center.z + SPACING*0.4f}
                };
                RayCollision col2 = GetRayCollisionBox(ray, bb);
                if (col2.hit && col2.distance < bestDist) {
                    bestDist = col2.distance;
                    g.hoverRow = r; g.hoverCol = c;
                }
            }
    }

    // Click to place
    if (g.state == PLAYING && !g.falling.active &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g.hoverRow >= 0) {
        int tl = -1;
        for (int l = 0; l < SZ; l++)
            if (g.board.cells[l][g.hoverRow][g.hoverCol] == EMPTY) { tl = l; break; }
        if (tl >= 0) {
            g.falling = {g.hoverRow, g.hoverCol, tl, PLAYER,
                         cellPos(SZ, g.hoverRow, g.hoverCol).y,
                         cellPos(tl, g.hoverRow, g.hoverCol).y, true};
        }
    }

    // Falling animation
    if (g.falling.active) {
        g.falling.y -= dt * 15.0f;
        if (g.falling.y <= g.falling.targetY) {
            g.falling.y = g.falling.targetY;
            g.falling.active = false;
            g.board.drop(g.falling.row, g.falling.col, g.falling.piece);

            if (g.board.checkWin(g.falling.piece)) {
                g.state = GAME_OVER;
                g.winner = g.falling.piece;
                g.findWinLine(g.winner);
                if (g.winner == PLAYER) {
                    g.statusMsg = "You win!!! Press R to restart.";
                    g.statusColor = {100, 200, 255, 255};
                } else {
                    g.statusMsg = "AI wins! Press R to restart.";
                    g.statusColor = {255, 80, 80, 255};
                }
            } else if (g.board.isFull()) {
                g.state = GAME_OVER;
                g.statusMsg = "Draw! Press R to restart.";
                g.statusColor = YELLOW;
            } else if (g.falling.piece == PLAYER) {
                g.state = AI_THINKING;
                g.statusMsg = "AI is thinking...";
                g.statusColor = {255, 180, 80, 255};
                g.aiThinkTimer = 0.0f;
                g.aiDone = false;
            } else {
                g.state = PLAYING;
                g.statusMsg = "Your turn! Click a column to drop.";
                g.statusColor = WHITE;
            }
        }
    }

    // AI
    if (g.state == AI_THINKING && !g.falling.active) {
        g.aiThinkTimer += dt;
        if (g.aiThinkTimer > 0.1f && !g.aiDone) {
            g.aiMoveResult = mctsSearch(g.board, AI_PIECE);
            g.aiDone = true;
        }
        if (g.aiDone) {
            int r = g.aiMoveResult / SZ, c = g.aiMoveResult % SZ;
            int tl = -1;
            for (int l = 0; l < SZ; l++)
                if (g.board.cells[l][r][c] == EMPTY) { tl = l; break; }
            if (tl >= 0) {
                g.falling = {r, c, tl, AI_PIECE,
                             cellPos(SZ, r, c).y, cellPos(tl, r, c).y, true};
                g.aiDone = false;
            }
        }
    }

    // Restart
    if (g.state == GAME_OVER && IsKeyPressed(KEY_R)) g.reset();

    // ══════════ DRAW ══════════
    BeginDrawing();
    ClearBackground({20, 20, 30, 255});
    BeginMode3D(g.camera);

    DrawCube({0, -0.3f, 0}, SZ*SPACING+1, 0.3f, SZ*SPACING+1, {40, 40, 55, 255});
    DrawCubeWires({0, -0.3f, 0}, SZ*SPACING+1, 0.3f, SZ*SPACING+1, {60, 60, 80, 255});
    drawGridFrame();

    // Pieces
    for (int l = 0; l < SZ; l++)
        for (int r = 0; r < SZ; r++)
            for (int c = 0; c < SZ; c++) {
                if (g.board.cells[l][r][c] == EMPTY) continue;
                Vector3 pos = cellPos(l, r, c);
                Color col = (g.board.cells[l][r][c] == PLAYER)
                    ? Color{60, 140, 255, 255} : Color{255, 70, 70, 255};
                if (g.winLine.active) {
                    bool isWin = false;
                    for (int w = 0; w < SZ; w++) {
                        Vector3 wp = g.winLine.positions[w];
                        if (fabsf(wp.x-pos.x)<0.1f && fabsf(wp.y-pos.y)<0.1f && fabsf(wp.z-pos.z)<0.1f)
                            { isWin = true; break; }
                    }
                    if (isWin) {
                        float pulse = (sinf(GetTime()*5.0f)+1.0f)/2.0f;
                        DrawSphere(pos, SPHERE_RAD+0.05f, ColorBrightness(col, pulse*0.5f));
                        continue;
                    }
                }
                DrawSphere(pos, SPHERE_RAD, col);
            }

    // Falling piece
    if (g.falling.active) {
        Vector3 fp = cellPos(0, g.falling.row, g.falling.col);
        fp.y = g.falling.y;
        DrawSphere(fp, SPHERE_RAD,
            (g.falling.piece == PLAYER) ? Color{60,140,255,255} : Color{255,70,70,255});
    }

    // Hover ghost
    if (g.hoverRow >= 0 && g.state == PLAYING && !g.falling.active) {
        Vector3 gp = cellPos(SZ, g.hoverRow, g.hoverCol);
        gp.y -= SPACING * 0.3f;
        DrawSphere(gp, SPHERE_RAD*0.8f, {60, 140, 255, 100});
        for (int l = 0; l < SZ; l++)
            if (g.board.cells[l][g.hoverRow][g.hoverCol] == EMPTY)
                DrawSphereWires(cellPos(l, g.hoverRow, g.hoverCol), SPHERE_RAD*0.6f, 4, 6, {60,140,255,40});
    }

    // Empty slots
    for (int l = 0; l < SZ; l++)
        for (int r = 0; r < SZ; r++)
            for (int c = 0; c < SZ; c++)
                if (g.board.cells[l][r][c] == EMPTY)
                    DrawSphereWires(cellPos(l,r,c), SPHERE_RAD*0.3f, 4, 4, {60,60,80,30});

    // Win beam
    if (g.winLine.active) {
        float pulse = (sinf(GetTime()*5.0f)+1.0f)/2.0f;
        Color bc = (g.winner==PLAYER) ? Color{60,140,255,(unsigned char)(120+80*pulse)}
                                      : Color{255,70,70,(unsigned char)(120+80*pulse)};
        for (int i = 0; i < SZ-1; i++)
            DrawCylinderEx(g.winLine.positions[i], g.winLine.positions[i+1], 0.08f, 0.08f, 8, bc);
    }

    EndMode3D();

    // HUD
    DrawText("3D CONNECT FOUR", 20, 20, 30, WHITE);
    DrawText(game.statusMsg.c_str(), 20, 60, 24, game.statusColor);

    DrawCircle(g.screenW-180, 30, 10, {60,140,255,255});
    DrawText("You", g.screenW-160, 22, 20, WHITE);
    DrawCircle(g.screenW-180, 60, 10, {255,70,70,255});
    DrawText("AI (MCTS)", g.screenW-160, 52, 20, WHITE);

    DrawText("Arrows/Right-drag: orbit  |  Scroll: zoom  |  R: restart",
             20, g.screenH-30, 16, {150,150,150,255});

    if (g.state == AI_THINKING && !g.aiDone) {
        int dots = ((int)(GetTime()*3)) % 4;
        std::string t = "Thinking";
        for (int i = 0; i < dots; i++) t += ".";
        DrawText(t.c_str(), g.screenW/2-60, g.screenH/2, 30, {255,180,80,255});
    }

    EndDrawing();
}

// ─── Main ───────────────────────────────────────────────────────────────────
int main() {
    srand((unsigned)time(nullptr));

    game.screenW = 1200; game.screenH = 800;
    InitWindow(game.screenW, game.screenH, "3D Connect Four - vs MCTS AI");
    SetTargetFPS(60);

    game.camera = {};
    game.camera.target = {0, 3.0f, 0};
    game.camera.up = {0, 1.0f, 0};
    game.camera.fovy = 45.0f;
    game.camera.projection = CAMERA_PERSPECTIVE;
    game.updateCam();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(gameFrame, 60, 1);
#else
    while (!WindowShouldClose()) gameFrame();
#endif

    CloseWindow();
    return 0;
}
