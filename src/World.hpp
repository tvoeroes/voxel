#pragma once

#define NEW_REGION_FORMAT

#include "MemoryBlock.hpp"
#include "RingBufferSingleProducerSingleConsumer.hpp"
#include "SparseMap.hpp"
#include "TinyAlgebra.hpp"
#include "Block.hpp"
#include "SphereIterator.hpp"
#include <string>
#include <vector>
#include <GL/gl3w.h>

// TODO: checkout other compression libraries that are faster
#include <zlib.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stack>
#include <queue>
#include "ModTable.hpp"
#include "ThreadBarrier.hpp"

// TODO: expand
// TODO: char instead of int position and type
struct Vertex { iVec3 position; int type; ucVec4 shaddow; };

struct Mesh { GLuint VAO; GLuint VBO; GLsizei size; };
struct UnusedBuffer { GLuint VAO; GLuint VBO; };
struct MeshMeta { iVec3 position; bool empty; };

#ifdef NEW_REGION_FORMAT
enum class CType { MEMORY, FILE, NOWHERE }; // also for determining what's in the union
//struct ChunkMeta { int size; CType loc; union { int offset_n; Bytef * location; }; };
struct ChunkMeta { int size; CType loc; struct { int offset_n; Bytef * location; }; }; // Unions could also work and would use less space, but i had few minor bugs with unions and will for now fix them by replacing union with struct
#else
struct ChunkMeta { int size; int offset; };
#endif

enum class Status : char { UNLOADED, LOADED, CHECKED };

struct Command
{
    enum class Type : int { REMOVE, UPLOAD };
    Type type;
    int index;
    iVec3 position;
    std::vector<Vertex> mesh;
};

struct MeshWPos { Mesh mesh; iVec3 position; };

enum class WorldType { SINE, SMALL_BLOCK, FLOOR, SIMPLEX_2D, EMPTY };

//==============================================================================
class World
{
public:
    World(); // TODO: refactor
    ~World(); // TODO: refactor

    void draw(const iVec3 new_center, const fVec4 frustum_planes[6]);

private:
    //==============================================================================
    // constants

    // only edit following line / no need to tinker with the rest
    static constexpr int
            RDISTANCE{ 15 },
            REDISTANCE{ RDISTANCE * 2 },
            CSIZE{ 16 },
            MSIZE{ 16 },
            MCSIZE{ (REDISTANCE * 2) + 1 + 8 }, // + any number
            MESH_BORDER_REQUIRED_SIZE{ 1 },
            MOFF{ CSIZE / 2 },
            CRSIZE{ ceilIntDiv(512, CSIZE) },
            MRSIZE{ ceilIntDiv(512, MSIZE) },
            CCSIZE{ ceilIntDiv(MSIZE + MESH_BORDER_REQUIRED_SIZE * 2, CSIZE) + 1 + 0 }, // + any number
            CRCSIZE{ ceilIntDiv((MSIZE * REDISTANCE + MESH_BORDER_REQUIRED_SIZE * 2), (CSIZE * CRSIZE)) + 2 + 0 }, // + any number
            MRCSIZE{ CRCSIZE };

    static_assert(CSIZE > 0 && MSIZE > 0 && MCSIZE > 0 && MESH_BORDER_REQUIRED_SIZE >= 0 && CRSIZE > 0 && CRCSIZE > 0, "Parameters must be positive.");
    static_assert((RDISTANCE * 2) + 1 <= MCSIZE, "Mesh container too small for the render distance.");
    static_assert((REDISTANCE * 2) + 1 <= MCSIZE, "Mesh container too small for the loaded distance.");
    static_assert(((MESH_BORDER_REQUIRED_SIZE * 2 + MSIZE) + (CSIZE - 1)) / CSIZE <= CCSIZE, "Chunk container size too small.");

    static constexpr iVec3 CHUNK_SIZES{ CSIZE, CSIZE, CSIZE };
    static constexpr iVec3 CHUNK_CONTAINER_SIZES{ CCSIZE, CCSIZE, CCSIZE };
    static constexpr iVec3 CHUNK_REGION_SIZES{ CRSIZE, CRSIZE, CRSIZE };
    static constexpr iVec3 CHUNK_REGION_CONTAINER_SIZES{ CRCSIZE, CRCSIZE, CRCSIZE };

    static constexpr iVec3 MESH_SIZES{ MSIZE, MSIZE, MSIZE };
    static constexpr iVec3 MESH_CONTAINER_SIZES{ MCSIZE, MCSIZE, MCSIZE };
    static constexpr iVec3 MESH_REGION_SIZES{ MRSIZE, MRSIZE, MRSIZE };
    static constexpr iVec3 MESH_REGION_CONTAINER_SIZES{ MRCSIZE, MRCSIZE, MRCSIZE };

    static constexpr iVec3 MESH_OFFSETS{ MOFF, MOFF, MOFF };

    static constexpr int CHUNK_SIZE{ product(CHUNK_SIZES) };
    static constexpr int CHUNK_CONTAINER_SIZE{ product(CHUNK_CONTAINER_SIZES) };
    static constexpr int MESH_CONTAINER_SIZE{ product(MESH_CONTAINER_SIZES) };

    static constexpr int SQUARE_RENDER_DISTANCE{ RDISTANCE * RDISTANCE };
    static constexpr int SQUARE_REMOVE_DISTANCE{ REDISTANCE * REDISTANCE };

    static constexpr char WORLD_ROOT[]{ "world/" };
    static constexpr char MESH_CACHE_ROOT[]{ "mesh_cache/" };

    static_assert(sizeof(Bytef) == sizeof(char), "Assuming that.");
    static constexpr int CHUNK_DATA_SIZE{ sizeof(Block) * CHUNK_SIZE };

    static constexpr int COMMAND_BUFFER_SIZE{ 128 };
    static constexpr int SLEEP_MS{ 300 };
    static constexpr int STALL_SLEEP_MS{ 50 };
    static constexpr int MAX_COMMANDS_PER_FRAME{ 16 }; // TODO: dynamic based on time left
    static constexpr unsigned char SHADDOW_STRENGTH{ 60 };

    static_assert(MAX_COMMANDS_PER_FRAME > 0, "Can't do anything without command execution.");

    static constexpr int MESH_CACHE_DATA_SIZE_FACTOR{ 4096 * 64 };
    static constexpr int REGION_DATA_SIZE_FACTOR{ CHUNK_DATA_SIZE * 128 };

    static constexpr int THREAD_COUNT{ 3 }; // locking issues. multi threads are not working, because of reallocating region data?

public:
    static_assert(CSIZE == 16 && MSIZE == 16 && MOFF == 8, "Temporary.");
    static constexpr iVec3 chunk_container_size{ 2, 2, 2 };
private:

    //==============================================================================
    // variables

    // loader thread data
    std::thread m_loader_thread;
    Block m_blocks[CHUNK_SIZE * CHUNK_CONTAINER_SIZE]; // TODO: use mod table

    struct ChunkStatus { iVec3 position; bool needs_save; };
    ModTable<ChunkStatus, int, CHUNK_CONTAINER_SIZES(0), CHUNK_CONTAINER_SIZES(1), CHUNK_CONTAINER_SIZES(2)> m_chunk_statuses;

    // TODO: more space efficient format than current (3 states only needed)
    ModTable<Status, int, MESH_CONTAINER_SIZES(0), MESH_CONTAINER_SIZES(1), MESH_CONTAINER_SIZES(2)> m_mesh_loaded;

    std::thread m_workers[THREAD_COUNT];
    SphereIterator<RDISTANCE, THREAD_COUNT> m_iterator;
    std::atomic_int m_iterator_index{ 0 };
    ThreadBarrier m_barrier{ THREAD_COUNT };
    std::mutex m_ring_buffer_lock; // TODO: my idea was to create a lock free system, but this might be okay

    // TODO: Maybe replace by array and size counter. Max possible size should be equal to MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z, but is overkill.
    std::vector<MeshMeta> m_loaded_meshes; // contains all loaded meshes
    std::mutex m_loaded_meshes_lock; // TODO: replace above vector with container that has a thread safe push operation (easy peasy)

    struct Region
    {
        iVec3 position;
        ModTable<ChunkMeta, int, CHUNK_REGION_SIZES(0), CHUNK_REGION_SIZES(1), CHUNK_REGION_SIZES(2)> metas;
#ifdef NEW_REGION_FORMAT
        // yes, use both
        MemoryBlock<> data_memory;
        Bytef * data; // TODO: replace pointer with RAII mechanism
#else
        Bytef * data; // TODO: replace pointer with RAII mechanism
#endif
        int size, container_size;
        std::mutex write_lock;
        bool needs_save;
    };
    ModTable<Region, int, CHUNK_REGION_CONTAINER_SIZES(0), CHUNK_REGION_CONTAINER_SIZES(1), CHUNK_REGION_CONTAINER_SIZES(2)> m_regions;

    struct MeshCache
    {
        enum class Status : char { UNKNOWN, EMPTY, NON_EMPTY }; // could be reduced to bitmap (2 bits per mesh)
        struct MeshCacheInfo { Status status; int vertex_count; int compressed_size; int offset; };

        iVec3 position;
        int size, container_size;
        bool needs_save;
        ModTable<MeshCacheInfo, int, MESH_REGION_SIZES(0), MESH_REGION_SIZES(1), MESH_REGION_SIZES(2)> infos;

        Bytef * data; // TODO: replace pointer with RAII mechanism
    };
    ModTable<MeshCache, int, MESH_REGION_CONTAINER_SIZES(0), MESH_REGION_CONTAINER_SIZES(1), MESH_REGION_CONTAINER_SIZES(2)> m_mesh_caches;

    // renderer thread data
    std::stack<UnusedBuffer> m_unused_buffers;
    //iVec3 m_reference_center;
    SparseMap<MeshWPos, MESH_CONTAINER_SIZE> m_meshes;

    // shared / synchronization data
    RingBufferSingleProducerSingleConsumer<Command, COMMAND_BUFFER_SIZE> m_commands;
    std::atomic<iVec3> m_center_mesh;
    std::atomic_bool m_quit;
    std::atomic_int m_exited_threads{ 0 };
    std::atomic_int m_waiting_threads{ 0 };
    std::atomic_bool m_moved_center_mesh;

    //==============================================================================
    // functions
    static bool meshInFrustum(const fVec4 planes[6], const iVec3 mesh_offset); // TODO: refactor

    // renderer functions
    void executeRendererCommands(const int max_command_count);

    // loader functions
    std::vector<Vertex> loadMesh(const iVec3 mesh_position);
    void exitLoaderThread();
    void loadChunkToChunkContainerOld(const iVec3 chunk_position);
    void loadChunkToChunkContainerNew(const iVec3 chunk_position, Block * const chunks, iVec3 * const chunk_meta);
    void saveRegionToDriveNew(const iVec3 region_position);
    void saveRegionToDriveOld(const iVec3 region_position);
    void saveMeshCacheToDrive(const iVec3 mesh_cache_position);
    void loadChunkRange(const iVec3 from_block, const iVec3 to_block);
    template<typename GetBlock>
    std::vector<Vertex> generateMesh(const iVec3 from_block, const iVec3 to_block, GetBlock blockGet);
    std::vector<Vertex> generateMeshNew(const iVec3 mesh_position, /*const iVec3 chunk_container_size,*/ Block * const chunks, iVec3 * const chunk_metas);
    std::vector<Vertex> generateMeshOld(const iVec3 from_block, const iVec3 to_block);
    class BlockGetter // this is temporary, to reduce boilerplate (duplicating generateMesh)
    {
    public:
        BlockGetter(World * w) : world{ w } {}
        Block & operator () (const iVec3 block_position) { return world->getBlock(block_position); }
    private:
        World * world;
    };

    void generateChunk(const iVec3 from_block, const iVec3 to_block, const WorldType world_type);
    void generateChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block, const WorldType world_type);
    MeshCache::Status getMeshStatus(const iVec3 mesh_position);
    Block & getBlock(const iVec3 block_position);
    void loadMeshCache(const iVec3 mesh_cache_position);
    void loadRegionOld(const iVec3 region_position);
    void loadRegionNew(const iVec3 region_position);
    void saveChunkToRegionOld(const iVec3 chunk_position);
    void saveChunkToRegionNew(const Block * const source, const iVec3 chunk_position);
    void saveMeshToMeshCache(const iVec3 mesh_position, const std::vector<Vertex> & mesh);
    bool removeOutOfRangeMeshes(const iVec3 center_mesh); // returns false if buffer is full and operation was not completed
    void meshLoader();
    void multiThreadMeshLoader(const int thread_id);

    void sineChunk(const iVec3 from_block, const iVec3 to_block);
    void simplex2DChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block);
    void emptyChunk(const iVec3 from_block, const iVec3 to_block);
    void sineChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block);
    void emptyChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block);
    void smallBlockChunk(const iVec3 from_block, const iVec3 to_block);
    void floorTilesChunk(const iVec3 from_block, const iVec3 to_block);

    // shared functions
    static bool inRange(const iVec3 center, const iVec3 position, const int max_square_distance);
    static unsigned char vertexAO(const bool side_a, const bool side_b, const bool corner);

};
