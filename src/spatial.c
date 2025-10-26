#include "spatial.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>

// === Internal Helper Functions ===

// Allocate a new quadtree node
static QuadNode* node_create(AABB bounds, int depth) {
    QuadNode* node = (QuadNode*)malloc(sizeof(QuadNode));
    if (!node) return NULL;

    node->bounds = bounds;
    node->entity_count = 0;
    node->depth = depth;
    node->is_leaf = true;

    for (int i = 0; i < 4; i++) {
        node->children[i] = NULL;
    }

    return node;
}

// Recursively destroy a node and all its children
static void node_destroy(QuadNode* node) {
    if (!node) return;

    for (int i = 0; i < 4; i++) {
        if (node->children[i]) {
            node_destroy(node->children[i]);
        }
    }

    free(node);
}

// Subdivide a node into 4 children (NW, NE, SW, SE)
static void node_subdivide(QuadNode* node) {
    if (!node->is_leaf) return; // Already subdivided

    float x_mid = (node->bounds.x_min + node->bounds.x_max) / 2.0f;
    float y_mid = (node->bounds.y_min + node->bounds.y_max) / 2.0f;

    // Create 4 children
    // [0] = NW (top-left)
    node->children[0] = node_create((AABB){
        node->bounds.x_min, node->bounds.y_min,
        x_mid, y_mid
    }, node->depth + 1);

    // [1] = NE (top-right)
    node->children[1] = node_create((AABB){
        x_mid, node->bounds.y_min,
        node->bounds.x_max, y_mid
    }, node->depth + 1);

    // [2] = SW (bottom-left)
    node->children[2] = node_create((AABB){
        node->bounds.x_min, y_mid,
        x_mid, node->bounds.y_max
    }, node->depth + 1);

    // [3] = SE (bottom-right)
    node->children[3] = node_create((AABB){
        x_mid, y_mid,
        node->bounds.x_max, node->bounds.y_max
    }, node->depth + 1);

    node->is_leaf = false;
}

// Insert an entity into a specific node (recursive)
static void node_insert(QuadNode* node, int entity_index, AABB bounds, int max_depth) {
    // If not a leaf, insert into appropriate child
    if (!node->is_leaf) {
        for (int i = 0; i < 4; i++) {
            if (node->children[i] && aabb_intersects(node->children[i]->bounds, bounds)) {
                node_insert(node->children[i], entity_index, bounds, max_depth);
            }
        }
        return;
    }

    // Add entity to this leaf node
    if (node->entity_count < QUADTREE_NODE_CAPACITY) {
        node->entities[node->entity_count].index = entity_index;
        node->entities[node->entity_count].bounds = bounds;
        node->entity_count++;
        return;
    }

    // Node is full and we haven't reached max depth - subdivide
    if (node->depth < max_depth) {
        node_subdivide(node);

        // Redistribute existing entities to children
        SpatialEntity temp_entities[QUADTREE_NODE_CAPACITY];
        memcpy(temp_entities, node->entities, sizeof(SpatialEntity) * node->entity_count);
        int temp_count = node->entity_count;
        node->entity_count = 0; // Clear parent

        for (int i = 0; i < temp_count; i++) {
            for (int j = 0; j < 4; j++) {
                if (aabb_intersects(node->children[j]->bounds, temp_entities[i].bounds)) {
                    node_insert(node->children[j], temp_entities[i].index, temp_entities[i].bounds, max_depth);
                }
            }
        }

        // Insert the new entity
        for (int i = 0; i < 4; i++) {
            if (node->children[i] && aabb_intersects(node->children[i]->bounds, bounds)) {
                node_insert(node->children[i], entity_index, bounds, max_depth);
            }
        }
    } else {
        // Max depth reached, force insert even if over capacity
        if (node->entity_count < QUADTREE_NODE_CAPACITY) {
            node->entities[node->entity_count].index = entity_index;
            node->entities[node->entity_count].bounds = bounds;
            node->entity_count++;
        }
    }
}

// Query entities in a node (recursive)
static void node_query(QuadNode* node, AABB query_bounds, int* results, int* result_count, int max_results) {
    if (!node || !aabb_intersects(node->bounds, query_bounds)) {
        return; // No intersection
    }

    // If leaf, check all entities in this node
    if (node->is_leaf) {
        for (int i = 0; i < node->entity_count; i++) {
            if (*result_count >= max_results) return;

            if (aabb_intersects(node->entities[i].bounds, query_bounds)) {
                results[*result_count] = node->entities[i].index;
                (*result_count)++;
            }
        }
        return;
    }

    // Recursively query children
    for (int i = 0; i < 4; i++) {
        if (node->children[i]) {
            node_query(node->children[i], query_bounds, results, result_count, max_results);
        }
    }
}

// Query with callback
static void node_query_callback(QuadNode* node, AABB query_bounds, QueryCallback callback, void* user_data) {
    if (!node || !aabb_intersects(node->bounds, query_bounds)) {
        return;
    }

    if (node->is_leaf) {
        for (int i = 0; i < node->entity_count; i++) {
            if (aabb_intersects(node->entities[i].bounds, query_bounds)) {
                callback(node->entities[i].index, user_data);
            }
        }
        return;
    }

    for (int i = 0; i < 4; i++) {
        if (node->children[i]) {
            node_query_callback(node->children[i], query_bounds, callback, user_data);
        }
    }
}

// Clear a node recursively
static void node_clear(QuadNode* node) {
    if (!node) return;

    node->entity_count = 0;

    for (int i = 0; i < 4; i++) {
        if (node->children[i]) {
            node_destroy(node->children[i]);
            node->children[i] = NULL;
        }
    }

    node->is_leaf = true;
}

// Count nodes recursively (for stats)
static int node_count_recursive(QuadNode* node) {
    if (!node) return 0;

    int count = 1;
    for (int i = 0; i < 4; i++) {
        count += node_count_recursive(node->children[i]);
    }
    return count;
}

// Get max depth recursively (for stats)
static int node_max_depth(QuadNode* node) {
    if (!node || node->is_leaf) {
        return node ? node->depth : 0;
    }

    int max = node->depth;
    for (int i = 0; i < 4; i++) {
        if (node->children[i]) {
            int child_depth = node_max_depth(node->children[i]);
            if (child_depth > max) max = child_depth;
        }
    }
    return max;
}

// === Public API Implementation ===

Quadtree* quadtree_create(AABB world_bounds) {
    Quadtree* tree = (Quadtree*)malloc(sizeof(Quadtree));
    if (!tree) return NULL;

    tree->root = node_create(world_bounds, 0);
    tree->world_bounds = world_bounds;
    tree->total_entities = 0;
    tree->node_count = 1;
    tree->max_depth_reached = 0;

    return tree;
}

void quadtree_destroy(Quadtree* tree) {
    if (!tree) return;

    node_destroy(tree->root);
    free(tree);
}

void quadtree_clear(Quadtree* tree) {
    if (!tree || !tree->root) return;

    node_clear(tree->root);
    tree->total_entities = 0;
    tree->node_count = 1;
    tree->max_depth_reached = 0;
}

void quadtree_insert(Quadtree* tree, int entity_index, AABB bounds) {
    if (!tree || !tree->root) return;

    node_insert(tree->root, entity_index, bounds, QUADTREE_MAX_DEPTH);
    tree->total_entities++;

    // Update stats
    tree->node_count = node_count_recursive(tree->root);
    tree->max_depth_reached = node_max_depth(tree->root);
}

int quadtree_query(Quadtree* tree, AABB query_bounds, int* results, int max_results) {
    if (!tree || !tree->root || !results) return 0;

    int result_count = 0;
    node_query(tree->root, query_bounds, results, &result_count, max_results);
    return result_count;
}

void quadtree_query_callback(Quadtree* tree, AABB query_bounds, QueryCallback callback, void* user_data) {
    if (!tree || !tree->root || !callback) return;

    node_query_callback(tree->root, query_bounds, callback, user_data);
}

// === Utility Functions ===

AABB aabb_from_circle(Vector2 position, float radius) {
    return (AABB){
        position.x - radius,
        position.y - radius,
        position.x + radius,
        position.y + radius
    };
}

bool aabb_intersects(AABB a, AABB b) {
    return !(a.x_max < b.x_min || a.x_min > b.x_max ||
             a.y_max < b.y_min || a.y_min > b.y_max);
}

bool aabb_contains_point(AABB box, Vector2 point) {
    return point.x >= box.x_min && point.x <= box.x_max &&
           point.y >= box.y_min && point.y <= box.y_max;
}

// === Debug Visualization ===

// Recursive drawing helper
static void node_debug_draw_recursive(QuadNode* node, Vector2 screen_center, float zoom) {
    if (!node) return;

    // Apply same transformation as entity rendering:
    // 1. Get offset from screen center for min corner
    Vector2 min_offset = {
        node->bounds.x_min - screen_center.x,
        node->bounds.y_min - screen_center.y
    };
    Vector2 scaled_min_offset = {min_offset.x * zoom, min_offset.y * zoom};
    Vector2 min_screen = {
        screen_center.x + scaled_min_offset.x,
        screen_center.y + scaled_min_offset.y
    };

    // 2. Calculate width/height and scale them
    float world_width = node->bounds.x_max - node->bounds.x_min;
    float world_height = node->bounds.y_max - node->bounds.y_min;
    float width = world_width * zoom;
    float height = world_height * zoom;

    float x = min_screen.x;
    float y = min_screen.y;

    // Color based on depth
    Color colors[] = {
        (Color){0, 255, 0, 100},    // Green (depth 0)
        (Color){0, 200, 255, 80},   // Cyan (depth 1)
        (Color){255, 255, 0, 60},   // Yellow (depth 2)
        (Color){255, 128, 0, 50},   // Orange (depth 3)
        (Color){255, 0, 0, 40},     // Red (depth 4+)
    };

    int color_index = node->depth < 5 ? node->depth : 4;
    Color color = colors[color_index];

    // Draw node bounds
    DrawRectangleLinesEx((Rectangle){x, y, width, height}, 1.0f, color);

    // Draw entity count if leaf
    if (node->is_leaf && node->entity_count > 0) {
        DrawText(TextFormat("%d", node->entity_count),
                 (int)(x + 2), (int)(y + 2), 10, WHITE);
    }

    // Recursively draw children
    for (int i = 0; i < 4; i++) {
        node_debug_draw_recursive(node->children[i], screen_center, zoom);
    }
}

void quadtree_debug_draw(Quadtree* tree, Vector2 screen_center, float zoom) {
    if (!tree || !tree->root) return;

    node_debug_draw_recursive(tree->root, screen_center, zoom);

    // Draw stats
    DrawText(TextFormat("Quadtree: %d nodes, %d entities, depth %d",
                        tree->node_count, tree->total_entities, tree->max_depth_reached),
             10, 120, 20, YELLOW);
}
