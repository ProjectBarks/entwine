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

#include <entwine/new-reader/query-params.hpp>

#include <entwine/new-reader/filter.hpp>
#include <entwine/new-reader/hierarchy-reader.hpp>
#include <entwine/new-reader/new-chunk-reader.hpp>
#include <entwine/types/binary-point-table.hpp>
#include <entwine/types/key.hpp>
#include <entwine/types/schema.hpp>

namespace entwine
{

class NewReader;

class NewQuery
{
public:
    NewQuery(const NewReader& reader, const NewQueryParams& params);
    virtual ~NewQuery() { }

    void run();

    uint64_t numPoints() const { return m_numPoints; }

protected:
    virtual void process(const Cell& cell) { }

    const NewReader& m_reader;
    const Metadata& m_metadata;
    const HierarchyReader& m_hierarchy;
    const NewQueryParams m_params;
    const Filter m_filter;

    BinaryPointTable m_table;
    pdal::PointRef m_pointRef;

// private:
    HierarchyReader::Keys overlaps() const;
    void overlaps(HierarchyReader::Keys& keys, const ChunkKey& c) const;

    void maybeProcess(const Cell& cell);

    HierarchyReader::Keys m_overlaps;
    uint64_t m_numPoints = 0;
    // std::vector<NewChunkReader> m_chunks;    // TODO Hook up cache.
};

class NewCountQuery : public NewQuery
{
public:
    NewCountQuery(const NewReader& reader, const NewQueryParams& params)
        : NewQuery(reader, params)
    { }
};

class NewReadQuery : public NewQuery
{
public:
    NewReadQuery(
            const NewReader& reader,
            const NewQueryParams& params,
            const Schema& schema)
        : NewQuery(reader, params)
        , m_schema(schema.empty() ? m_metadata.schema() : schema)
        , m_mid(m_params.nativeBounds() ?
                m_params.delta().offset() :
                m_metadata.boundsScaledCubic().mid())
    { }

    const std::vector<char>& data() const { return m_data; }

protected:
    virtual void process(const Cell& cell) override;

private:
    void setScaled(const DimInfo& dim, std::size_t dimNum, char* pos)
    {
        double d(0);
        if (m_params.nativeBounds())
        {
            d = Point::unscale(
                    m_pointRef.getFieldAs<double>(dim.id()),
                    m_metadata.delta()->scale()[dimNum],
                    m_metadata.delta()->offset()[dimNum]);

            d = Point::scale(
                    d,
                    m_params.delta().scale()[dimNum],
                    m_params.delta().offset()[dimNum]);
        }
        else
        {
            d = Point::scale(
                    m_pointRef.getFieldAs<double>(dim.id()),
                    m_mid[dimNum],
                    m_params.delta().scale()[dimNum],
                    m_params.delta().offset()[dimNum]);
        }

        switch (dim.type())
        {
            case pdal::Dimension::Type::Double:     setAs<double>(pos, d);
                break;
            case pdal::Dimension::Type::Float:      setAs<float>(pos, d);
                break;
            case pdal::Dimension::Type::Unsigned8:  setAs<uint8_t>(pos, d);
                break;
            case pdal::Dimension::Type::Signed8:    setAs<int8_t>(pos, d);
                break;
            case pdal::Dimension::Type::Unsigned16: setAs<uint16_t>(pos, d);
                break;
            case pdal::Dimension::Type::Signed16:   setAs<int16_t>(pos, d);
                break;
            case pdal::Dimension::Type::Unsigned32: setAs<uint32_t>(pos, d);
                break;
            case pdal::Dimension::Type::Signed32:   setAs<int32_t>(pos, d);
                break;
            case pdal::Dimension::Type::Unsigned64: setAs<uint64_t>(pos, d);
                break;
            case pdal::Dimension::Type::Signed64:   setAs<int64_t>(pos, d);
                break;
            default:
                break;
        }
    }

    template<typename T> void setAs(char* dst, double d)
    {
        const T v(d);
        auto src(reinterpret_cast<const char*>(&v));
        std::copy(src, src + sizeof(T), dst);
    }

    const Schema m_schema;
    const Point m_mid;

    std::vector<char> m_data;
};

} // namespace entwine

