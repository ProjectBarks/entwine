/******************************************************************************
* Copyright (c) 2018, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#pragma once

#include <memory>
#include <string>

#include <entwine/new-reader/new-cache.hpp>
#include <entwine/new-reader/hierarchy-reader.hpp>
#include <entwine/new-reader/new-query.hpp>
#include <entwine/third/arbiter/arbiter.hpp>
#include <entwine/types/metadata.hpp>
#include <entwine/util/json.hpp>

namespace entwine
{

class NewReader
{
public:
    NewReader(
            std::string data,
            std::string tmp = "",
            std::shared_ptr<NewCache> cache = std::shared_ptr<NewCache>(),
            std::shared_ptr<arbiter::Arbiter> a =
                std::shared_ptr<arbiter::Arbiter>());

    std::vector<char> read(const Json::Value& json) const;

    const Metadata& metadata() const { return m_metadata; }
    const HierarchyReader& hierarchy() const { return m_hierarchy; }
    const arbiter::Endpoint& ep() const { return m_ep; }
    const arbiter::Endpoint& tmp() const { return m_tmp; }
    NewCache& cache() const { return *m_cache; }
    PointPool& pointPool() const { return m_pointPool; }

private:
    std::shared_ptr<arbiter::Arbiter> m_arbiter;
    arbiter::Endpoint m_ep;
    arbiter::Endpoint m_tmp;

    const Metadata m_metadata;
    const HierarchyReader m_hierarchy;

    std::shared_ptr<NewCache> m_cache;
    mutable PointPool m_pointPool;
};

} // namespace entwine

