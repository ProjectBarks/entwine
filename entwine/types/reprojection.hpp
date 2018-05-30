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

#include <stdexcept>
#include <string>

#include <json/json.h>

#include <entwine/util/unique.hpp>

namespace entwine
{

class Reprojection
{
public:
    Reprojection(std::string in, std::string out, bool hammer = false)
        : m_in(in)
        , m_out(out)
        , m_hammer(hammer)
    {
        if (m_out.empty())
        {
            throw std::runtime_error("Empty output projection");
        }

        if (m_hammer && m_in.empty())
        {
            throw std::runtime_error("Hammer option specified without in SRS");
        }
    }

    Reprojection(const Json::Value& json)
        : Reprojection(
                json["in"].asString(),
                json["out"].asString(),
                json["hammer"].asBool())
    { }

    static std::unique_ptr<Reprojection> create(const Json::Value& json)
    {
        if (json.isMember("out")) return makeUnique<Reprojection>(json);
        else return std::unique_ptr<Reprojection>();
    }

    Json::Value toJson() const
    {
        Json::Value json;
        json["out"] = out();
        if (m_in.size()) json["in"] = in();
        if (m_hammer) json["hammer"] = true;
        return json;
    }

    std::string toString() const
    {
        std::string s;

        if (hammer())
        {
            s += in() + " (OVERRIDING file headers)";
        }
        else if (in().size())
        {
            s += "(from file headers, or a default of '";
            s += in() + "')";
        }
        else
        {
            s += "(from file headers)";
        }

        s += " -> " + out();
        return s;
    }

    std::string in() const { return m_in; }
    std::string out() const { return m_out; }
    bool hammer() const { return m_hammer; }

private:
    std::string m_in;
    std::string m_out;

    bool m_hammer;
};

inline std::ostream& operator<<(std::ostream& os, const Reprojection& r)
{
    os << r.in() << " -> " << r.out();
    return os;
}

} // namespace entwine

