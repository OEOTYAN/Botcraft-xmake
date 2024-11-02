#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
#include "botcraft/Game/Entities/entities/animal/FoxEntity.hpp"

#include <mutex>

namespace Botcraft
{
    const std::array<std::string, FoxEntity::metadata_count> FoxEntity::metadata_names{ {
        "data_type_id",
        "data_flags_id",
        "data_trusted_id_0",
        "data_trusted_id_1",
    } };

    FoxEntity::FoxEntity()
    {
        // Initialize all metadata with default values
        SetDataTypeId(0);
        SetDataFlagsId(0);
        SetDataTrustedId0(std::optional<ProtocolCraft::UUID>());
        SetDataTrustedId1(std::optional<ProtocolCraft::UUID>());

        // Initialize all attributes with default values
        attributes.insert({ EntityAttribute::Type::MovementSpeed, EntityAttribute(EntityAttribute::Type::MovementSpeed, 0.3) });
        attributes.insert({ EntityAttribute::Type::MaxHealth, EntityAttribute(EntityAttribute::Type::MaxHealth, 10.0) });
        attributes.insert({ EntityAttribute::Type::FollowRange, EntityAttribute(EntityAttribute::Type::FollowRange, 32.0) });
        attributes.insert({ EntityAttribute::Type::AttackDamage, EntityAttribute(EntityAttribute::Type::AttackDamage, 2.0) });
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
        attributes.insert({ EntityAttribute::Type::SafeFallDistance, EntityAttribute(EntityAttribute::Type::SafeFallDistance, 5.0) });
#endif
    }

    FoxEntity::~FoxEntity()
    {

    }


    std::string FoxEntity::GetName() const
    {
        return "fox";
    }

    EntityType FoxEntity::GetType() const
    {
        return EntityType::Fox;
    }


    std::string FoxEntity::GetClassName()
    {
        return "fox";
    }

    EntityType FoxEntity::GetClassType()
    {
        return EntityType::Fox;
    }


    ProtocolCraft::Json::Value FoxEntity::Serialize() const
    {
        ProtocolCraft::Json::Value output = AnimalEntity::Serialize();

        output["metadata"]["data_type_id"] = GetDataTypeId();
        output["metadata"]["data_flags_id"] = GetDataFlagsId();
        output["metadata"]["data_trusted_id_0"] = GetDataTrustedId0() ? ProtocolCraft::Json::Value(GetDataTrustedId0().value()) : ProtocolCraft::Json::Value();
        output["metadata"]["data_trusted_id_1"] = GetDataTrustedId1() ? ProtocolCraft::Json::Value(GetDataTrustedId1().value()) : ProtocolCraft::Json::Value();

        output["attributes"]["attack_damage"] = GetAttributeAttackDamageValue();


        return output;
    }


    void FoxEntity::SetMetadataValue(const int index, const std::any& value)
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

    int FoxEntity::GetDataTypeId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<int>(metadata.at("data_type_id"));
    }

    char FoxEntity::GetDataFlagsId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<char>(metadata.at("data_flags_id"));
    }

    std::optional<ProtocolCraft::UUID> FoxEntity::GetDataTrustedId0() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<std::optional<ProtocolCraft::UUID>>(metadata.at("data_trusted_id_0"));
    }

    std::optional<ProtocolCraft::UUID> FoxEntity::GetDataTrustedId1() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<std::optional<ProtocolCraft::UUID>>(metadata.at("data_trusted_id_1"));
    }


    void FoxEntity::SetDataTypeId(const int data_type_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_type_id"] = data_type_id;
    }

    void FoxEntity::SetDataFlagsId(const char data_flags_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_flags_id"] = data_flags_id;
    }

    void FoxEntity::SetDataTrustedId0(const std::optional<ProtocolCraft::UUID>& data_trusted_id_0)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_trusted_id_0"] = data_trusted_id_0;
    }

    void FoxEntity::SetDataTrustedId1(const std::optional<ProtocolCraft::UUID>& data_trusted_id_1)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_trusted_id_1"] = data_trusted_id_1;
    }


    double FoxEntity::GetAttributeAttackDamageValue() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return attributes.at(EntityAttribute::Type::AttackDamage).GetValue();
    }


    double FoxEntity::GetWidthImpl() const
    {
        return 0.6;
    }

    double FoxEntity::GetHeightImpl() const
    {
        return 0.7;
    }

}
#endif
