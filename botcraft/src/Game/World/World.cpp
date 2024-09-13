#include "botcraft/Game/AssetsManager.hpp"
#include "botcraft/Game/World/Chunk.hpp"
#include "botcraft/Game/World/World.hpp"

#include "botcraft/Utilities/Logger.hpp"

namespace Botcraft
{
    World::World(const bool is_shared_) : is_shared(is_shared_)
    {
#if PROTOCOL_VERSION < 719 /* < 1.16 */
        current_dimension = Dimension::None;
#else
        current_dimension = "";
#endif

#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
        world_interaction_sequence_id = 0;
#endif
    }

    World::~World()
    {

    }

    bool World::IsLoaded(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);

        const int chunk_x = static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH)));
        const int chunk_z = static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)));

        return terrain.find({ chunk_x, chunk_z }) != terrain.end();
    }

    bool World::IsShared() const
    {
        return is_shared;
    }

    int World::GetHeight() const
    {
#if PROTOCOL_VERSION < 757 /* < 1.18 */
        return 256;
#else
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        return GetHeightImpl();
#endif
    }

    int World::GetMinY() const
    {
#if PROTOCOL_VERSION < 757 /* < 1.18 */
        return 0;
#else
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        return GetMinYImpl();
#endif
    }

    bool World::IsInUltraWarmDimension() const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 719 /* < 1.16 */
        return current_dimension == Dimension::Nether;
#else
        return dimension_ultrawarm.at(current_dimension);
#endif
    }

    bool World::HasChunkBeenModified(const int x, const int z)
    {
#if USE_GUI
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({ x,z });
        if (it == terrain.end())
        {
            return true;
        }
        return it->second.GetModifiedSinceLastRender();
#else
        return false;
#endif
    }

    std::optional<Chunk> World::ResetChunkModificationState(const int x, const int z)
    {
#if USE_GUI
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({ x,z });
        if (it == terrain.end())
        {
            return std::optional<Chunk>();
        }
        it->second.SetModifiedSinceLastRender(false);
        return std::optional<Chunk>(it->second);
#else
        return std::optional<Chunk>();
#endif
    }

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    void World::LoadChunk(const int x, const int z, const Dimension dim, const std::thread::id& loader_id)
#else
    void World::LoadChunk(const int x, const int z, const std::string& dim, const std::thread::id& loader_id)
#endif
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        LoadChunkImpl(x, z, dim, loader_id);
    }

    void World::UnloadChunk(const int x, const int z, const std::thread::id& loader_id)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        UnloadChunkImpl(x, z, loader_id);
    }

    void World::UnloadAllChunks(const std::thread::id& loader_id)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        for (auto it = terrain.begin(); it != terrain.end();)
        {
            const int load_count = it->second.RemoveLoader(loader_id);
            if (load_count == 0)
            {
                terrain.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    void World::SetBlock(const Position& pos, const BlockstateId id)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        SetBlockImpl(pos, id);
    }

    const Blockstate* World::GetBlock(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        return GetBlockImpl(pos);
    }

    std::vector<const Blockstate*> World::GetBlocks(const std::vector<Position>& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        std::vector<const Blockstate*> output(pos.size());
        for (size_t i = 0; i < pos.size(); ++i)
        {
            output[i] = GetBlockImpl(pos[i]);
        }

        return output;
    }

    std::vector<AABB> World::GetColliders(const AABB& aabb, const Vector3<double>& movement) const
    {
        const AABB movement_extended_aabb(aabb.GetCenter() + movement * 0.5, aabb.GetHalfSize() + movement.Abs() * 0.5);
        const Vector3<double> min_aabb = movement_extended_aabb.GetMin();
        const Vector3<double> max_aabb = movement_extended_aabb.GetMax();
        std::vector<AABB> output;
        output.reserve(32);
        Position current_pos;
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        for (int y = static_cast<int>(std::floor(min_aabb.y)) - 1; y <= static_cast<int>(std::floor(max_aabb.y)); ++y)
        {
            current_pos.y = y;
            for (int z = static_cast<int>(std::floor(min_aabb.z)); z <= static_cast<int>(std::floor(max_aabb.z)); ++z)
            {
                current_pos.z = z;
                for (int x = static_cast<int>(std::floor(min_aabb.x)); x <= static_cast<int>(std::floor(max_aabb.x)); ++x)
                {
                    current_pos.x = x;
                    const Blockstate* block = GetBlockImpl(current_pos);
                    if (block == nullptr || !block->IsSolid())
                    {
                        continue;
                    }

                    const std::set<AABB> colliders = block->GetCollidersAtPos(current_pos);
                    output.insert(output.end(), colliders.begin(), colliders.end());
                }
            }
        }
        return output;
    }

    Vector3<double> World::GetFlow(const Position& pos)
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        Vector3<double> flow(0.0);
        std::vector<Position> horizontal_neighbours = {
            Position(0, 0, -1), Position(1, 0, 0),
            Position(0, 0, 1), Position(-1, 0, 0)
        };
        const Blockstate* block = GetBlockImpl(pos);
        if (block == nullptr || !block->IsFluidOrWaterlogged())
        {
            return flow;
        }

        const float current_fluid_height = block->GetFluidHeight();
        for (const Position& neighbour_pos : horizontal_neighbours)
        {
            const Blockstate* neighbour = GetBlockImpl(pos + neighbour_pos);
            if (neighbour == nullptr || (neighbour->IsFluidOrWaterlogged() && neighbour->IsWaterOrWaterlogged() != block->IsWaterOrWaterlogged()))
            {
                continue;
            }
            const float neighbour_fluid_height = neighbour->GetFluidHeight();
            if (neighbour_fluid_height == 0.0f)
            {
                if (!neighbour->IsSolid())
                {
                    const Blockstate* block_below_neighbour = GetBlockImpl(pos + neighbour_pos + Position(0, -1, 0));
                    if (block_below_neighbour != nullptr &&
                        (!block_below_neighbour->IsFluidOrWaterlogged() || block_below_neighbour->IsWaterOrWaterlogged() == block->IsWaterOrWaterlogged()))
                    {
                        const float block_below_neighbour_fluid_height = block_below_neighbour->GetFluidHeight();
                        if (block_below_neighbour_fluid_height > 0.0f)
                        {
                            flow.x += (current_fluid_height - block_below_neighbour_fluid_height + 0.8888889f) * neighbour_pos.x;
                            flow.z += (current_fluid_height - block_below_neighbour_fluid_height + 0.8888889f) * neighbour_pos.z;
                        }
                    }
                }
            }
            else
            {
                flow.x += (current_fluid_height - neighbour_fluid_height) * neighbour_pos.x;
                flow.z += (current_fluid_height - neighbour_fluid_height) * neighbour_pos.z;
            }
        }

        if (block->IsFluidFalling())
        {
            for (const Position& neighbour_pos : horizontal_neighbours)
            {
                const Blockstate* neighbour = GetBlockImpl(pos + neighbour_pos);
                if (neighbour == nullptr)
                {
                    continue;
                }
                const Blockstate* above_neighbour = GetBlockImpl(pos + neighbour_pos + Position(0, 1, 0));
                if (above_neighbour == nullptr)
                {
                    continue;
                }
                if (neighbour->IsSolid() && above_neighbour->IsSolid())
                {
                    flow.Normalize();
                    flow.y -= 6.0;
                    break;
                }
            }
        }

        flow.Normalize();
        return flow;
    }

    Utilities::ScopeLockedWrapper<const std::unordered_map<std::pair<int, int>, Chunk>, std::shared_mutex, std::shared_lock> World::GetChunks() const
    {
        return Utilities::ScopeLockedWrapper<const std::unordered_map<std::pair<int, int>, Chunk>, std::shared_mutex, std::shared_lock>(terrain, world_mutex);
    }

#if PROTOCOL_VERSION < 358 /* < 1.13 */
    void World::SetBiome(const int x, const int z, const unsigned char biome)
#elif PROTOCOL_VERSION < 552 /* < 1.15 */
    void World::SetBiome(const int x, const int z, const int biome)
#else
    void World::SetBiome(const int x, const int y, const int z, const int biome)
#endif
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 552 /* < 1.15 */
        SetBiomeImpl(x, z, biome);
#else
        SetBiomeImpl(x, y, z, biome);
#endif
    }

    const Biome* World::GetBiome(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });
        if (it == terrain.end())
        {
            return nullptr;
        }

        const int in_chunk_x = (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
        const int in_chunk_z = (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;

#if PROTOCOL_VERSION < 552 /* < 1.15 */
        return it->second.GetBiome(in_chunk_x, in_chunk_z);
#else
        return it->second.GetBiome(in_chunk_x, pos.y, in_chunk_z);
#endif
    }

    void World::SetSkyLight(const Position& pos, const unsigned char skylight)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });

        if (it != terrain.end() && it->second.GetHasSkyLight())
        {
            const int in_chunk_x = (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
            const int in_chunk_z = (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
            it->second.SetSkyLight(Position(in_chunk_x, pos.y, in_chunk_z), skylight);
        }
    }

    void World::SetBlockLight(const Position& pos, const unsigned char blocklight)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });

        if (it != terrain.end())
        {
            const int in_chunk_x = (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
            const int in_chunk_z = (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
            it->second.SetBlockLight(Position(in_chunk_x, pos.y, in_chunk_z), blocklight);
        }
    }

    unsigned char World::GetSkyLight(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });
        if (it == terrain.end())
        {
            return 0;
        }

        const int in_chunk_x = (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
        const int in_chunk_z = (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
        return it->second.GetSkyLight(Position(in_chunk_x, pos.y, in_chunk_z));
    }

    unsigned char World::GetBlockLight(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });
        if (it == terrain.end())
        {
            return 0;
        }

        const int in_chunk_x = (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
        const int in_chunk_z = (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH;
        return it->second.GetBlockLight(Position(in_chunk_x, pos.y, in_chunk_z));
    }

    void World::SetBlockEntityData(const Position& pos, const ProtocolCraft::NBT::Value& data)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });
        if (it == terrain.end())
        {
            return;
        }

        const Position chunk_pos(
            (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH,
            pos.y,
            (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH
        );

        if (data.HasData())
        {
            it->second.SetBlockEntityData(chunk_pos, data);
        }
        else
        {
            it->second.RemoveBlockEntityData(chunk_pos);
        }
    }

    ProtocolCraft::NBT::Value World::GetBlockEntityData(const Position& pos) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({
            static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)))
        });

        if (it == terrain.end())
        {
            return ProtocolCraft::NBT::Value();
        }

        const Position chunk_pos(
            (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH,
            pos.y,
            (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH
        );

        return it->second.GetBlockEntityData(chunk_pos);
    }

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    Dimension World::GetDimension(const int x, const int z) const
#else
    std::string World::GetDimension(const int x, const int z) const
#endif
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        auto it = terrain.find({ x, z });
        if (it == terrain.end())
        {
#if PROTOCOL_VERSION < 719 /* < 1.16 */
            return Dimension::None;
#else
            return "";
#endif
        }

        return index_dimension_map.at(it->second.GetDimensionIndex());
    }

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    Dimension World::GetCurrentDimension() const
#else
    std::string World::GetCurrentDimension() const
#endif
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);
        return current_dimension;
    }

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    void World::SetCurrentDimension(const Dimension dimension)
#else
    void World::SetCurrentDimension(const std::string& dimension)
#endif
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        SetCurrentDimensionImpl(dimension);
    }

#if PROTOCOL_VERSION > 756 /* > 1.17.1 */
    void World::SetDimensionHeight(const std::string& dimension, const int height)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        dimension_height[dimension] = height;
    }

    void World::SetDimensionMinY(const std::string& dimension, const int min_y)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        dimension_min_y[dimension] = min_y;
    }
#endif

#if PROTOCOL_VERSION > 718 /* > 1.15.2 */
    void World::SetDimensionUltrawarm(const std::string& dimension, const bool ultrawarm)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        dimension_ultrawarm[dimension] = ultrawarm;
    }
#endif

    const Blockstate* World::Raycast(const Vector3<double>& origin, const Vector3<double>& direction, const float max_radius, Position& out_pos, Position& out_normal)
    {
        // Inspired from https://gist.github.com/dogfuntom/cc881c8fc86ad43d55d8
        // Searching along origin + t * direction line

        // Position of the current cube examined
        out_pos = Position(
            static_cast<int>(std::floor(origin.x)),
            static_cast<int>(std::floor(origin.y)),
            static_cast<int>(std::floor(origin.z))
        );

        // Increment on each axis
        Vector3<double> step((0.0 < direction.x) - (direction.x < 0.0), (0.0 < direction.y) - (direction.y < 0.0), (0.0 < direction.z) - (direction.z < 0.0));

        // tMax is the t-value to cross a cube boundary
        // for each axis. The axis with the least tMax
        // value is the one the ray crosses in first
        // tDelta is the increment of t for each step
        Vector3<double> tMax, tDelta;

        for (int i = 0; i < 3; ++i)
        {
            bool isInteger = std::round(origin[i]) == origin[i];
            if (direction[i] < 0 && isInteger)
            {
                tMax[i] = 0.0;
            }
            else
            {
                if (direction[i] > 0)
                {
                    tMax[i] = ((origin[i] == 0.0f) ? 1.0f : std::ceil(origin[i]) - origin[i]) / std::abs(direction[i]);
                }
                else if (direction[i] < 0)
                {
                    tMax[i] = (origin[i] - std::floor(origin[i])) / std::abs(direction[i]);
                }
                else
                {
                    tMax[i] = std::numeric_limits<double>::max();
                }
            }

            if (direction[i] == 0)
            {
                tDelta[i] = std::numeric_limits<double>::max();
            }
            else
            {
                tDelta[i] = step[i] / direction[i];
            }
        }

        if (direction.x == 0 && direction.y == 0 && direction.z == 0)
        {
            throw std::runtime_error("Raycasting with null direction");
        }

        const float radius = max_radius / static_cast<float>(std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z));

        while (true)
        {
            const Blockstate* block = GetBlock(out_pos);

            if (block != nullptr && !block->IsAir())
            {
                for (const auto& collider : block->GetCollidersAtPos(out_pos))
                {
                    if (collider.Intersect(origin, direction))
                    {
                        return block;
                    }
                }
            }

            // select the direction in which the next face is
            // the closest
            if (tMax.x < tMax.y && tMax.x < tMax.z)
            {
                if (tMax.x > radius)
                {
                    return nullptr;
                }

                out_pos.x += static_cast<int>(std::round(step.x));
                tMax.x += tDelta.x;
                out_normal.x = -static_cast<int>(std::round(step.x));
                out_normal.y = 0;
                out_normal.z = 0;
            }
            else if (tMax.y < tMax.x && tMax.y < tMax.z)
            {
                if (tMax.y > radius)
                {
                    return nullptr;
                }
                out_pos.y += static_cast<int>(std::round(step.y));
                tMax.y += tDelta.y;
                out_normal[0] = 0;
                out_normal[1] = -static_cast<int>(std::round(step.y));
                out_normal[2] = 0;
            }
            else // tMax.z < tMax.x && tMax.z < tMax.y
            {
                if (tMax.z > radius)
                {
                    return nullptr;
                }

                out_pos.z += static_cast<int>(std::round(step.z));
                tMax.z += tDelta.z;
                out_normal.x = 0;
                out_normal.x = 0;
                out_normal.x = -static_cast<int>(std::round(step.z));
            }
        }
    }

#if PROTOCOL_VERSION > 758 /* > 1.18.2 */
    int World::GetNextWorldInteractionSequenceId()
    {
        return ++world_interaction_sequence_id;
    }
#endif

    bool World::IsFree(const AABB& aabb, const bool fluid_collide) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);

        const Vector3<double> min_aabb = aabb.GetMin();
        const Vector3<double> max_aabb = aabb.GetMax();

        Position cube_pos;
        for (int y = static_cast<int>(std::floor(min_aabb.y)) - 1; y <= static_cast<int>(std::floor(max_aabb.y)); ++y)
        {
            cube_pos.y = y;
            for (int z = static_cast<int>(std::floor(min_aabb.z)); z <= static_cast<int>(std::floor(max_aabb.z)); ++z)
            {
                cube_pos.z = z;
                for (int x = static_cast<int>(std::floor(min_aabb.x)); x <= static_cast<int>(std::floor(max_aabb.x)); ++x)
                {
                    cube_pos.x = x;
                    const Blockstate* block = GetBlockImpl(cube_pos);

                    if (block == nullptr)
                    {
                        continue;
                    }

                    if (block->IsFluid())
                    {
                        if (!fluid_collide)
                        {
                            continue;
                        }
                    }
                    else if (!block->IsSolid())
                    {
                        continue;
                    }

                    for (const auto& collider : block->GetCollidersAtPos(cube_pos))
                    {
                        if (aabb.Collide(collider))
                        {
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    std::optional<Position> World::GetSupportingBlockPos(const AABB& aabb) const
    {
        std::shared_lock<std::shared_mutex> lock(world_mutex);

        const Vector3<double> min_aabb = aabb.GetMin();
        const Vector3<double> max_aabb = aabb.GetMax();

        Position cube_pos;
        std::optional<Position> output = std::optional<Position>();
        double min_distance = std::numeric_limits<double>::max();
        for (int y = static_cast<int>(std::floor(min_aabb.y)) - 1; y <= static_cast<int>(std::floor(max_aabb.y)); ++y)
        {
            cube_pos.y = y;
            for (int z = static_cast<int>(std::floor(min_aabb.z)); z <= static_cast<int>(std::floor(max_aabb.z)); ++z)
            {
                cube_pos.z = z;
                for (int x = static_cast<int>(std::floor(min_aabb.x)); x <= static_cast<int>(std::floor(max_aabb.x)); ++x)
                {
                    cube_pos.x = x;
                    const Blockstate* block = GetBlockImpl(cube_pos);

                    if (block == nullptr || !block->IsSolid())
                    {
                        continue;
                    }

                    for (const auto& collider : block->GetCollidersAtPos(cube_pos))
                    {
                        if (aabb.Collide(collider))
                        {
                            const double distance = aabb.GetCenter().SqrDist(collider.GetCenter());
                            if (distance < min_distance)
                            {
                                min_distance = distance;
                                output = cube_pos;
                            }
                        }
                    }
                }
            }
        }

        return output;
    }

    void World::Handle(ProtocolCraft::ClientboundLoginPacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 719 /* < 1.16 */
        SetCurrentDimensionImpl(static_cast<Dimension>(msg.GetDimension()));
#elif PROTOCOL_VERSION < 764 /* < 1.20.2 */
        SetCurrentDimensionImpl(msg.GetDimension().GetFull());
#else
        SetCurrentDimensionImpl(msg.GetCommonPlayerSpawnInfo().GetDimension().GetFull());
#endif

#if PROTOCOL_VERSION > 718 /* > 1.15.2 */
#if PROTOCOL_VERSION < 751 /* < 1.16.2 */
        for (const auto& d : msg.GetRegistryHolder()["dimension"].as_list_of<ProtocolCraft::NBT::TagCompound>())
        {
            const std::string& dim_name = d["name"].get<std::string>();
            dimension_ultrawarm[dim_name] = static_cast<bool>(d["ultrawarm"].get<char>());
        }
#elif PROTOCOL_VERSION < 764 /* < 1.20.2 */
        for (const auto& d : msg.GetRegistryHolder()["minecraft:dimension_type"]["value"].as_list_of<ProtocolCraft::NBT::TagCompound>())
        {
            const std::string& dim_name = d["name"].get<std::string>();
            dimension_ultrawarm[dim_name] = static_cast<bool>(d["element"]["ultrawarm"].get<char>());
#if PROTOCOL_VERSION > 756 /* > 1.17.1 */
            dimension_height[dim_name] = static_cast<unsigned int>(d["element"]["height"].get<int>());
            dimension_min_y[dim_name] = d["element"]["min_y"].get<int>();
#endif
        }
#endif
#endif
    }

    void World::Handle(ProtocolCraft::ClientboundRespawnPacket& msg)
    {
        UnloadAllChunks(std::this_thread::get_id());

        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 719 /* < 1.16 */
        SetCurrentDimensionImpl(static_cast<Dimension>(msg.GetDimension()));
#elif PROTOCOL_VERSION < 764 /* < 1.20.2 */
        SetCurrentDimensionImpl(msg.GetDimension().GetFull());
#else
        SetCurrentDimensionImpl(msg.GetCommonPlayerSpawnInfo().GetDimension().GetFull());
#endif

#if PROTOCOL_VERSION > 747 /* > 1.16.1 */ && PROTOCOL_VERSION < 759 /* < 1.19 */
        dimension_ultrawarm[current_dimension] = static_cast<bool>(msg.GetDimensionType()["ultrawarm"].get<char>());
#if PROTOCOL_VERSION > 756 /* > 1.17.1 */
        dimension_height[current_dimension] = msg.GetDimensionType()["height"].get<int>();
        dimension_min_y[current_dimension] = msg.GetDimensionType()["min_y"].get<int>();
#endif
#endif
    }

    void World::Handle(ProtocolCraft::ClientboundBlockUpdatePacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 347 /* < 1.13 */
        int id;
        unsigned char metadata;
        Blockstate::IdToIdMetadata(msg.GetBlockstate(), id, metadata);
        SetBlockImpl(msg.GetPos(), { id, metadata });
#else
        SetBlockImpl(msg.GetPos(), msg.GetBlockstate());
#endif
    }

    void World::Handle(ProtocolCraft::ClientboundSectionBlocksUpdatePacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 739 /* < 1.16.2 */
        for (size_t i = 0; i < msg.GetRecords().size(); ++i)
        {
            unsigned char x = (msg.GetRecords()[i].GetHorizontalPosition() >> 4) & 0x0F;
            unsigned char z = msg.GetRecords()[i].GetHorizontalPosition() & 0x0F;

            const int x_pos = CHUNK_WIDTH * msg.GetChunkX() + x;
            const int y_pos = msg.GetRecords()[i].GetYCoordinate();
            const int z_pos = CHUNK_WIDTH * msg.GetChunkZ() + z;
#else
        const int chunk_x = CHUNK_WIDTH * (msg.GetSectionPos() >> 42); // 22 bits
        const int chunk_z = CHUNK_WIDTH * (msg.GetSectionPos() << 22 >> 42); // 22 bits
        const int chunk_y = SECTION_HEIGHT * (msg.GetSectionPos() << 44 >> 44); // 20 bits

        for (size_t i = 0; i < msg.GetPosState().size(); ++i)
        {
            const unsigned int block_id = msg.GetPosState()[i] >> 12;
            const short position = msg.GetPosState()[i] & 0xFFFl;

            const int x_pos = chunk_x + ((position >> 8) & 0xF);
            const int z_pos = chunk_z + ((position >> 4) & 0xF);
            const int y_pos = chunk_y + ((position >> 0) & 0xF);
#endif
            Position cube_pos(x_pos, y_pos, z_pos);

            {
#if PROTOCOL_VERSION < 347 /* < 1.13 */
                int id;
                unsigned char metadata;
                Blockstate::IdToIdMetadata(msg.GetRecords()[i].GetBlockId(), id, metadata);

                SetBlockImpl(cube_pos, { id, metadata });
#elif PROTOCOL_VERSION < 739 /* < 1.16.2 */
                SetBlockImpl(cube_pos, msg.GetRecords()[i].GetBlockId());
#else
                SetBlockImpl(cube_pos, block_id);
#endif
            }
        }
    }

    void World::Handle(ProtocolCraft::ClientboundForgetLevelChunkPacket& msg)
    {
#if PROTOCOL_VERSION < 764 /* < 1.20.2 */
        UnloadChunk(msg.GetX(), msg.GetZ(), std::this_thread::get_id());
#else
        UnloadChunk(msg.GetPos().GetX(), msg.GetPos().GetZ(), std::this_thread::get_id());
#endif
    }

#if PROTOCOL_VERSION < 757 /* < 1.18 */
    void World::Handle(ProtocolCraft::ClientboundLevelChunkPacket& msg)
    {

#if PROTOCOL_VERSION < 755 /* < 1.17 */
        if (msg.GetFullChunk())
        {
#endif
            LoadChunk(msg.GetX(), msg.GetZ(), current_dimension, std::this_thread::get_id());
#if PROTOCOL_VERSION < 755 /* < 1.17 */
        }
#endif

        { // lock scope
            std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
            if (auto it = delayed_light_updates.find({ msg.GetX(), msg.GetZ() }); it != delayed_light_updates.end())
            {
                UpdateChunkLight(it->second.GetX(), it->second.GetZ(), current_dimension,
                    it->second.GetSkyYMask(), it->second.GetEmptySkyYMask(), it->second.GetSkyUpdates(), true);
                UpdateChunkLight(it->second.GetX(), it->second.GetZ(), current_dimension,
                    it->second.GetBlockYMask(), it->second.GetEmptyBlockYMask(), it->second.GetBlockUpdates(), false);
                delayed_light_updates.erase(it);
            }
#endif
#if PROTOCOL_VERSION < 552 /* < 1.15 */
            LoadDataInChunk(msg.GetX(), msg.GetZ(), msg.GetBuffer(), msg.GetAvailableSections(), msg.GetFullChunk());
#else
            LoadDataInChunk(msg.GetX(), msg.GetZ(), msg.GetBuffer(), msg.GetAvailableSections());
#if PROTOCOL_VERSION < 755 /* < 1.17 */
            if (msg.GetBiomes().has_value())
            {
#if PROTOCOL_VERSION < 751 /* < 1.16.2 */
                // Copy to convert the std::array to std::vector
                LoadBiomesInChunk(msg.GetX(), msg.GetZ(), std::vector<int>(msg.GetBiomes().value().begin(), msg.GetBiomes().value().end()));
#else
                LoadBiomesInChunk(msg.GetX(), msg.GetZ(), msg.GetBiomes().value());
#endif
            }
#else
            LoadBiomesInChunk(msg.GetX(), msg.GetZ(), msg.GetBiomes());
#endif
#endif
            LoadBlockEntityDataInChunk(msg.GetX(), msg.GetZ(), msg.GetBlockEntitiesTags());
        }
    }
#else
    void World::Handle(ProtocolCraft::ClientboundLevelChunkWithLightPacket& msg)
    {
            std::scoped_lock<std::shared_mutex> lock(world_mutex);
            LoadChunkImpl(msg.GetX(), msg.GetZ(), current_dimension, std::this_thread::get_id());
            LoadDataInChunk(msg.GetX(), msg.GetZ(), msg.GetChunkData().GetBuffer());
            LoadBlockEntityDataInChunk(msg.GetX(), msg.GetZ(), msg.GetChunkData().GetBlockEntitiesData());
            UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
                msg.GetLightData().GetSkyYMask(), msg.GetLightData().GetEmptySkyYMask(), msg.GetLightData().GetSkyUpdates(), true);
            UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
                msg.GetLightData().GetBlockYMask(), msg.GetLightData().GetEmptyBlockYMask(), msg.GetLightData().GetBlockUpdates(), false);
    }
#endif

#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
    void World::Handle(ProtocolCraft::ClientboundLightUpdatePacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 757 /* < 1.18 */
        if (terrain.find({ msg.GetX(), msg.GetZ() }) == terrain.end())
        {
            delayed_light_updates[{msg.GetX(), msg.GetZ()}] = msg;
            return;
        }
        UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
            msg.GetSkyYMask(), msg.GetEmptySkyYMask(), msg.GetSkyUpdates(), true);
        UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
            msg.GetBlockYMask(), msg.GetEmptyBlockYMask(), msg.GetBlockUpdates(), false);
#else
        UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
            msg.GetLightData().GetSkyYMask(), msg.GetLightData().GetEmptySkyYMask(), msg.GetLightData().GetSkyUpdates(), true);
        UpdateChunkLight(msg.GetX(), msg.GetZ(), current_dimension,
            msg.GetLightData().GetBlockYMask(), msg.GetLightData().GetEmptyBlockYMask(), msg.GetLightData().GetBlockUpdates(), false);
#endif
    }
#endif

    void World::Handle(ProtocolCraft::ClientboundBlockEntityDataPacket& msg)
    {
        SetBlockEntityData(msg.GetPos(), msg.GetTag());
    }

#if PROTOCOL_VERSION > 761 /* > 1.19.3 */
    void World::Handle(ProtocolCraft::ClientboundChunksBiomesPacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
        for (const auto& chunk_data : msg.GetChunkBiomeData())
        {
            auto it = terrain.find({ chunk_data.GetPos().GetX(), chunk_data.GetPos().GetZ()});
            if (it != terrain.end())
            {
                it->second.LoadBiomesData(chunk_data.GetBuffer());
            }
            else
            {
                LOG_WARNING("Trying to load biomes data in non loaded chunk (" << chunk_data.GetPos().GetX() << ", " << chunk_data.GetPos().GetZ() << ")");
            }
        }
    }
#endif

#if PROTOCOL_VERSION > 763 /* > 1.20.1 */
    void World::Handle(ProtocolCraft::ClientboundRegistryDataPacket& msg)
    {
        std::scoped_lock<std::shared_mutex> lock(world_mutex);
#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
        for (const auto& d : msg.GetRegistryHolder()["minecraft:dimension_type"]["value"].as_list_of<ProtocolCraft::NBT::TagCompound>())
        {
            const std::string& dim_name = d["name"].get<std::string>();
            dimension_height[dim_name] = static_cast<unsigned int>(d["element"]["height"].get<int>());
            dimension_min_y[dim_name] = d["element"]["min_y"].get<int>();
            dimension_ultrawarm[dim_name] = static_cast<bool>(d["element"]["ultrawarm"].get<char>());
        }
#else
        if (msg.GetRegistry().GetFull() != "minecraft:dimension_type")
        {
            return;
        }

        const auto& entries = msg.GetEntries();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const std::string dim_name = entries[i].GetId().GetFull();
            // Make sure we use the same indices as minecraft registry
            // Not really useful but just in case
            dimension_index_map.insert({ dim_name, i });
            index_dimension_map.insert({ i, dim_name });

            if (entries[i].GetData().has_value())
            {
                const ProtocolCraft::NBT::Value& data = entries[i].GetData().value();
                dimension_height[dim_name] = static_cast<unsigned int>(data["height"].get<int>());
                dimension_min_y[dim_name] = data["min_y"].get<int>();
                dimension_ultrawarm[dim_name] = static_cast<bool>(data["ultrawarm"].get<char>());
            }
        }
#endif
    }
#endif

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    void World::LoadChunkImpl(const int x, const int z, const Dimension dim, const std::thread::id& loader_id)
#else
    void World::LoadChunkImpl(const int x, const int z, const std::string& dim, const std::thread::id& loader_id)
#endif
    {
#if PROTOCOL_VERSION < 719 /* < 1.16 */
        const bool has_sky_light = dim == Dimension::Overworld;
#else
        const bool has_sky_light = dim == "minecraft:overworld";
#endif
        const size_t dim_index = GetDimIndex(dim);
        auto it = terrain.find({ x,z });
        if (it == terrain.end())
        {
#if PROTOCOL_VERSION < 757 /* < 1.18 */
            auto inserted = terrain.insert({ {x, z}, Chunk(dim_index, has_sky_light) });
#else
            auto inserted = terrain.insert({ { x, z }, Chunk(dimension_min_y.at(dim), dimension_height.at(dim), dim_index, has_sky_light)});
#endif
            inserted.first->second.AddLoader(loader_id);
        }
        // This may already exists in this dimension if this is a shared world
        else if (it->second.GetDimensionIndex() != dim_index)
        {
            if (is_shared)
            {
                LOG_WARNING("Changing dimension with a shared world is not supported and can lead to wrong world data");
            }
            UnloadChunkImpl(x, z, loader_id);
#if PROTOCOL_VERSION < 757 /* < 1.18 */
            it->second = Chunk(dim_index, has_sky_light);
#else
            it->second = Chunk(dimension_min_y.at(dim), dimension_height.at(dim), dim_index, has_sky_light);
#endif
            it->second.AddLoader(loader_id);
        }
        else
        {
            it->second.AddLoader(loader_id);
        }

        //Not necessary, from void to air, there is no difference
        //UpdateChunk(x, z);
    }

    void World::UnloadChunkImpl(const int x, const int z, const std::thread::id& loader_id)
    {
        auto it = terrain.find({ x, z });
        if (it != terrain.end())
        {
            const size_t load_counter = it->second.RemoveLoader(loader_id);
            if (load_counter == 0)
            {
                terrain.erase(it);
#if USE_GUI
                UpdateChunk(x, z);
#endif
            }
        }
    }

    void World::SetBlockImpl(const Position& pos, const BlockstateId id)
    {
        const int chunk_x = static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH)));
        const int chunk_z = static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)));

        auto it = terrain.find({ chunk_x, chunk_z });

        // Can't set block in unloaded chunk
        if (it == terrain.end())
        {
            return;
        }

        const Position set_pos(
            (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH,
            pos.y,
            (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH
        );

        it->second.SetBlock(set_pos, id);

#if USE_GUI
        // If this block is on the edge, update neighbours chunks
        UpdateChunk(chunk_x, chunk_z, pos);
#endif
    }

    const Blockstate* World::GetBlockImpl(const Position& pos) const
    {
        const int chunk_x = static_cast<int>(std::floor(pos.x / static_cast<double>(CHUNK_WIDTH)));
        const int chunk_z = static_cast<int>(std::floor(pos.z / static_cast<double>(CHUNK_WIDTH)));

        auto it = terrain.find({ chunk_x, chunk_z });

        // Can't get block in unloaded chunk
        if (it == terrain.end())
        {
            return nullptr;
        }

        const Position chunk_pos(
            (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH,
            pos.y,
            (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH
        );

        const Blockstate* output = it->second.GetBlock(chunk_pos);

        // As we are in a loaded chunk, nullptr means it's in an empty section
        // (or the chunk position was invalid but we know it's valid given how it's constructed above)
        // --> return air block instead of nullptr
        return output != nullptr ?
            output :
#if PROTOCOL_VERSION < 347 /* < 1.13 */
            AssetsManager::getInstance().GetBlockstate(std::make_pair(0, 0));
#else
            AssetsManager::getInstance().GetBlockstate(0);
#endif
    }

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    void World::SetCurrentDimensionImpl(const Dimension dimension)
#else
    void World::SetCurrentDimensionImpl(const std::string& dimension)
#endif
    {
        current_dimension = dimension;
#if PROTOCOL_VERSION > 404 /* > 1.13.2 */ && PROTOCOL_VERSION < 757 /* < 1.18 */
        delayed_light_updates.clear();
#endif
    }

    int World::GetHeightImpl() const
    {
#if PROTOCOL_VERSION < 757 /* < 1.18 */
        return 256;
#else
        return dimension_height.at(current_dimension);
#endif
    }

    int World::GetMinYImpl() const
    {
#if PROTOCOL_VERSION < 757 /* < 1.18 */
        return 0;
#else
        return dimension_min_y.at(current_dimension);
#endif
    }

#if PROTOCOL_VERSION < 358 /* < 1.13 */
    void World::SetBiomeImpl(const int x, const int z, const unsigned char biome)
#elif PROTOCOL_VERSION < 552 /* < 1.15 */
    void World::SetBiomeImpl(const int x, const int z, const int biome)
#else
    void World::SetBiomeImpl(const int x, const int y, const int z, const int biome)
#endif
    {
        auto it = terrain.find({
            static_cast<int>(std::floor(x / static_cast<double>(CHUNK_WIDTH))),
            static_cast<int>(std::floor(z / static_cast<double>(CHUNK_WIDTH)))
            });

        if (it != terrain.end())
        {
#if PROTOCOL_VERSION < 552 /* < 1.15 */
            it->second.SetBiome((x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH, (z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH, biome);
#else
            it->second.SetBiome((x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH, y, (z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH, biome);
#endif
        }
    }

    void World::UpdateChunk(const int x, const int z, const Position& pos)
    {
#if USE_GUI
        const Position this_chunk_position(
            (pos.x % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH,
            pos.y,
            (pos.z % CHUNK_WIDTH + CHUNK_WIDTH) % CHUNK_WIDTH
        );

        if (this_chunk_position.x > 0 && this_chunk_position.x < CHUNK_WIDTH - 1 &&
            this_chunk_position.z > 0 && this_chunk_position.z < CHUNK_WIDTH - 1)
        {
            return;
        }

        Chunk* chunk = GetChunk(x, z);

        if (chunk == nullptr)
        {
            // This should not happen
            LOG_WARNING("Trying to propagate chunk updates from a non loaded chunk");
            return;
        }

        const Blockstate* blockstate = chunk->GetBlock(this_chunk_position);

        if (this_chunk_position.x == 0)
        {
            Chunk* neighbour_chunk = GetChunk(x - 1, z);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->SetBlock(Position(CHUNK_WIDTH, this_chunk_position.y, this_chunk_position.z), blockstate);
            }
        }

        if (this_chunk_position.x == CHUNK_WIDTH - 1)
        {
            Chunk* neighbour_chunk = GetChunk(x + 1, z);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->SetBlock(Position(-1, this_chunk_position.y, this_chunk_position.z), blockstate);
            }
        }

        if (this_chunk_position.z == 0)
        {
            Chunk* neighbour_chunk = GetChunk(x, z - 1);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->SetBlock(Position(this_chunk_position.x, this_chunk_position.y, CHUNK_WIDTH), blockstate);
            }
        }

        if (this_chunk_position.z == CHUNK_WIDTH - 1)
        {
            Chunk* neighbour_chunk = GetChunk(x, z + 1);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->SetBlock(Position(this_chunk_position.x, this_chunk_position.y, -1), blockstate);
            }
        }
#endif
    }

    void World::UpdateChunk(const int x, const int z)
    {
#if USE_GUI
        Chunk* chunk = GetChunk(x, z);
        // This means this chunk has just beend added
        // Copy data for all edges between its neighbours
        if (chunk != nullptr)
        {
            chunk->UpdateNeighbour(GetChunk(x - 1, z), Orientation::West);
            chunk->UpdateNeighbour(GetChunk(x + 1, z), Orientation::East);
            chunk->UpdateNeighbour(GetChunk(x, z - 1), Orientation::North);
            chunk->UpdateNeighbour(GetChunk(x, z + 1), Orientation::South);
        }
        // This means this chunk has just been removed
        // Update all existing neighbours to let them know
        else
        {
            Chunk* neighbour_chunk;
            neighbour_chunk = GetChunk(x - 1, z);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->UpdateNeighbour(nullptr, Orientation::East);
            }
            neighbour_chunk = GetChunk(x + 1, z);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->UpdateNeighbour(nullptr, Orientation::West);
            }
            neighbour_chunk = GetChunk(x, z - 1);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->UpdateNeighbour(nullptr, Orientation::South);
            }
            neighbour_chunk = GetChunk(x, z + 1);
            if (neighbour_chunk != nullptr)
            {
                neighbour_chunk->UpdateNeighbour(nullptr, Orientation::North);
            }
        }
#endif
    }

    Chunk* World::GetChunk(const int x, const int z)
    {
        auto it = terrain.find({ x,z });
        if (it == terrain.end())
        {
            return nullptr;
        }
        return &it->second;
    }

#if PROTOCOL_VERSION < 552 /* < 1.15 */
    void World::LoadDataInChunk(const int x, const int z, const std::vector<unsigned char>& data,
        const int primary_bit_mask, const bool ground_up_continuous)
#elif PROTOCOL_VERSION < 755 /* < 1.17 */
    void World::LoadDataInChunk(const int x, const int z, const std::vector<unsigned char>& data,
        const int primary_bit_mask)
#elif PROTOCOL_VERSION < 757 /* < 1.18 */
    void World::LoadDataInChunk(const int x, const int z, const std::vector<unsigned char>& data,
        const std::vector<unsigned long long int>& primary_bit_mask)
#else
    void World::LoadDataInChunk(const int x, const int z, const std::vector<unsigned char>& data)
#endif
    {
        auto it = terrain.find({ x,z });
        if (it != terrain.end())
        {
#if PROTOCOL_VERSION < 552 /* < 1.15 */
            it->second.LoadChunkData(data, primary_bit_mask, ground_up_continuous);
#elif PROTOCOL_VERSION < 757 /* < 1.18 */
            it->second.LoadChunkData(data, primary_bit_mask);
#else
            it->second.LoadChunkData(data);
#endif
#if USE_GUI
            UpdateChunk(x, z);
#endif
        }
    }

#if PROTOCOL_VERSION < 757 /* < 1.18 */
    void World::LoadBlockEntityDataInChunk(const int x, const int z, const std::vector<ProtocolCraft::NBT::Value>& block_entities)
#else
    void World::LoadBlockEntityDataInChunk(const int x, const int z, const std::vector<ProtocolCraft::BlockEntityInfo>& block_entities)
#endif
    {
        auto it = terrain.find({ x,z });
        if (it != terrain.end())
        {
            it->second.LoadChunkBlockEntitiesData(block_entities);
        }
    }

#if PROTOCOL_VERSION > 551 /* > 1.14.4 */ && PROTOCOL_VERSION < 757 /* < 1.18 */
    void World::LoadBiomesInChunk(const int x, const int z, const std::vector<int>& biomes)
    {
        auto it = terrain.find({ x,z });
        if (it != terrain.end())
        {
            it->second.SetBiomes(biomes);
        }
    }
#endif

#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
#if PROTOCOL_VERSION < 719 /* < 1.16 */
    void World::UpdateChunkLight(const int x, const int z, const Dimension dim, const int light_mask, const int empty_light_mask,
        const std::vector<std::vector<char>>& data, const bool sky)
#elif PROTOCOL_VERSION < 755 /* < 1.17 */
    void World::UpdateChunkLight(const int x, const int z, const std::string& dim, const int light_mask, const int empty_light_mask,
        const std::vector<std::vector<char>>& data, const bool sky)
#else
    void World::UpdateChunkLight(const int x, const int z, const std::string& dim,
        const std::vector<unsigned long long int>& light_mask, const std::vector<unsigned long long int>& empty_light_mask,
        const std::vector<std::vector<char>>& data, const bool sky)
#endif
    {
        auto it = terrain.find({ x, z });

        if (it == terrain.end())
        {
            LOG_WARNING("Trying to update lights in an unloaded chunk: (" << x << "," << z << ")");
            return;
        }

        int counter_arrays = 0;
        Position pos1, pos2;

        const int num_sections = GetHeightImpl() / 16 + 2;

        for (int i = 0; i < num_sections; ++i)
        {
            const int section_Y = i - 1;

            // Sky light
#if PROTOCOL_VERSION < 755 /* < 1.17 */
            if ((light_mask >> i) & 1)
#else
            if ((light_mask.size() > i / 64) && (light_mask[i / 64] >> (i % 64)) & 1)
#endif
            {
                if (i > 0 && i < num_sections - 1)
                {
                    for (int block_y = 0; block_y < SECTION_HEIGHT; ++block_y)
                    {
                        pos1.y = block_y + section_Y * SECTION_HEIGHT + GetMinYImpl();
                        pos2.y = pos1.y;
                        for (int block_z = 0; block_z < CHUNK_WIDTH; ++block_z)
                        {
                            pos1.z = block_z;
                            pos2.z = block_z;
                            for (int block_x = 0; block_x < CHUNK_WIDTH; block_x += 2)
                            {
                                pos1.x = block_x;
                                pos2.x = block_x + 1;
                                const char two_light_values = data[counter_arrays][(block_y * CHUNK_WIDTH * CHUNK_WIDTH + block_z * CHUNK_WIDTH + block_x) / 2];

                                if (sky)
                                {
                                    it->second.SetSkyLight(pos1, two_light_values & 0x0F);
                                    it->second.SetSkyLight(pos2, (two_light_values >> 4) & 0x0F);
                                }
                                else
                                {
                                    it->second.SetBlockLight(pos1, two_light_values & 0x0F);
                                    it->second.SetBlockLight(pos2, (two_light_values >> 4) & 0x0F);
                                }
                            }
                        }
                    }
                }
                counter_arrays++;
            }
#if PROTOCOL_VERSION < 755 /* < 1.17 */
            else if ((empty_light_mask >> i) & 1)
#else
            else if ((empty_light_mask.size() > i / 64) && (empty_light_mask[i / 64] >> (i % 64)) & 1)
#endif
            {
                if (i > 0 && i < num_sections - 1)
                {
                    for (int block_y = 0; block_y < SECTION_HEIGHT; ++block_y)
                    {
                        pos1.y = block_y + section_Y * SECTION_HEIGHT + GetMinYImpl();
                        pos2.y = pos1.y;
                        for (int block_z = 0; block_z < CHUNK_WIDTH; ++block_z)
                        {
                            pos1.z = block_z;
                            pos2.z = block_z;
                            for (int block_x = 0; block_x < CHUNK_WIDTH; block_x += 2)
                            {
                                pos1.x = block_x;
                                pos2.x = block_x + 1;
                                if (sky)
                                {
                                    it->second.SetSkyLight(pos1, 0);
                                    it->second.SetSkyLight(pos2, 0);
                                }
                                else
                                {
                                    it->second.SetBlockLight(pos1, 0);
                                    it->second.SetBlockLight(pos2, 0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#endif

#if PROTOCOL_VERSION < 719 /* < 1.16 */
    size_t World::GetDimIndex(const Dimension dim)
#else
    size_t World::GetDimIndex(const std::string& dim)
#endif
    {
        auto it = dimension_index_map.find(dim);

        if (it == dimension_index_map.end())
        {
            index_dimension_map.insert({ dimension_index_map.size(), dim });
            it = dimension_index_map.insert({ dim, dimension_index_map.size() }).first;
        }
        return it->second;
    }
} // Botcraft
