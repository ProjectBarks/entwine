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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include <entwine/third/arbiter/arbiter.hpp>
#include <entwine/types/defs.hpp>
#include <entwine/types/file-info.hpp>
#include <entwine/types/stats.hpp>

namespace entwine
{

class Bounds;
class Pool;

class Files
{
public:
    Files(const FileInfoList& files)
        : m_files(files)
    {
        for (const auto& f : m_files)
        {
            m_pointStats += f.pointStats();
            addStatus(f.status());
            m_totalPoints += f.numPoints();
        }
    }

    Files(const Json::Value& json) : Files(toFileInfo(json)) { }

    void save(const arbiter::Endpoint& ep, const std::string& postfix) const;

    std::size_t size() const { return m_files.size(); }

    Origin find(const std::string& p) const
    {
        for (std::size_t i(0); i < size(); ++i)
        {
            if (m_files[i].path().find(p) != std::string::npos) return i;
        }

        return invalidOrigin;
    }

    FileInfo& get(Origin o) { return m_files.at(o); }
    const FileInfo& get(Origin o) const { return m_files.at(o); }

    void set(Origin o, FileInfo::Status status, std::string message = "")
    {
        addStatus(status);
        get(o).status(status, message);
    }

    void add(Origin origin, const PointStats& stats)
    {
        get(origin).add(stats);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pointStats.add(stats);
    }

    void addOutOfBounds(Origin origin, std::size_t count, bool primary)
    {
        get(origin).pointStats().addOutOfBounds(count);
        if (primary) m_pointStats.addOutOfBounds(count);
    }

    const FileInfoList& list() const { return m_files; }
    const PointStats& pointStats() const { return m_pointStats; }
    const FileStats& fileStats() const { return m_fileStats; }

    FileInfoList diff(const FileInfoList& fileInfo) const;
    void append(const FileInfoList& fileInfo);

    std::size_t totalPoints() const
    {
        std::size_t n(0);
        for (const auto& f : m_files) n += f.numPoints();
        return n;
    }

    void merge(const Files& other);

private:
    void addStatus(FileInfo::Status status)
    {
        switch (status)
        {
            case FileInfo::Status::Inserted:    m_fileStats.addInsert(); break;
            case FileInfo::Status::Omitted:     m_fileStats.addOmit(); break;
            case FileInfo::Status::Error:       m_fileStats.addError(); break;
            default: break;
        }
    }

    FileInfoList m_files;
    uint64_t m_totalPoints = 0;

    mutable std::mutex m_mutex;
    PointStats m_pointStats;
    FileStats m_fileStats;
};

} // namespace entwine

