#include "botcraft/Game/Entities/entities/animal/horse/LlamaEntity.hpp"

#include <mutex>

namespace Botcraft
{
    const std::array<std::string, LlamaEntity::metadata_count> LlamaEntity::metadata_names{ {
        "data_strength_id",
#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
        "data_swag_id",
#endif
        "data_variant_id",
    } };

    LlamaEntity::LlamaEntity()
    {
        // Initialize all metadata with default values
        SetDataStrengthId(0);
#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
        SetDataSwagId(-1);
#endif
        SetDataVariantId(0);

        // Initialize all attributes with default values
#if PROTOCOL_VERSION < 768 /* < 1.21.2 */
        attributes.insert({ EntityAttribute::Type::FollowRange, EntityAttribute(EntityAttribute::Type::FollowRange, 40.0) });
#endif
    }

    LlamaEntity::~LlamaEntity()
    {

    }


    std::string LlamaEntity::GetName() const
    {
        return "llama";
    }

    EntityType LlamaEntity::GetType() const
    {
        return EntityType::Llama;
    }


    std::string LlamaEntity::GetClassName()
    {
        return "llama";
    }

    EntityType LlamaEntity::GetClassType()
    {
        return EntityType::Llama;
    }


    ProtocolCraft::Json::Value LlamaEntity::Serialize() const
    {
        ProtocolCraft::Json::Value output = AbstractChestedHorseEntity::Serialize();

        output["metadata"]["data_strength_id"] = GetDataStrengthId();
#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
        output["metadata"]["data_swag_id"] = GetDataSwagId();
#endif
        output["metadata"]["data_variant_id"] = GetDataVariantId();

        return output;
    }


    void LlamaEntity::SetMetadataValue(const int index, const std::any& value)
    {
        if (index < hierarchy_metadata_count)
        {
            AbstractChestedHorseEntity::SetMetadataValue(index, value);
        }
        else if (index - hierarchy_metadata_count < metadata_count)
        {
            std::scoped_lock<std::shared_mutex> lock(entity_mutex);
            metadata[metadata_names[index - hierarchy_metadata_count]] = value;
        }
    }

    int LlamaEntity::GetDataStrengthId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_strength_id"));
    }

#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
    int LlamaEntity::GetDataSwagId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_swag_id"));
    }
#endif

    int LlamaEntity::GetDataVariantId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_variant_id"));
    }


    void LlamaEntity::SetDataStrengthId(const int data_strength_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_strength_id"] = data_strength_id;
    }

#if PROTOCOL_VERSION < 766 /* < 1.20.5 */
    void LlamaEntity::SetDataSwagId(const int data_swag_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_swag_id"] = data_swag_id;
    }
#endif

    void LlamaEntity::SetDataVariantId(const int data_variant_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_variant_id"] = data_variant_id;
    }


    double LlamaEntity::GetWidthImpl() const
    {
        return 0.9;
    }

    double LlamaEntity::GetHeightImpl() const
    {
        return 1.87;
    }

}
