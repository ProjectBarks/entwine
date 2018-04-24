/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/types/manifest.hpp>

#include <algorithm>
#include <iostream>
#include <limits>

#include <entwine/types/bounds.hpp>
#include <entwine/util/json.hpp>
#include <entwine/util/pool.hpp>
#include <entwine/util/io.hpp>
#include <entwine/util/json.hpp>
#include <entwine/util/unique.hpp>

namespace entwine
{

namespace
{
    void error(std::string message)
    {
        throw std::runtime_error(message);
    }

    const std::size_t denseSize(50);
    const std::size_t chunkSize(100);
}

void Files::save(const arbiter::Endpoint& ep) const
{
    const Json::Value json(toJson(m_files));
    io::ensurePut(ep, "entwine-files.json", json.toStyledString());
}

void Files::append(const FileInfoList& fileInfo)
{
    FileInfoList adding(diff(fileInfo));
    for (const auto& f : adding) m_files.emplace_back(f);
}

FileInfoList Files::diff(const FileInfoList& in) const
{
    FileInfoList out;
    for (const auto& f : in)
    {
        auto matches([&f](const FileInfo& x) { return f.path() == x.path(); });

        if (std::none_of(m_files.begin(), m_files.end(), matches))
        {
            out.emplace_back(f);
        }
    }

    return out;
}


Manifest::Manifest(
        const FileInfoList& fileInfo,
        const arbiter::Endpoint* endpoint)
    : m_fileInfo(fileInfo)
{ }

/*
Manifest::Manifest(Json::Value json, const arbiter::Endpoint* endpoint)
    : m_endpoint(maybeClone(endpoint))
{
    if (json.isArray())
    {
        Json::Value o;
        o["fileInfo"] = json;
        json = o;
    }

    if (!json.isObject())
    {
        throw std::runtime_error(
                "Invalid manifest JSON: " + json.toStyledString());
    }

    const Json::Value& fileInfo(json["fileInfo"]);
    if (fileInfo.isArray() && fileInfo.size())
    {
        m_fileInfo.reserve(fileInfo.size());
        for (Json::ArrayIndex i(0); i < fileInfo.size(); ++i)
        {
            Json::Value fileJson(fileInfo[i]);
            if (fileJson.isObject()) fileJson["origin"] = i;
            m_fileInfo.emplace_back(fileJson);
        }
    }

    m_chunkSize = json["chunkSize"].asUInt64();
    if (!m_chunkSize) m_chunkSize = chunkSize;

    const bool remote(json["remote"].asBool());
    m_remote.resize(m_fileInfo.size(), remote);

    if (json.isObject())
    {
        // If we have fileStats and pointStats, then we're dealing with a full
        // manifest from Manifest::toJson (a previous build).  Otherwise, we
        // have a simplified manifest from Manifest::toInferenceJson.
        if (json.isMember("fileStats") && json.isMember("pointStats"))
        {
            // Full manifest from Manifest::toJson.
            m_fileStats = FileStats(json["fileStats"]);
            m_pointStats = PointStats(json["pointStats"]);
        }
    }
}

Manifest::Manifest(const Manifest& other)
    : m_fileInfo(other.m_fileInfo)
    , m_remote(other.m_remote)
    , m_fileStats(other.m_fileStats)
    , m_pointStats(other.m_pointStats)
    , m_endpoint(maybeClone(other.m_endpoint.get()))
    , m_chunkSize(other.m_chunkSize)
{ }
*/

Origin Manifest::find(const std::string& search) const
{
    for (std::size_t i(0); i < size(); ++i)
    {
        if (m_fileInfo[i].path().find(search) != std::string::npos) return i;
    }

    return invalidOrigin;
}

OriginList Manifest::find(const Bounds& bounds) const
{
    OriginList origins;

    for (std::size_t i(0); i < size(); ++i)
    {
        if (const auto b = m_fileInfo[i].bounds())
        {
            if (b->overlaps(bounds)) origins.push_back(i);
        }
    }

    return origins;
}

void Manifest::append(const FileInfoList& fileInfo)
{
    FileInfoList adding(diff(fileInfo));

    for (const auto& f : adding)
    {
        m_fileInfo.emplace_back(f);
        m_remote.push_back(false);
    }
}

FileInfoList Manifest::diff(const FileInfoList& in) const
{
    FileInfoList out;
    for (const auto& f : in)
    {
        auto matches([&f](const FileInfo& x) { return f.path() == x.path(); });

        if (std::none_of(m_fileInfo.begin(), m_fileInfo.end(), matches))
        {
            out.emplace_back(f);
        }
    }

    return out;
}

void Manifest::merge(const Manifest& other)
{
    if (size() != other.size()) error("Invalid manifest sizes for merging.");

    FileStats fileStats;

    for (std::size_t i(0); i < size(); ++i)
    {
        FileInfo& ours(m_fileInfo[i]);
        const FileInfo& theirs(other.m_fileInfo[i]);

        if (ours.path() != theirs.path()) error("Invalid manifest paths");

        if (
                ours.status() == FileInfo::Status::Outstanding &&
                theirs.status() != FileInfo::Status::Outstanding)
        {
            ours.status(theirs.status());

            switch (theirs.status())
            {
                case FileInfo::Status::Inserted: fileStats.addInsert(); break;
                case FileInfo::Status::Omitted: fileStats.addOmit(); break;
                case FileInfo::Status::Error: fileStats.addError(); break;
                default: throw std::runtime_error("Invalid file status");
            }
        }

        ours.pointStats().add(theirs.pointStats());
    }

    m_pointStats.add(other.pointStats());
    m_fileStats.add(fileStats);
}

/*
Json::Value Manifest::toJson() const
{
    Json::Value json;

    Json::Value& fileInfo(json["fileInfo"]);
    fileInfo.resize(size());

    for (std::size_t i(0); i < size(); ++i)
    {
        const FileInfo& info(m_fileInfo[i]);
        Json::Value& value(fileInfo[static_cast<Json::ArrayIndex>(i)]);

        value = info.toJson();
    }

    if (!m_fileStats.empty())   json["fileStats"] = m_fileStats.toJson();
    if (!m_pointStats.empty())  json["pointStats"] = m_pointStats.toJson();

    return json;
}
*/

void Manifest::awakenAll(Pool& pool) const
{
    for (std::size_t i(0); i < m_fileInfo.size(); i += m_chunkSize)
    {
        pool.add([this, i]() { awaken(i); });
    }

    pool.cycle();

    if (std::any_of(
                m_remote.begin(),
                m_remote.end(),
                [](bool b) { return b; }))
    {
        throw std::runtime_error("Invalid remote manifest");
    }
}

void Manifest::awaken(Origin origin) const
{
    /*
    if (!m_remote.at(origin)) return;
    // if (!m_endpoint) return;

    const std::size_t chunk(origin / m_chunkSize * m_chunkSize);
    const auto m(m_endpoint->getSubEndpoint("m"));
    const auto bytes(io::ensureGet(m, std::to_string(chunk)));
    const auto json(parse(bytes->data()));

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!json.isArray())
    {
        throw std::runtime_error(
                "Invalid file-info chunk - expected array: " +
                json.toStyledString());
    }
    else if (json.size() != std::min(m_chunkSize, size() - chunk))
    {
        throw std::runtime_error("Invalid file-info chunk - unexpected size");
    }

    std::size_t i(chunk);
    for (const auto& f : json)
    {
        m_fileInfo.at(i) = FileInfo(f);
        m_remote.at(i) = false;
        ++i;
    }
    */
}

void Manifest::save(const arbiter::Endpoint& ep) const
{
    const Json::Value json(toJson(m_fileInfo));
    io::ensurePut(ep, "entwine-files.json", json.toStyledString());
}

/*
void Manifest::save(const bool primary, const std::string postfix) const
{
    if (!m_endpoint) throw std::runtime_error("No endpoint to save manifest");
    auto m(m_endpoint->getSubEndpoint("m"));

    Json::Value json;
    json["fileStats"] = m_fileStats.toJson();
    json["pointStats"] = m_pointStats.toJson();
    Json::Value& fileInfo(json["fileInfo"]);

    // If we have a postfix (and therefore we're a subset), we'll just write
    // everything out together even if it's huge.  The split-up metadata is a
    // read-time optimization - we'll need to wake everything up to merge at
    // build time anyway.
    const auto n(size());
    const bool dense(n <= denseSize || postfix.size());
    fileInfo.resize(n);

    if (dense || (m.isLocal() && !arbiter::fs::mkdirp(m.root())))
    {
        for (Json::ArrayIndex i(0); i < n; ++i)
        {
            fileInfo[i] = m_fileInfo[i].toJson(primary);
        }
    }
    else
    {
        assert(postfix.empty());

        for (Json::ArrayIndex i(0); i < n; ++i)
        {
            // We're storing the file info separately, so the "fileInfo" key
            // will just contain path/bounds instead of the full info object.
            json["remote"] = true;
            json["chunkSize"] = static_cast<Json::UInt64>(chunkSize);
            fileInfo[i]["path"] = m_fileInfo[i].path();

            if (const auto b = m_fileInfo[i].bounds())
            {
                fileInfo[i]["bounds"] = b->toJson();
            }
        }

        // TODO Could pool these.
        for (Json::ArrayIndex i(0); i < n; i += chunkSize)
        {
            Json::Value chunk;
            chunk.resize(std::min(chunkSize, n - i));

            for (Json::ArrayIndex c(0); c < chunk.size(); ++c)
            {
                chunk[c] = m_fileInfo[i + c].toJson(primary);
            }

            io::ensurePut(m, std::to_string(i), chunk.toStyledString());
        }
    }

    io::ensurePut(
            *m_endpoint,
            "entwine-files" + postfix + ".json",
            primary ? json.toStyledString() : toFastString(json));
}
*/

} // namespace entwine

