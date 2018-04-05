/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/reader/reader.hpp>

#include <algorithm>
#include <numeric>

#include <entwine/reader/cache.hpp>
#include <entwine/reader/chunk-reader.hpp>
#include <entwine/third/arbiter/arbiter.hpp>
#include <entwine/tree/chunk.hpp>
#include <entwine/tree/climber.hpp>
#include <entwine/tree/builder.hpp>
#include <entwine/tree/registry.hpp>
#include <entwine/types/bounds.hpp>
#include <entwine/types/manifest.hpp>
#include <entwine/types/reprojection.hpp>
#include <entwine/types/schema.hpp>
#include <entwine/types/structure.hpp>
#include <entwine/types/subset.hpp>
#include <entwine/util/compression.hpp>
#include <entwine/util/json.hpp>
#include <entwine/util/unique.hpp>

namespace entwine
{

namespace
{
    void checkQuery(std::size_t depthBegin, std::size_t depthEnd)
    {
        if (depthBegin >= depthEnd)
        {
            throw InvalidQuery(
                    "Invalid depths " + std::to_string(depthBegin) +
                    ", " + std::to_string(depthEnd));
        }
    }

    Bounds ensure3d(const Bounds& bounds)
    {
        if (bounds.is3d())
        {
            return bounds;
        }
        else
        {
            return Bounds(
                    Point(
                        bounds.min().x,
                        bounds.min().y,
                        std::numeric_limits<double>::lowest()),
                    Point(
                        bounds.max().x,
                        bounds.max().y,
                        std::numeric_limits<double>::max()));
        }
    }

    const std::size_t basePoolBlockSize(65536);
    // HierarchyCell::Pool hierarchyPool(4096);
}

Reader::Reader(const std::string path, const std::string tmp, Cache& cache)
    : m_ownedArbiter(makeUnique<arbiter::Arbiter>())
    , m_endpoint(m_ownedArbiter->getEndpoint(path))
    , m_tmp(m_ownedArbiter->getEndpoint(tmp))
    , m_metadata(m_endpoint)
    , m_pool(m_metadata.schema(), m_metadata.delta(), basePoolBlockSize)
    , m_cache(cache)
    , m_threadPool(2)
    /*
    , m_hierarchy(
            makeUnique<HierarchyReader>(
                hierarchyPool,
                m_metadata,
                m_endpoint,
                m_cache))
    */
{
    init();
}

Reader::Reader(
        const arbiter::Endpoint& endpoint,
        const arbiter::Endpoint& tmp,
        Cache& cache)
    : m_endpoint(endpoint)
    , m_tmp(tmp)
    , m_metadata(m_endpoint)
    , m_pool(m_metadata.schema(), m_metadata.delta(), basePoolBlockSize)
    , m_cache(cache)
    , m_threadPool(2)
    /*
    , m_hierarchy(
            makeUnique<HierarchyReader>(
                hierarchyPool,
                m_metadata,
                m_endpoint,
                m_cache))
    */
{
    init();
}

void Reader::init()
{
    const Structure& structure(m_metadata.structure());

    if (structure.hasBase())
    {
        m_base = makeUnique<BaseChunkReader>(
                m_metadata,
                m_endpoint,
                m_tmp,
                m_pool);
    }

    if (structure.hasCold())
    {
        m_threadPool->add([&]()
        {
            const auto ids(extractIds(m_endpoint.get("entwine-ids")));
            if (ids.empty()) return;

            std::size_t depth(ChunkInfo::calcDepth(4, ids.front()));
            Id nextDepthIndex(ChunkInfo::calcLevelIndex(2, depth + 1));

            m_ids.resize(ChunkInfo::calcDepth(4, ids.back()) + 1);

            for (const auto& id : ids)
            {
                if (id >= nextDepthIndex)
                {
                    ++depth;
                    nextDepthIndex <<= structure.dimensions();
                    ++nextDepthIndex;
                }

                if (ChunkInfo::calcDepth(4, id) != depth)
                {
                    throw std::runtime_error("Invalid depth");
                }

                m_ids.at(depth).push_back(id);
            }

            std::cout << m_endpoint.prefixedRoot() << " ready" << std::endl;
            m_ready = true;
        });
    }

    if (m_endpoint.tryGetSize("d/dimensions.json"))
    {
        const Json::Value json(parse(m_endpoint.get("d/dimensions.json")));
        for (const auto name : json.getMemberNames())
        {
            try
            {
                registerAppend(name, Schema(json[name]));
            }
            catch (std::exception& e)
            {
                std::cout << e.what() << std::endl;
            }
        }
    }
}

void Reader::registerAppend(std::string name, Schema schema)
{
    if (name.empty())
    {
        throw std::runtime_error("Appended-dimension set name cannot be empty");
    }

    schema = schema.filter("Omit");

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_appends.count(name))
    {
        if (schema != appendAt(name, false))
        {
            throw std::runtime_error(
                    "Cannot change the schema of an existing append set");
        }
    }

    for (const auto& dim : schema.dims())
    {
        if (m_metadata.schema().contains(dim.name()))
        {
            throw std::runtime_error(
                    "Cannot re-register native dimension: " + dim.name());
        }

        const std::string existing(findAppendName(dim.name(), false));
        if (existing.size())
        {
            if (name != existing)
            {
                throw std::runtime_error(
                    "Dimension was already appended: " + dim.name());
            }

            if (schema != appendAt(existing, false))
            {
                throw std::runtime_error(
                        "Cannot re-register this name with a new schema");
            }
        }
    }

    std::cout << "Registering append: " << name << std::endl;

    if (m_endpoint.isLocal())
    {
        arbiter::fs::mkdirp(m_endpoint.root() + "d/" + name);
    }

    m_appends[name] = schema;

    Json::Value json;
    for (const auto& p : m_appends) json[p.first] = p.second.toJson();
    std::cout << "Writing dimensions.json: " << json << std::endl;
    m_endpoint.put("d/dimensions.json", json.toStyledString());
}

std::size_t Reader::write(
        std::string name,
        const std::vector<char>& data,
        const Json::Value& q)
{
    if (data.empty()) return 0;
    std::unique_lock<std::mutex> lock(m_mutex);
    Schema schema(m_appends.at(name));
    lock.unlock();

    Schema requested(q["schema"]);

    // The requested schema must match this addon's schema - with the exception
    // that it may contain an "Omit" dimension for edge-effect buffering.
    if (requested.pointSize())
    {
        if (requested.filter("Omit") != schema)
        {
            throw std::runtime_error("Invalid schema for addon: " + name);
        }

        schema = requested;
    }

    WriteQuery writeQuery(*this, QueryParams(q), name, schema, data);

    writeQuery.run();
    return writeQuery.numPoints();
}

Reader::~Reader() { m_cache.release(*this); }

bool Reader::exists(const QueryChunkState& c) const
{
    if (m_ready)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_threadPool)
        {
            m_threadPool.reset();
            std::cout << m_endpoint.prefixedRoot() << " joined" << std::endl;
        }
        lock.unlock();

        if (c.depth() >= m_ids.size()) return false;
        const auto& slice(m_ids[c.depth()]);
        return std::binary_search(slice.begin(), slice.end(), c.chunkId());
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pre.count(c.chunkId())) return m_pre[c.chunkId()];
        auto& val(m_pre[c.chunkId()]);
        val = false;

        const auto f(m_metadata.filename(c.chunkId()));
        if (const auto size = m_endpoint.tryGetSize(f)) val = *size;
        std::cout << m_endpoint.prefixedRoot() << f << ": " << val << std::endl;
        return val;
    }
}

Json::Value Reader::hierarchy(
        const Bounds& inBounds,
        const std::size_t depthBegin,
        const std::size_t depthEnd,
        const bool vertical,
        const Point* scale,
        const Point* offset)
{
    checkQuery(depthBegin, depthEnd);

    return Json::nullValue;
}

Json::Value Reader::hierarchy(const Json::Value q)
{
    const Bounds bounds = q.isMember("bounds") ?
        Bounds(q["bounds"]) : Bounds::everything();

    const std::size_t depthBegin = q.isMember("depth") ?
        q["depth"].asUInt64() : q["depthBegin"].asUInt64();

    const std::size_t depthEnd = q.isMember("depth") ?
        q["depth"].asUInt64() + 1 : q["depthEnd"].asUInt64();

    const bool vertical(q["vertical"].asBool());

    auto scale(entwine::maybeCreate<entwine::Scale>(q["scale"]));
    auto offset(entwine::maybeCreate<entwine::Offset>(q["offset"]));

    if (depthBegin == depthEnd) std::cout << q << std::endl;

    return hierarchy(
            bounds,
            depthBegin,
            depthEnd,
            vertical,
            scale.get(),
            offset.get());
}

FileInfo Reader::files(const Origin origin) const
{
    return m_metadata.manifest().get(origin);
}

FileInfoList Reader::files(const std::vector<Origin>& origins) const
{
    FileInfoList fileInfo;
    fileInfo.reserve(origins.size());
    for (const auto origin : origins) fileInfo.push_back(files(origin));
    return fileInfo;
}

FileInfo Reader::files(std::string search) const
{
    return files(m_metadata.manifest().find(search));
}

FileInfoList Reader::files(const std::vector<std::string>& searches) const
{
    FileInfoList fileInfo;
    fileInfo.reserve(searches.size());
    for (const auto& search : searches) fileInfo.push_back(files(search));
    return fileInfo;
}

FileInfoList Reader::files(
        const Bounds& queryBounds,
        const Point* scale,
        const Point* offset) const
{
    auto delta(Delta::maybeCreate(scale, offset));
    const Bounds absoluteBounds(
            delta ?
                queryBounds.unscale(delta->scale(), delta->offset()) :
                queryBounds);
    const Bounds absoluteCube(ensure3d(absoluteBounds));
    return files(m_metadata.manifest().find(absoluteCube));
}

Delta Reader::localizeDelta(const Point* scale, const Point* offset) const
{
    const Delta builtInDelta(m_metadata.delta());
    const Delta queryDelta(scale, offset);
    return Delta(
            queryDelta.scale() / builtInDelta.scale(),
            queryDelta.offset() - builtInDelta.offset());
}

Bounds Reader::localize(
        const Bounds& queryBounds,
        const Delta& localDelta) const
{
    if (localDelta.empty() || queryBounds == Bounds::everything())
    {
        return queryBounds;
    }

    const Bounds indexedBounds(m_metadata.boundsScaledCubic());

    const Point queryReferenceCenter(
            Bounds(
                Point::scale(
                    indexedBounds.min(),
                    indexedBounds.mid(),
                    localDelta.scale(),
                    localDelta.offset()),
                Point::scale(
                    indexedBounds.max(),
                    indexedBounds.mid(),
                    localDelta.scale(),
                    localDelta.offset())).mid());

    const Bounds queryTransformed(
            Point::unscale(
                queryBounds.min(),
                Point(),
                localDelta.scale(),
                -queryReferenceCenter),
            Point::unscale(
                queryBounds.max(),
                Point(),
                localDelta.scale(),
                -queryReferenceCenter));

    Bounds queryCube(
            queryTransformed.min() + indexedBounds.mid(),
            queryTransformed.max() + indexedBounds.mid());

    // If the query bounds were 2d, make sure we maintain maximal extents.
    if (
            queryBounds.min().z == Bounds::everything().min().z &&
            queryBounds.max().z == Bounds::everything().max().z)
    {
        queryCube = Bounds(
                Point(
                    queryCube.min().x,
                    queryCube.min().y,
                    Bounds::everything().min().z),
                Point(
                    queryCube.max().x,
                    queryCube.max().y,
                    Bounds::everything().max().z));
    }

    return queryCube;
}

} // namespace entwine

