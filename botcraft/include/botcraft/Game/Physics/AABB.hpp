#pragma once

#include "botcraft/Game/Vector3.hpp"

namespace Botcraft
{
    class AABB
    {
    public:
        AABB(const Vector3<double>& center_, const Vector3<double>& half_size_);
        ~AABB();
        Vector3<double> GetMin() const;
        Vector3<double> GetMax() const;
        const Vector3<double>& GetCenter() const;
        const Vector3<double>& GetHalfSize() const;
        /// @brief Get the closest point in the AABB from a given point
        /// @param pos The reference point
        /// @return Closest point to pos inside the AABB
        Vector3<double> GetClosestPoint(const Vector3<double>& pos) const;
        /// @brief Get the volume of this AABB
        /// @return size_x * size_y * size_z
        double GetVolume() const;

        //Return whether or not these two AABB collides
        bool Collide(const AABB& b) const;

        bool Intersect(const Vector3<double>& origin, const Vector3<double>& direction) const;

        AABB& Inflate(const double d);
        AABB& Translate(const Vector3<double>& t);

        bool operator<(const AABB& other) const;
        bool operator==(const AABB& other) const;

        template <typename T>
        friend AABB operator+(AABB box, const Vector3<T>& offset)
        {
            box.center.x += offset.x;
            box.center.y += offset.y;
            box.center.z += offset.z;

            return box;
        }


    private:
        Vector3<double> center;
        Vector3<double> half_size;
    };
} // Botcraft