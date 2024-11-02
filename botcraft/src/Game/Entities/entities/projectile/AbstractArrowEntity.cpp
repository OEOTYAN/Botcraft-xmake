#include "botcraft/Game/Entities/entities/projectile/AbstractArrowEntity.hpp"

#include <mutex>

namespace Botcraft
{
    const std::array<std::string, AbstractArrowEntity::metadata_count> AbstractArrowEntity::metadata_names{ {
        "id_flags",
#if PROTOCOL_VERSION < 579 /* < 1.16 */ && PROTOCOL_VERSION > 393 /* > 1.13 */
        "data_owneruuid_id",
#endif
#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
        "pierce_level",
#endif
#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
        "in_ground",
#endif
    } };

    AbstractArrowEntity::AbstractArrowEntity()
    {
        // Initialize all metadata with default values
        SetIdFlags(0);
#if PROTOCOL_VERSION < 579 /* < 1.16 */ && PROTOCOL_VERSION > 393 /* > 1.13 */
        SetDataOwneruuidId(std::optional<ProtocolCraft::UUID>());
#endif
#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
        SetPierceLevel(0);
#endif
#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
        SetInGround(false);
#endif
    }

    AbstractArrowEntity::~AbstractArrowEntity()
    {

    }

    bool AbstractArrowEntity::IsAbstractArrow() const
    {
        return true;
    }


    ProtocolCraft::Json::Value AbstractArrowEntity::Serialize() const
    {
#if PROTOCOL_VERSION > 578 /* > 1.15.2 */
        ProtocolCraft::Json::Value output = ProjectileEntity::Serialize();
#else
        ProtocolCraft::Json::Value output = Entity::Serialize();
#endif

        output["metadata"]["id_flags"] = GetIdFlags();
#if PROTOCOL_VERSION < 579 /* < 1.16 */ && PROTOCOL_VERSION > 393 /* > 1.13 */
        output["metadata"]["data_owneruuid_id"] = GetDataOwneruuidId() ? ProtocolCraft::Json::Value(GetDataOwneruuidId().value()) : ProtocolCraft::Json::Value();
#endif
#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
        output["metadata"]["pierce_level"] = GetPierceLevel();
#endif
#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
        output["metadata"]["in_ground"] = GetInGround();
#endif

        return output;
    }


    void AbstractArrowEntity::SetMetadataValue(const int index, const std::any& value)
    {
        if (index < hierarchy_metadata_count)
        {
#if PROTOCOL_VERSION > 578 /* > 1.15.2 */
            ProjectileEntity::SetMetadataValue(index, value);
#else
            Entity::SetMetadataValue(index, value);
#endif
        }
        else if (index - hierarchy_metadata_count < metadata_count)
        {
            std::scoped_lock<std::shared_mutex> lock(entity_mutex);
            metadata[metadata_names[index - hierarchy_metadata_count]] = value;
        }
    }

    char AbstractArrowEntity::GetIdFlags() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<char>(metadata.at("id_flags"));
    }

#if PROTOCOL_VERSION < 579 /* < 1.16 */ && PROTOCOL_VERSION > 393 /* > 1.13 */
    std::optional<ProtocolCraft::UUID> AbstractArrowEntity::GetDataOwneruuidId() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<std::optional<ProtocolCraft::UUID>>(metadata.at("data_owneruuid_id"));
    }
#endif

#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
    char AbstractArrowEntity::GetPierceLevel() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<char>(metadata.at("pierce_level"));
    }
#endif

#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
    bool AbstractArrowEntity::GetInGround() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<bool>(metadata.at("in_ground"));
    }
#endif


    void AbstractArrowEntity::SetIdFlags(const char id_flags)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["id_flags"] = id_flags;
    }

#if PROTOCOL_VERSION < 579 /* < 1.16 */ && PROTOCOL_VERSION > 393 /* > 1.13 */
    void AbstractArrowEntity::SetDataOwneruuidId(const std::optional<ProtocolCraft::UUID>& data_owneruuid_id)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["data_owneruuid_id"] = data_owneruuid_id;
    }
#endif

#if PROTOCOL_VERSION > 404 /* > 1.13.2 */
    void AbstractArrowEntity::SetPierceLevel(const char pierce_level)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["pierce_level"] = pierce_level;
    }
#endif

#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
    void AbstractArrowEntity::SetInGround(const bool in_ground)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["in_ground"] = in_ground;
    }
#endif

}
