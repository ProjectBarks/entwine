/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <json/json.h>

#include <entwine/tree/new-climber.hpp>
#include <entwine/tree/new-clipper.hpp>
#include <entwine/tree/cold.hpp>
#include <entwine/tree/slice.hpp>
#include <entwine/types/key.hpp>
#include <entwine/types/point-pool.hpp>
#include <entwine/types/tube.hpp>
#include <entwine/util/unique.hpp>

namespace arbiter
{
    class Endpoint;
}

namespace entwine
{

class NewClimber;
class NewClipper;

class Registry
{
public:
    Registry(
            const Metadata& metadata,
            const arbiter::Endpoint& out,
            const arbiter::Endpoint& tmp,
            PointPool& pointPool,
            bool exists = false);

    void save(const arbiter::Endpoint& endpoint) const;
    void merge(const Registry& other) { } // TODO

    bool addPoint(
            Cell::PooledNode& cell,
            NewClimber& climber,
            NewClipper& clipper,
            std::size_t maxDepth = 0)
    {
        Tube::Insertion attempt;

        while (true)
        {
            auto& slice(m_slices.at(climber.depth()));
            attempt = slice.insert(cell, climber, clipper);

            if (!attempt.done()) climber.step(cell->point());
            else return true;
        }
    }

    void clip(uint64_t d, const Xyz& p, uint64_t o)
    {
        m_slices.at(d).clip(p, o);
    }

    const Metadata& metadata() const { return m_metadata; }

private:
    void loadAsNew();
    void loadFromRemote();

    void flatHierarchy(Json::Value& json, uint64_t d, Xyz p) const;
    void hierarchy(Json::Value& json, uint64_t d, Xyz p) const;

    const Metadata& m_metadata;
    const arbiter::Endpoint& m_out;
    const arbiter::Endpoint& m_tmp;

    std::vector<Slice> m_slices;
};

} // namespace entwine

