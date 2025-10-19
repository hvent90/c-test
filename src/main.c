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
	Color color;
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
	Color color;
	bool active;
	bool wasActive;
} AttractionRangeVFX;
ECS_COMPONENT_DECLARE(AttractionRangeVFX);

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
	if (hex[0] != '#') return BLACK;

	int r, g, b;
	if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
		return (Color){r, g, b, 255};
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
				printf("Loaded theme: %s\n", theme.name);
			} else {
				printf("Failed to load theme: %s\n", entry->d_name);
			}
		}
	}

	closedir(dir);
	return themeCount;
}

void PlayerMovementSystem(ecs_iter_t *it) {
	Velocity *v = ecs_field(it, Velocity, 0);

	const float PLAYER_SPEED = 200.0f * GetFrameTime();

	for (int i = 0; i < it->count; i++) {
		v[i].velocity = (Vector2){0, 0};

		if (IsKeyDown(KEY_RIGHT)) v[i].velocity.x = PLAYER_SPEED;
		if (IsKeyDown(KEY_LEFT)) v[i].velocity.x = -PLAYER_SPEED;
		if (IsKeyDown(KEY_UP)) v[i].velocity.y = -PLAYER_SPEED;
		if (IsKeyDown(KEY_DOWN)) v[i].velocity.y = PLAYER_SPEED;
	}
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

	const float ENEMY_SPEED = 100.0f * GetFrameTime();
	const float MAX_ATTRACTION_FORCE = 5.0f * GetFrameTime();
	const float MAX_ATTRACTION_RANGE = (float)MIN(GetScreenWidth(), GetScreenHeight()) / 2;

	for (int i = 0; i < it->count; i++) {
		if (!e[i].directionSet && ENEMY_SPEED > 0) {
			e[i].directionSet = true;
			v[i].velocity = (Vector2){
				.x = (float) GetRandomValue(-(int) ENEMY_SPEED, (int) ENEMY_SPEED),
				.y = (float) GetRandomValue(-(int) ENEMY_SPEED, (int) ENEMY_SPEED)
			};
		}

		if (IsKeyDown(KEY_SPACE)) {
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
			Vector2 attractionForce = (Vector2){dir.x * MAX_ATTRACTION_FORCE * attractionStrength,
											   dir.y * MAX_ATTRACTION_FORCE * attractionStrength};
			v[i].velocity = Vector2Add(v[i].velocity, attractionForce);
		}
	}
}

void PositionUpdateSystem(ecs_iter_t *it) {
	Renderable *r = ecs_field(it, Renderable, 0);
	Velocity *v = ecs_field(it, Velocity, 1);

	const int screenWidth = GetScreenWidth();
	const int screenHeight = GetScreenHeight();

	for (int i = 0; i < it->count; i++) {
		r[i].position.x += v[i].velocity.x;
		r[i].position.y += v[i].velocity.y;

		if (r[i].position.x < r[i].radius || (int) r[i].position.x > screenWidth - (int)r[i].radius) {
			v[i].velocity.x *= -1;
			r[i].position.x = CLAMP(r[i].position.x, r[i].radius, screenWidth - r[i].radius);
			PlayBounceSoundWithVelocity(v[i].velocity.x);
		}

		if (r[i].position.y < r[i].radius || (int) r[i].position.y > screenHeight - (int)r[i].radius) {
			v[i].velocity.y *= -1;
			r[i].position.y = CLAMP(r[i].position.y, r[i].radius, screenHeight - r[i].radius);
			PlayBounceSoundWithVelocity(v[i].velocity.y);
		}
	}
}

void RenderSystem(ecs_iter_t *it) {
	const Renderable *r = ecs_field(it, Renderable, 0);

	for (int i = 0; i < it->count; i++) {
		DrawCircleV(r[i].position, r[i].radius, r[i].color);
	}
}

void SpringAnimationSystem(ecs_iter_t *it) {
	SpringAnimation *spring = ecs_field(it, SpringAnimation, 0);
	Renderable *renderable = ecs_field(it, Renderable, 1);
	
	for (int i = 0; i < it->count; i++) {
		// Spring physics
		float force = (spring[i].targetRadius - spring[i].currentRadius) * spring[i].stiffness;
		spring[i].velocity += force;
		spring[i].velocity *= spring[i].damping;
		spring[i].currentRadius += spring[i].velocity;
		
		// Update renderable radius
		renderable[i].radius = spring[i].currentRadius;
		
		// Remove component when animation is complete
		if (fabsf(spring[i].currentRadius - spring[i].targetRadius) < 0.1f && 
			fabsf(spring[i].velocity) < 0.1f) {
			renderable[i].radius = spring[i].targetRadius;
			ecs_remove_id(it->world, it->entities[i], ecs_id(SpringAnimation));
		}
	}
}

void AttractionRangeVFXSystem(ecs_iter_t *it) {
	AttractionRangeVFX *vfx = ecs_field(it, AttractionRangeVFX, 0);
	const Renderable *renderable = ecs_field(it, Renderable, 1);
	
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
				Color vfxColor = vfx[i].color;
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

void DrawUI() {
	Theme *theme = &themes[currentThemeIndex];

	const int fontSize = 20;
	const int margin = 10;
	int y = margin;

	// Draw title
	DrawText(theme->name, margin, y, fontSize, theme->foreground);

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
	ecs_set(world, enemy, Renderable, {.position = position, .radius = targetRadius * 0.3f, .color = GREEN});
	ecs_set(world, enemy, Velocity, {.velocity ={0, 0}});
	ecs_set(world, enemy, EnemyInput, {.directionSet = false});
	ecs_set(world, enemy, SpringAnimation, {
		.currentRadius = targetRadius * 0.3f,
		.targetRadius = targetRadius,
		.velocity = 0,
		.damping = 0.8f,
		.stiffness = 0.2f
	});

	PlayEnemySpawnSound();
}

void HandleInput(ecs_world_t *world) {
	if (IsKeyPressed(KEY_TAB)) {
		ApplyTheme(currentThemeIndex + 1);
	}

	if (IsKeyPressed(KEY_E)) {
		Vector2 spawnPos = {
			(float)GetRandomValue(50, GetScreenWidth() - 50),
			(float)GetRandomValue(50, GetScreenHeight() - 50)
		};
		SpawnEnemy(world, spawnPos);
	}
}

int main(void) {
	SetConfigFlags(FLAG_WINDOW_HIGHDPI); // Enable high DPI support
	InitWindow(800, 450, "raylib window");
	SetTargetFPS(60);
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

	ECS_SYSTEM(world, PlayerMovementSystem, EcsOnUpdate, Velocity, PlayerInput);
	ECS_SYSTEM(world, EnemyMovementSystem, EcsOnUpdate, Velocity, EnemyInput, Renderable);
	ECS_SYSTEM(world, PositionUpdateSystem, EcsOnUpdate, Renderable, Velocity);
	ECS_SYSTEM(world, RenderSystem, EcsOnUpdate, Renderable);
	ECS_SYSTEM(world, SpringAnimationSystem, EcsOnUpdate, SpringAnimation, Renderable);
	ECS_SYSTEM(world, AttractionRangeVFXSystem, EcsOnUpdate, AttractionRangeVFX, Renderable);

	ecs_entity_t player = ecs_new(world);
	ecs_set_name(world, player, "Player");
	ecs_set(world, player, Health, {.health = 100});
	ecs_set(world, player, Renderable, {.position = {400, 225}, .radius = 20, .color = RED});
	ecs_set(world, player, Velocity, {.velocity ={0, 0}});
	ecs_set(world, player, PlayerInput, {});
	ecs_set(world, player, AttractionRangeVFX, {
		.range = (float)MIN(800, 450) / 2, // Match MAX_ATTRACTION_RANGE from EnemyMovementSystem
		.currentRange = 0,
		.targetRange = 0,
		.velocity = 0,
		.damping = 0.95f,
		.stiffness = 0.01f,
		.color = BLUE,
		.active = true,
		.wasActive = false
	});

	while (!WindowShouldClose()) {
		BeginDrawing();

		HandleInput(world);

		Theme *theme = &themes[currentThemeIndex];
		ClearBackground(theme->background);

		ecs_progress(world, GetFrameTime());

		DrawUI();

		EndDrawing();
	}

	ecs_fini(world);
	CleanupAudio();
	CloseWindow();

	return 0;
}