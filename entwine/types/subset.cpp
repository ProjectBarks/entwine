/******************************************************************************
* Copyright (c) 2018, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/types/subset.hpp>

#include <entwine/types/metadata.hpp>
#include <entwine/util/unique.hpp>

namespace entwine
{

Subset::Subset(const Metadata& m, const Json::Value& json)
    : m_id(json["id"].asUInt64())
    , m_of(json["of"].asUInt64())
    , m_splits(std::log2(m_of) / std::log2(4))
    , m_boundsScaled(m.boundsScaledCubic())
{
    if (!m_id) throw std::runtime_error("Subset IDs should be 1-based.");
    if (m_of <= 1) throw std::runtime_error("Invalid subset range");
    if (m_id > m_of) throw std::runtime_error("Invalid subset ID - too large.");

    if (static_cast<uint64_t>(std::pow(2, std::log2(m_of))) != m_of)
    {
        throw std::runtime_error("Subset range must be a power of 2");
    }

    if (std::pow(std::sqrt(m_of), 2) != m_of)
    {
        throw std::runtime_error("Subset range must be a perfect square");
    }

    // Always split only X-Y range, leaving Z at its full extents.
    const uint64_t mask(0x3);
    for (std::size_t i(0); i < m_splits; ++i)
    {
        const Dir dir(toDir(((m_id - 1) >> (i * 2)) & mask));
        m_boundsScaled.go(dir, true);
    }

    m_boundsNative = m_boundsScaled.undeltify(m.delta());
}

std::unique_ptr<Subset> Subset::create(const Metadata& m, const Json::Value& j)
{
    if (j.isNull()) return std::unique_ptr<Subset>();
    else return makeUnique<Subset>(m, j);
}

} // namespace entwine

