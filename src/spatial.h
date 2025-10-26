#ifndef SPATIAL_H
#define SPATIAL_H

#include <stdbool.h>
#include <raylib.h>

// Maximum entities stored in array per node before subdivision
#define QUADTREE_NODE_CAPACITY 16

// Maximum depth of the quadtree
#define QUADTREE_MAX_DEPTH 8

// Axis-Aligned Bounding Box
typedef struct {
    float x_min;
    float y_min;
    float x_max;
    float y_max;
} AABB;

// Entity reference for spatial queries
typedef struct {
    int index;       // Index into the entity array
    AABB bounds;     // Cached bounding box
} SpatialEntity;

// Quadtree node (recursive structure)
typedef struct QuadNode {
    AABB bounds;                           // Node's spatial boundaries
    struct QuadNode* children[4];          // NW, NE, SW, SE children (NULL if leaf)
    SpatialEntity entities[QUADTREE_NODE_CAPACITY]; // Entities in this node
    int entity_count;                      // Number of entities in this node
    int depth;                             // Depth in the tree (root = 0)
    bool is_leaf;                          // True if this node has no children
} QuadNode;

// Quadtree spatial partitioning structure
typedef struct {
    QuadNode* root;
    AABB world_bounds;
    int total_entities;
    int node_count;
    int max_depth_reached;
} Quadtree;

// Callback function for querying entities
// Called for each entity found in query
// Parameters: entity_index, user_data
typedef void (*QueryCallback)(int entity_index, void* user_data);

// === Lifecycle ===

// Create a new quadtree with the given world bounds
Quadtree* quadtree_create(AABB world_bounds);

// Destroy the quadtree and free all memory
void quadtree_destroy(Quadtree* tree);

// Clear all entities from the quadtree (keeps structure for reuse)
void quadtree_clear(Quadtree* tree);

// === Insertion ===

// Insert an entity with its bounding box into the quadtree
// entity_index: Index of the entity in your game's entity array
// bounds: The AABB of the entity
void quadtree_insert(Quadtree* tree, int entity_index, AABB bounds);

// === Queries ===

// Query all entities that intersect with the given AABB
// Returns the number of entities found
// results: Array to store entity indices (must be pre-allocated)
// max_results: Maximum number of results to return
int quadtree_query(Quadtree* tree, AABB query_bounds, int* results, int max_results);

// Query with callback (more flexible, avoids allocation)
void quadtree_query_callback(Quadtree* tree, AABB query_bounds, QueryCallback callback, void* user_data);

// === Utilities ===

// Create an AABB from a circle (position + radius)
AABB aabb_from_circle(Vector2 position, float radius);

// Check if two AABBs intersect
bool aabb_intersects(AABB a, AABB b);

// Check if an AABB contains a point
bool aabb_contains_point(AABB box, Vector2 point);

// === Debug Visualization ===

// Draw the quadtree structure (for debugging)
// screen_center: The screen center point (for proper zoom rendering)
// zoom: The current zoom level
void quadtree_debug_draw(Quadtree* tree, Vector2 screen_center, float zoom);

#endif // SPATIAL_H
