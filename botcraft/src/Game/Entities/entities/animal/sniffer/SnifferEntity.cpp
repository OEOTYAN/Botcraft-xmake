#if PROTOCOL_VERSION > 761 /* > 1.19.3 */
#include "botcraft/Game/Entities/entities/animal/sniffer/SnifferEntity.hpp"

#include <mutex>

namespace Botcraft
{
    const std::array<std::string, SnifferEntity::metadata_count> SnifferEntity::metadata_names{ {
        "data_state",
        "data_drop_seed_at_tick",
    } };

    SnifferEntity::SnifferEntity()
    {
        // Initialize all metadata with default values
        SetDataState(0);
        SetDataDropSeedAtTick(0);

        // Initialize all attributes with default values
        attributes.insert({ EntityAttribute::Type::MovementSpeed, EntityAttribute(EntityAttribute::Type::MovementSpeed, 0.1) });
        attributes.insert({ EntityAttribute::Type::MaxHealth, EntityAttribute(EntityAttribute::Type::MaxHealth, 14.0) });
    }

    SnifferEntity::~SnifferEntity()
    {

    }


    std::string SnifferEntity::GetName() const
    {
        return "sniffer";
    }

    EntityType SnifferEntity::GetType() const
    {
        return EntityType::Sniffer;
    }


    std::string SnifferEntity::GetClassName()
    {
        return "sniffer";
    }

    EntityType SnifferEntity::GetClassType()
    {
        return EntityType::Sniffer;
    }



    ProtocolCraft::Json::Value SnifferEntity::Serialize() const
    {
        ProtocolCraft::Json::Value output = AnimalEntity::Serialize();

        output["metadata"]["data_state"] = GetDataState();
        output["metadata"]["data_drop_seed_at_tick"] = GetDataDropSeedAtTick();

        return output;
    }


    void SnifferEntity::SetMetadataValue(const int index, const std::any& value)
    {
        if (index < hierarchy_metadata_count)
        {
            AnimalEntity::SetMetadataValue(index, value);
        }
        else if (index - hierarchy_metadata_count < metadata_count)
        {
            std::scoped_lock<std::shared_mutex> lock(entity_mutex);
            metadata[metadata_names[index - hierarchy_metadata_count]] = value;
        }
    }


    int SnifferEntity::GetDataState() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_state"));
    }

    int SnifferEntity::GetDataDropSeedAtTick() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_drop_seed_at_tick"));
    }


    void SnifferEntity::SetDataState(const int data_state)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_state"] = data_state;
    }

    void SnifferEntity::SetDataDropSeedAtTick(const int data_drop_seed_at_tick)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_drop_seed_at_tick"] = data_drop_seed_at_tick;
    }


    double SnifferEntity::GetWidthImpl() const
    {
        return 1.9;
    }

    double SnifferEntity::GetHeightImpl() const
    {
        return 1.75;
    }

}
#endif
