#if PROTOCOL_VERSION > 340 /* > 1.12.2 */
#include "botcraft/Game/Entities/entities/animal/TurtleEntity.hpp"

#include <mutex>

namespace Botcraft
{
    const std::array<std::string, TurtleEntity::metadata_count> TurtleEntity::metadata_names{ {
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        "home_pos",
#endif
        "has_egg",
        "laying_egg",
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        "travel_pos",
        "going_home",
        "travelling",
#endif
    } };

    TurtleEntity::TurtleEntity()
    {
        // Initialize all metadata with default values
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        SetHomePos(Position(0, 0, 0));
#endif
        SetHasEgg(false);
        SetLayingEgg(false);
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        SetTravelPos(Position(0, 0, 0));
        SetGoingHome(false);
        SetTravelling(false);
#endif

        // Initialize all attributes with default values
        attributes.insert({ EntityAttribute::Type::MaxHealth, EntityAttribute(EntityAttribute::Type::MaxHealth, 30.0) });
        attributes.insert({ EntityAttribute::Type::MovementSpeed, EntityAttribute(EntityAttribute::Type::MovementSpeed, 0.25) });
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
        attributes.insert({ EntityAttribute::Type::StepHeight, EntityAttribute(EntityAttribute::Type::StepHeight, 1.0) });
#endif
    }

    TurtleEntity::~TurtleEntity()
    {

    }


    std::string TurtleEntity::GetName() const
    {
        return "turtle";
    }

    EntityType TurtleEntity::GetType() const
    {
        return EntityType::Turtle;
    }


    std::string TurtleEntity::GetClassName()
    {
        return "turtle";
    }

    EntityType TurtleEntity::GetClassType()
    {
        return EntityType::Turtle;
    }


    ProtocolCraft::Json::Value TurtleEntity::Serialize() const
    {
        ProtocolCraft::Json::Value output = AnimalEntity::Serialize();

#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        output["metadata"]["home_pos"] = GetHomePos().Serialize();
#endif
        output["metadata"]["has_egg"] = GetHasEgg();
        output["metadata"]["laying_egg"] = GetLayingEgg();
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
        output["metadata"]["travel_pos"] = GetTravelPos().Serialize();
        output["metadata"]["going_home"] = GetGoingHome();
        output["metadata"]["travelling"] = GetTravelling();
#endif

        return output;
    }


    void TurtleEntity::SetMetadataValue(const int index, const std::any& value)
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

#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
    Position TurtleEntity::GetHomePos() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<Position>(metadata.at("home_pos"));
    }
#endif

    bool TurtleEntity::GetHasEgg() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<bool>(metadata.at("has_egg"));
    }

    bool TurtleEntity::GetLayingEgg() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<bool>(metadata.at("laying_egg"));
    }

#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
    Position TurtleEntity::GetTravelPos() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<Position>(metadata.at("travel_pos"));
    }

    bool TurtleEntity::GetGoingHome() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<bool>(metadata.at("going_home"));
    }

    bool TurtleEntity::GetTravelling() const
    {
        std::shared_lock<std::shared_mutex> lock(entity_mutex);
        return std::any_cast<bool>(metadata.at("travelling"));
    }
#endif


#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
    void TurtleEntity::SetHomePos(const Position& home_pos)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["home_pos"] = home_pos;
    }
#endif

    void TurtleEntity::SetHasEgg(const bool has_egg)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["has_egg"] = has_egg;
    }

    void TurtleEntity::SetLayingEgg(const bool laying_egg)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["laying_egg"] = laying_egg;
    }

#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
    void TurtleEntity::SetTravelPos(const Position& travel_pos)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["travel_pos"] = travel_pos;
    }

    void TurtleEntity::SetGoingHome(const bool going_home)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["going_home"] = going_home;
    }

    void TurtleEntity::SetTravelling(const bool travelling)
    {
        std::scoped_lock<std::shared_mutex> lock(entity_mutex);
        metadata["travelling"] = travelling;
    }
#endif


    double TurtleEntity::GetWidthImpl() const
    {
        return 1.2;
    }

    double TurtleEntity::GetHeightImpl() const
    {
        return 0.4;
    }

}
#endif
