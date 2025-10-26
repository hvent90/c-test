#include <stdio.h>
#include <dirent.h>      // For directory operations
#include <string.h>      // For string manipulation
#include <sys/stat.h>    // For file stat checks
#include <flecs.h>
#include <MacTypes.h>
#include <math.h>
#include <raylib.h>
#include "raymath.h"
#include "audio.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

float SmoothDamp(float current, float target, float smoothTime) {
    return current + (target - current) * smoothTime;
}

const int MAX_ENTITIES = 10000;

typedef enum {
    COLOR_PALETTE_0,
    COLOR_PALETTE_1,
    COLOR_PALETTE_2,
    COLOR_PALETTE_3,
    COLOR_PALETTE_4,
    COLOR_PALETTE_5,
    COLOR_PALETTE_6,
    COLOR_PALETTE_7,
    COLOR_PALETTE_8,
    COLOR_PALETTE_9,
    COLOR_PALETTE_10,
    COLOR_PALETTE_11,
    COLOR_PALETTE_12,
    COLOR_PALETTE_13,
    COLOR_PALETTE_14,
    COLOR_PALETTE_15,
    COLOR_BACKGROUND,
    COLOR_FOREGROUND,
    COLOR_CURSOR,
    COLOR_SELECTION
} ThemeColor;

// ECS
typedef struct {
    float health;
} Health;

ECS_COMPONENT_DECLARE(Health);

typedef struct {
    Vector2 velocity;
} Velocity;

ECS_COMPONENT_DECLARE(Velocity);

typedef struct {
    bool defaultValue;
} PlayerInput;

ECS_COMPONENT_DECLARE(PlayerInput);

typedef struct {
    bool directionSet;
} EnemyInput;

ECS_COMPONENT_DECLARE(EnemyInput);

typedef struct {
    Vector2 position;
    float radius;
    ThemeColor colorIndex;
} Renderable;

ECS_COMPONENT_DECLARE(Renderable);

typedef struct {
    float currentRadius;
    float targetRadius;
    float velocity;
    float damping;
    float stiffness;
} SpringAnimation;

ECS_COMPONENT_DECLARE(SpringAnimation);

typedef struct {
    float range;
    float currentRange;
    float targetRange;
    float velocity;
    float damping;
    float stiffness;
    ThemeColor colorIndex;
    bool active;
    bool wasActive;
} AttractionRangeVFX;

ECS_COMPONENT_DECLARE(AttractionRangeVFX);

typedef struct {
    bool unused;
} Spike;

ECS_COMPONENT_DECLARE(Spike);

typedef struct {
    bool unused;
} Mortal;

ECS_COMPONENT_DECLARE(Mortal);

typedef enum {
    REPEL, ATTRACT,
    PHYSICS_COUNT
} Physics;

typedef struct {
    Physics physics;
    float zoom;
    float targetZoom;
} GameState;

ECS_COMPONENT_DECLARE(GameState);

// UI
typedef struct {
    char name[64];
    Color palette[16];
    Color background;
    Color foreground;
    Color cursor_color;
    Color selection_background;
} Theme;

Color HexToColor(const char *hex) {
    int r, g, b;
    if (hex[0] == '#') {
        if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return (Color){r, g, b, 255};
        }
    } else {
        if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
            return (Color){r, g, b, 255};
        }
    }

    return BLACK;
}

#define MAX_THEMES 100
Theme themes[MAX_THEMES];
int themeCount = 0;
int currentThemeIndex = 0;

void ApplyTheme(int themeIndex) {
    if (themeIndex >= 0) {
        currentThemeIndex = themeIndex % themeCount;
        printf("Applied theme: %s\n", themes[themeIndex].name);
    }
}

int ParseThemeFile(const char *filePath, Theme *theme) {
    FILE *file = fopen(filePath, "r");
    if (!file) return 0;

    *theme = (Theme){
        .name = "",
        .palette = {
            BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, LIME, WHITE,
            GRAY, PINK, LIME, GOLD, SKYBLUE, PURPLE, GREEN, WHITE
        },
        .background = BLACK,
        .foreground = WHITE,
        .cursor_color = WHITE,
        .selection_background = GRAY
    };

    // Extract theme from the filename
    const char *fileName = strrchr(filePath, '/');
    if (fileName) {
        fileName++; // skip the '/'
    } else {
        fileName = filePath;
    }
    strncpy(theme->name, fileName, sizeof(theme->name) - 1);
    theme->name[sizeof(theme->name) - 1] = '\0';

    // Read the file line by line
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';

        // skip empty lines and comments
        if (strlen(line) == 0 || line[0] == '#') {
            continue;
        }

        // Parse the line
        char key[64], value[64];
        if (sscanf(line, "%63s = %63s", key, value) == 2) {
            // Process key-value pairs
            if (strcmp(key, "palette") == 0) {
                int index;
                char hexColor[8];
                if (sscanf(value, "%d=#%7s", &index, hexColor) == 2) {
                    if (index >= 0 && index < 16) {
                        theme->palette[index] = HexToColor(hexColor);
                    }
                }
            } else if (strcmp(key, "background") == 0) {
                theme->background = HexToColor(value);
            } else if (strcmp(key, "foreground") == 0) {
                theme->foreground = HexToColor(value);
            } else if (strcmp(key, "cursor-color") == 0) {
                theme->cursor_color = HexToColor(value);
            } else if (strcmp(key, "selection-background") == 0) {
                theme->selection_background = HexToColor(value);
            }
        }
    }

    fclose(file);
    return 1;
}

int IsRegularFile(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

int ScanThemeDirectory(const char *dirPath, Theme *themes, int maxThemes) {
    // Open directory
    struct dirent *entry;
    int themeCount = 0;

    DIR *dir = opendir(dirPath);
    if (dir == NULL) {
        printf("Error: Cannot open directory %s\n", dirPath);
        return 0;
    }

    // Read each entry (skip . and ..)
    while ((entry = readdir(dir)) != NULL) {
        if (themeCount >= maxThemes) break;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // For each file, construct a full path
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

        // Check if it's a regular file
        if (IsRegularFile(fullPath)) {
            // Parse theme file
            Theme theme;
            if (ParseThemeFile(fullPath, &theme)) {
                themes[themeCount] = theme;
                themeCount++;
                // printf("Loaded theme: %s\n", theme.name);
            } else {
                printf("Failed to load theme: %s\n", entry->d_name);
            }
        }
    }

    closedir(dir);
    return themeCount;
}

void TriggerDestruction(ecs_world_t *world, ecs_entity_t entity) {
    // Add spring animation to shrink the entity to 0
    const Renderable *r = ecs_get(world, entity, Renderable);
    if (r) {
        ecs_set(world, entity, SpringAnimation, {
                .currentRadius = r->radius,
                .targetRadius = 0,
                .velocity = 0,
                .damping = 0.00000001f,
                .stiffness = 1000.0f
                });
    }

    // PlayDesctructionSound();
}

void PlayerMovementSystem(ecs_iter_t *it) {
    Velocity *v = ecs_field(it, Velocity, 0);

    float speed = 0.0f;

    for (int i = 0; i < it->count; i++) {
        const float PLAYER_SPEED = 200.0f;
        const float ACCELERATION = 2.0f; // Higher = faster acceleration
        Vector2 direction = {0, 0};

        if (IsKeyDown(KEY_RIGHT)) direction.x = 1.0f;
        if (IsKeyDown(KEY_LEFT)) direction.x = -1.0f;
        if (IsKeyDown(KEY_UP)) direction.y = -1.0f;
        if (IsKeyDown(KEY_DOWN)) direction.y = 1.0f;

        // Calculate target velocity
        Vector2 targetVelocity;
        float magnitude = Vector2Length(direction);
        if (magnitude > 0) {
            direction = Vector2Scale(direction, 1.0f / magnitude);
            targetVelocity = Vector2Scale(direction, PLAYER_SPEED);
        } else {
            targetVelocity = (Vector2){0, 0};
        }

        // Smoothly interpolate current velocity towards target
        v[i].velocity.x += (targetVelocity.x - v[i].velocity.x) * ACCELERATION * GetFrameTime();
        v[i].velocity.y += (targetVelocity.y - v[i].velocity.y) * ACCELERATION * GetFrameTime();

        speed = Vector2Length(v[i].velocity);
    }

    GameState *state = ecs_singleton_get_mut(it->world, GameState);
    state->targetZoom = 1.0f - speed * 0.001f;
    state->zoom = state->targetZoom;
    // float smoothSpeed = 10.0f; // Higher = faster transition
    // state->zoom += (state->targetZoom - state->zoom) * smoothSpeed * GetFrameTime();
}

void EnemyMovementSystem(ecs_iter_t *it) {
    Velocity *v = ecs_field(it, Velocity, 0);
    EnemyInput *e = ecs_field(it, EnemyInput, 1);
    const Renderable *r = ecs_field(it, Renderable, 2);

    const ecs_world_t *world = it->world;
    const ecs_entity_t player = ecs_lookup(world, "Player");
    const ecs_id_t renderable_id = ecs_field_id(it, 2);
    const Renderable *playerRenderable = ecs_get_id(world, player, renderable_id);
    const Vector2 playerPos = playerRenderable->position;

    const float MAX_ATTRACTION_RANGE = (float) MIN(GetScreenWidth(), GetScreenHeight()) / 2;

    for (int i = 0; i < it->count; i++) {
        const float ENEMY_SPEED = 100.0f;
        if (!e[i].directionSet && ENEMY_SPEED > 0) {
            e[i].directionSet = true;
            v[i].velocity = (Vector2){
                .x = (float) GetRandomValue(-(int) ENEMY_SPEED, (int) ENEMY_SPEED),
                .y = (float) GetRandomValue(-(int) ENEMY_SPEED, (int) ENEMY_SPEED)
            };
        }

        if (IsKeyDown(KEY_SPACE)) {
            const float MAX_ATTRACTION_FORCE = 5.0f;
            Vector2 dir = {
                .x = playerPos.x - r[i].position.x,
                .y = playerPos.y - r[i].position.y
            };

            const float magnitude = sqrtf(dir.x * dir.x + dir.y * dir.y);

            // Normalize direction
            if (magnitude > 0) {
                dir.x /= magnitude;
                dir.y /= magnitude;
            }

            float attractionStrength = fmaxf(0, 1.0f - (magnitude / MAX_ATTRACTION_RANGE));
            Vector2 attractionForce = (Vector2){
                dir.x * MAX_ATTRACTION_FORCE * attractionStrength,
                dir.y * MAX_ATTRACTION_FORCE * attractionStrength
            };
            v[i].velocity = Vector2Add(v[i].velocity, attractionForce);
        }
    }
}

void GlobalPositionUpdateSystem(ecs_iter_t *it) {
    GameState *state = ecs_singleton_get(it->world, GameState);

    ecs_query_t *q = ecs_query(it->world, {
                               .terms = {
                               { ecs_id(Renderable) }, { ecs_id(Velocity) },
                               }
                               });

    // Temporary storage for all entities
    typedef struct {
        ecs_entity_t entity;
        Renderable *renderable;
        Velocity *velocity;
    } EntityRef;

    EntityRef entities[MAX_ENTITIES];
    int entityCount = 0;
    int tableCount = 0;

    // Collect all entities from all tables
    ecs_iter_t query_it = ecs_query_iter(it->world, q);
    while (ecs_query_next(&query_it)) {
        tableCount++;
        Renderable *r = ecs_field(&query_it, Renderable, 0);
        Velocity *v = ecs_field(&query_it, Velocity, 1);

        for (int i = 0; i < query_it.count && entityCount < MAX_ENTITIES; i++) {
            entities[entityCount].entity = query_it.entities[i];
            entities[entityCount].renderable = &r[i];
            entities[entityCount].velocity = &v[i];
            entityCount++;
        }
    }

    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    // Calculate zoom-adjusted world boundaries
    const Vector2 screenCenter = {screenWidth / 2.0f, screenHeight / 2.0f};
    const float zoom = state->zoom;
    const float worldMinX = screenCenter.x - screenCenter.x / zoom;
    const float worldMaxX = screenCenter.x + screenCenter.x / zoom;
    const float worldMinY = screenCenter.y - screenCenter.y / zoom;
    const float worldMaxY = screenCenter.y + screenCenter.y / zoom;

    // Update positions and handle collisions across all tables
    for (int i = 0; i < entityCount; i++) {
        entities[i].renderable->position.x += entities[i].velocity->velocity.x *
                GetFrameTime();
        entities[i].renderable->position.y += entities[i].velocity->velocity.y *
                GetFrameTime();

        // Check collisions with all other entities (cross-table)
        for (int j = i + 1; j < entityCount; j++) {
            Vector2 dir = {
                .x = entities[j].renderable->position.x - entities[i].renderable->position.x,
                .y = entities[j].renderable->position.y - entities[i].renderable->position.y
            };

            const float magnitude = sqrtf(dir.x * dir.x + dir.y * dir.y);
            const float boundary = entities[j].renderable->radius + entities[i].renderable->radius;
            if (magnitude > boundary) {
                continue;
            }

            // Check for Spike
            const Spike *spike_i = ecs_get(it->world, entities[i].entity, Spike);
            const Spike *spike_j = ecs_get(it->world, entities[j].entity, Spike);
            const Mortal *mortal_i = ecs_get(it->world, entities[i].entity, Mortal);
            const Mortal *mortal_j = ecs_get(it->world, entities[j].entity, Mortal);

            // Destroy both
            if (spike_i && mortal_i && spike_j && mortal_j) {
                TriggerDestruction(it->world, entities[i].entity);
                TriggerDestruction(it->world, entities[j].entity);
                continue;
            }

            if (spike_i && mortal_j) {
                TriggerDestruction(it->world, entities[j].entity);
                continue;
            }

            if (spike_j && mortal_i) {
                TriggerDestruction(it->world, entities[i].entity);
                continue;
            }

            // Normalize direction
            if (magnitude > 0) {
                dir.x /= magnitude;
                dir.y /= magnitude;
            }

            // Adjust positions
            const float adjustment = (boundary - magnitude) * 0.5f;
            entities[i].renderable->position.x -= dir.x * adjustment;
            entities[i].renderable->position.y -= dir.y * adjustment;
            entities[j].renderable->position.x += dir.x * adjustment;
            entities[j].renderable->position.y += dir.y * adjustment;

            // Adjust velocity
            // Calculate relative velocity
            Vector2 relativeVelocity = {
                entities[j].velocity->velocity.x - entities[i].velocity->velocity.x,
                entities[j].velocity->velocity.y - entities[i].velocity->velocity.y
            };

            // Calculate velocity along the collision normal (dir)
            float velocityAlongNormal = relativeVelocity.x * dir.x + relativeVelocity.y * dir.y;

            // Only resolve if entities are moving toward each other
            if (state->physics == ATTRACT ? velocityAlongNormal > 0 : velocityAlongNormal < 0) {
                // Restitution (bounciness): 0 = no bounce, 1 = perfect bounce
                const float restitution = 0.9f;

                // Calculate impulse scalar
                float impulseMagnitude = -(1 + restitution) * velocityAlongNormal;
                impulseMagnitude *= 0.5f; // Divide by 2 because both entities have equal "mass"

                // Apply impulse to both entities
                entities[i].velocity->velocity.x -= dir.x * impulseMagnitude;
                entities[i].velocity->velocity.y -= dir.y * impulseMagnitude;

                entities[j].velocity->velocity.x += dir.x * impulseMagnitude;
                entities[j].velocity->velocity.y += dir.y * impulseMagnitude;
            }
        }

        // World boundary collision (zoom-adjusted)
        if (entities[i].renderable->position.x < worldMinX + entities[i].renderable->radius ||
            entities[i].renderable->position.x > worldMaxX - entities[i].renderable->radius) {
            entities[i].velocity->velocity.x *= -1;
            entities[i].renderable->position.x = CLAMP(entities[i].renderable->position.x,
                                                       worldMinX + entities[i].renderable->radius,
                                                       worldMaxX - entities[i].renderable->radius);
            PlayBounceSoundWithVelocity(entities[i].velocity->velocity.x);
        }

        if (entities[i].renderable->position.y < worldMinY + entities[i].renderable->radius ||
            entities[i].renderable->position.y > worldMaxY - entities[i].renderable->radius) {
            entities[i].velocity->velocity.y *= -1;
            entities[i].renderable->position.y = CLAMP(entities[i].renderable->position.y,
                                                       worldMinY + entities[i].renderable->radius,
                                                       worldMaxY - entities[i].renderable->radius);
            PlayBounceSoundWithVelocity(entities[i].velocity->velocity.y);
        }
    }

    ecs_query_fini(q);
}

void RenderSystem(ecs_iter_t *it) {
    const Renderable *r = ecs_field(it, Renderable, 0);
    Theme *theme = &themes[currentThemeIndex];

    GameState *state = ecs_singleton_get(it->world, GameState);

    for (int i = 0; i < it->count; i++) {
        Color color;
        switch (r[i].colorIndex) {
            case COLOR_BACKGROUND: color = theme->background;
                break;
            case COLOR_FOREGROUND: color = theme->foreground;
                break;
            case COLOR_CURSOR: color = theme->cursor_color;
                break;
            case COLOR_SELECTION: color = theme->selection_background;
                break;
            default:
                if (r[i].colorIndex >= COLOR_PALETTE_0 && r[i].colorIndex <= COLOR_PALETTE_15) {
                    color = theme->palette[r[i].colorIndex];
                } else {
                    color = WHITE;
                }
                break;
        }
        Vector2 screenCenter = {
            GetScreenWidth() / 2.0f, GetScreenHeight() /
                                     2.0f
        };
        Vector2 offset = Vector2Subtract(r[i].position, screenCenter);
        Vector2 scaledOffset = Vector2Scale(offset, state->zoom);
        Vector2 scaledPosition = Vector2Add(screenCenter, scaledOffset);

        DrawCircleV(scaledPosition, r[i].radius * state->zoom, color);
    }
}

void SpringAnimationSystem(ecs_iter_t *it) {
    SpringAnimation *spring = ecs_field(it, SpringAnimation, 0);
    Renderable *renderable = ecs_field(it, Renderable, 1);

    for (int i = 0; i < it->count; i++) {
        // Spring physics
        float force = (spring[i].targetRadius - spring[i].currentRadius) * spring[i].stiffness;
        spring[i].velocity += force * GetFrameTime();
        spring[i].velocity *= powf(spring[i].damping, GetFrameTime());
        spring[i].currentRadius += spring[i].velocity * GetFrameTime();

        // Update renderable radius
        renderable[i].radius = spring[i].currentRadius;

        // Remove component when animation is complete
        if (fabsf(spring[i].currentRadius - spring[i].targetRadius) < 0.1f &&
            fabsf(spring[i].velocity) < 0.1f) {
            renderable[i].radius = spring[i].targetRadius;

            if (spring[i].targetRadius <= 0.0f) {
                ecs_delete(it->world, it->entities[i]);
            } else {
                ecs_remove_id(it->world, it->entities[i], ecs_id(SpringAnimation));
            }
        }
    }
}

void AttractionRangeVFXSystem(ecs_iter_t *it) {
    AttractionRangeVFX *vfx = ecs_field(it, AttractionRangeVFX, 0);
    const Renderable *renderable = ecs_field(it, Renderable, 1);
    Theme *theme = &themes[currentThemeIndex];

    for (int i = 0; i < it->count; i++) {
        bool isSpacePressed = IsKeyDown(KEY_SPACE);

        // Handle activation/deactivation
        if (isSpacePressed && !vfx[i].wasActive) {
            // Just activated - set target to full range
            vfx[i].targetRange = vfx[i].range;
        } else if (!isSpacePressed && vfx[i].wasActive) {
            // Just deactivated - animate back to 0
            vfx[i].targetRange = 0;
        }

        // Update ease-out animation if active or animating
        if (vfx[i].active || vfx[i].currentRange > 0.1f) {
            // Ease-out animation
            float diff = vfx[i].targetRange - vfx[i].currentRange;
            vfx[i].currentRange += diff * 0.15f; // Ease-out factor

            // Stop very small movements
            if (fabsf(diff) < 0.5f) {
                vfx[i].currentRange = vfx[i].targetRange;
            }

            // Draw attraction range circle with transparency
            if (vfx[i].currentRange > 0.1f) {
                Color vfxColor;
                switch (vfx[i].colorIndex) {
                    case COLOR_BACKGROUND: vfxColor = theme->background;
                        break;
                    case COLOR_FOREGROUND: vfxColor = theme->foreground;
                        break;
                    case COLOR_CURSOR: vfxColor = theme->cursor_color;
                        break;
                    case COLOR_SELECTION: vfxColor = theme->selection_background;
                        break;
                    default:
                        if (vfx[i].colorIndex >= COLOR_PALETTE_0 && vfx[i].colorIndex <= COLOR_PALETTE_15) {
                            vfxColor = theme->palette[vfx[i].colorIndex];
                        } else {
                            vfxColor = WHITE;
                        }
                        break;
                }
                vfxColor.a = 50; // Set transparency
                DrawCircleLines(renderable[i].position.x, renderable[i].position.y, vfx[i].currentRange, vfxColor);

                // Draw filled circle with even more transparency
                vfxColor.a = 20;
                DrawCircle(renderable[i].position.x, renderable[i].position.y, vfx[i].currentRange, vfxColor);
            }
        }

        vfx[i].wasActive = isSpacePressed;
    }
}

void DrawBackgroundGrid(ecs_world_t *world) {
    GameState *state = ecs_singleton_get(world, GameState);
    const float zoom = state->zoom;
    const Vector2 screenCenter = {
        .x = GetScreenWidth() * 0.5f,
        .y = GetScreenHeight() * 0.5f
    };
    Theme *theme = &themes[currentThemeIndex];

    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    float gridSpacing = 50.0f; // Base grid spacing

    Color gridColor = theme->foreground;
    gridColor.a = 30;

    // Calculate how many grid lines we need based on unscaled spacing
    int countX = (int)(screenWidth / (gridSpacing * zoom)) + 2;
    int countY = (int)(screenHeight / (gridSpacing * zoom)) + 2;

    // Draw grid dots
    for (int i = -countX/2; i <= countX/2; i++) {
        for (int j = -countY/2; j <= countY/2; j++) {
            // Offset in world space (not screen space)
            Vector2 offset = {i * gridSpacing, j * gridSpacing};

            // Scale the offset and add to screen center
            Vector2 scaledOffset = Vector2Scale(offset, zoom);
            Vector2 screenPos = Vector2Add(screenCenter, scaledOffset);

            // Only draw if on screen
            if (screenPos.x >= 0 && screenPos.x < screenWidth &&
                screenPos.y >= 0 && screenPos.y < screenHeight) {
                DrawCircle((int)screenPos.x, (int)screenPos.y, 2.0f, gridColor);
            }
        }
    }
}

void DrawUI(ecs_world_t *world) {
    Theme *theme = &themes[currentThemeIndex];

    const int fontSize = 20;
    const int margin = 10;
    int y = margin;

    // Draw title
    DrawText(theme->name, margin, y, fontSize, theme->foreground);

    // Draw Physics
    GameState *state = ecs_singleton_get(world, GameState);
    DrawText(state->physics == REPEL ? "Repel" : "Attract", margin, y + 20, fontSize, theme->foreground);

    // Enemy count
    int enemyCount = ecs_count(world, EnemyInput);
    const char *enemyText = TextFormat("%d", enemyCount);
    DrawText(enemyText, margin, y + 40, fontSize, theme->foreground);

    // Draw FPS
    static int fps = 0;
    static float fpsTimer = 0.0f;
    static int frameCount = 0;
    const float deltaTime = GetFrameTime();
    frameCount++;
    fpsTimer += deltaTime;
    if (fpsTimer >= 1.0f) {
        fps = frameCount;
        frameCount = 0;
        fpsTimer = 0.0f;
    }

    const char *fpsText = TextFormat("FPS: %d", (int) fps);
    int fpsWidth = MeasureText(fpsText, fontSize);
    DrawText(fpsText, GetScreenWidth() - fpsWidth - margin, y, fontSize, theme->foreground);
}

void SpawnEnemy(ecs_world_t *world, Vector2 position) {
    const ecs_entity_t enemy = ecs_new(world);

    const float targetRadius = 15;

    ecs_set(world, enemy, Health, {.health = 100});
    ecs_set(world, enemy, Renderable, {.position = position, .radius = targetRadius * 0.3f,
            .colorIndex = COLOR_PALETTE_2});
    ecs_set(world, enemy, Velocity, {.velocity ={0, 0}});
    ecs_set(world, enemy, EnemyInput, {.directionSet = false});
    ecs_set(world, enemy, SpringAnimation, {
            .currentRadius = targetRadius * 0.3f,
            .targetRadius = targetRadius,
            .velocity = 0,
            .damping = 0.00001f,
            .stiffness = 700.0f
            });
    ecs_set(world, enemy, Mortal, {});

    PlayEnemySpawnSound();
}

void HandleInput(ecs_world_t *world) {
    if (IsKeyPressed(KEY_TAB)) {
        ApplyTheme(currentThemeIndex + 1);
    }

    if (IsKeyDown(KEY_E)) {
        Vector2 spawnPos = {
            (float) GetRandomValue(50, GetScreenWidth() - 50),
            (float) GetRandomValue(50, GetScreenHeight() - 50)
        };
        SpawnEnemy(world, spawnPos);
    }

    if (IsKeyPressed(KEY_F)) {
        GameState *state = ecs_singleton_get_mut(world, GameState);
        state->physics = (state->physics + 1) % PHYSICS_COUNT;
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI); // Enable high DPI support
    InitWindow(1280, 720, "raylib window");
    SetTargetFPS(120);
    InitAudio();

    // Setup themes
    themeCount = ScanThemeDirectory(
        "/Applications/Ghostty.app/Contents/Resources/ghostty/themes",
        themes,
        MAX_THEMES
    );

    printf("Loaded %d themes\n", themeCount);

    if (themeCount > 0) {
        ApplyTheme(0);
    }

    ecs_world_t *world = ecs_init();

    ECS_COMPONENT_DEFINE(world, Health);
    ECS_COMPONENT_DEFINE(world, Velocity);
    ECS_COMPONENT_DEFINE(world, PlayerInput);
    ECS_COMPONENT_DEFINE(world, EnemyInput);
    ECS_COMPONENT_DEFINE(world, Renderable);
    ECS_COMPONENT_DEFINE(world, SpringAnimation);
    ECS_COMPONENT_DEFINE(world, AttractionRangeVFX);
    ECS_COMPONENT_DEFINE(world, Spike);
    ECS_COMPONENT_DEFINE(world, Mortal);
    ECS_COMPONENT_DEFINE(world, GameState);

    ecs_singleton_set(world, GameState, {
                      .physics = REPEL,
                      .zoom = 1.0f,
                      .targetZoom = 1.0f
                      });

    ECS_SYSTEM(world, PlayerMovementSystem, EcsOnUpdate, Velocity, PlayerInput);
    ECS_SYSTEM(world, EnemyMovementSystem, EcsOnUpdate, Velocity, EnemyInput, Renderable);
    ECS_SYSTEM(world, GlobalPositionUpdateSystem, EcsOnUpdate);
    ECS_SYSTEM(world, SpringAnimationSystem, EcsOnUpdate, SpringAnimation, Renderable);
    ECS_SYSTEM(world, AttractionRangeVFXSystem, EcsOnUpdate, AttractionRangeVFX, Renderable);
    ECS_SYSTEM(world, RenderSystem, EcsOnUpdate, Renderable);

    ecs_entity_t player = ecs_new(world);
    ecs_set_name(world, player, "Player"); // {}
    ecs_set(world, player, Health, {.health = 100});
    ecs_set(world, player, Renderable, {
        .position = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f},
        .radius = 20,
        .colorIndex = COLOR_FOREGROUND
    });
    ecs_set(world, player, Velocity, {.velocity ={0, 0}});
    ecs_set(world, player, PlayerInput, {});
    ecs_set(world, player, AttractionRangeVFX, {
            .range = (float)MIN(GetScreenWidth(), GetScreenHeight()) / 2, // Match MAX_ATTRACTION_RANGE from EnemyMovementSystem
            .currentRange = 0,
            .targetRange = 0,
            .velocity = 0,
            .damping = 0.95f,
            .stiffness = 0.01f,
            .colorIndex = COLOR_PALETTE_4,
            .active = true,
            .wasActive = false
            });
    ecs_set(world, player, Spike, {});

    while (!WindowShouldClose()) {
        BeginDrawing();

        Theme *theme = &themes[currentThemeIndex];
        ClearBackground(theme->background);

        DrawBackgroundGrid(world);

        HandleInput(world);

        ecs_progress(world, GetFrameTime());

        DrawUI(world);

        EndDrawing();
    }

    ecs_fini(world);
    CleanupAudio();
    CloseWindow();

    return 0;
}
