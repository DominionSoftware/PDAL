/******************************************************************************
* Copyright (c) 2015, Peter J. Gadomski <pete.gadomski@gmail.com>
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include "PlyWriter.hpp"

#include <limits>
#include <sstream>

#include <pdal/util/OStream.hpp>
#include <pdal/util/ProgramArgs.hpp>

namespace pdal
{

static StaticPluginInfo const s_info
{
        "writers.ply",
        "ply writer",
        "http://pdal.io/stages/writers.ply.html",
        { "ply" },
        { "ply" }
};

CREATE_STATIC_STAGE(PlyWriter, s_info)

std::string PlyWriter::getName() const { return s_info.name; }


PlyWriter::PlyWriter()
{}


void PlyWriter::addArgs(ProgramArgs& args)
{
    args.add("filename", "Output filename", m_filename).setPositional();
    args.add("storage_mode", "PLY Storage Mode", m_format, Format::Ascii);
    args.add("dims", "Dimension names", m_dimNames);
    args.add("faces", "Write faces", m_faces);
}


void PlyWriter::prepared(PointTableRef table)
{
    if (m_dimNames.size())
    {
        for (auto& name : m_dimNames)
        {
            auto id = table.layout()->findDim(name);
            if (id == Dimension::Id::Unknown)
                throwError("Unknown dimension '" + name + "' in provided "
                    "dimension list.");
            m_dims.push_back(id);
        }
    }
    else
    {
        m_dims = table.layout()->dims();
        for (auto dim : m_dims)
            m_dimNames.push_back(Utils::tolower(table.layout()->dimName(dim)));
    }
}


std::string PlyWriter::getType(Dimension::Type type) const
{
   static std::map<Dimension::Type, std::string> types =
    {
        { Dimension::Type::Signed8, "int8" },
        { Dimension::Type::Unsigned8, "uint8" },
        { Dimension::Type::Signed16, "int16" },
        { Dimension::Type::Unsigned16, "uint16" },
        { Dimension::Type::Signed32, "int32" },
        { Dimension::Type::Unsigned32, "uint32" },
        { Dimension::Type::Float, "float32" },
        { Dimension::Type::Double, "float64" }
    };

    try
    {
        return types.at(type);
    }
    catch (std::out_of_range)
    {
        throwError("Can't write dimension of type '" +
                Dimension::interpretationName(type) + "'.");
    }
    return "";
}


void PlyWriter::writeHeader(PointLayoutPtr layout) const
{
    *m_stream << "ply" << std::endl;
    *m_stream << "format " << m_format << " 1.0" << std::endl;
    *m_stream << "comment Generated by PDAL" << std::endl;
    *m_stream << "element vertex " << pointCount() << std::endl;

    auto ni = m_dimNames.begin();
    for (auto dim : m_dims)
    {
        std::string name = *ni++;
        std::string typeString = getType(layout->dimType(dim));
        *m_stream << "property " << typeString << " " << name << std::endl;
    }
    if (m_faces)
    {
        *m_stream << "element face " << faceCount() << std::endl;
        *m_stream << "property list uint8 uint32 vertex_indices" << std::endl;
    }
    *m_stream << "end_header" << std::endl;
}


void PlyWriter::ready(PointTableRef table)
{
    if (pointCount() > std::numeric_limits<uint32_t>::max())
        throwError("Can't write PLY file.  Only " +
            std::to_string(std::numeric_limits<uint32_t>::max()) +
            " points supported.");

    m_stream = Utils::createFile(m_filename, true);
    writeHeader(table.layout());
}


void PlyWriter::write(const PointViewPtr data)
{
    m_views.push_back(data);
}


void PlyWriter::writeValue(PointRef& point, Dimension::Id dim,
    Dimension::Type type)
{
    if (m_format == Format::Ascii)
    {
        double d = point.getFieldAs<double>(dim);
        *m_stream << d;
    }
    else if (m_format == Format::BinaryLe)
    {
        OLeStream out(m_stream);
        Everything e;
        point.getField((char *)&e, dim, type);
        Utils::insertDim(out, type, e);
    }
    else if (m_format == Format::BinaryBe)
    {
        OBeStream out(m_stream);
        Everything e;
        point.getField((char *)&e, dim, type);
        Utils::insertDim(out, type, e);
    }
}


void PlyWriter::writePoint(PointRef& point, PointLayoutPtr layout)
{
    for (auto it = m_dims.begin(); it != m_dims.end();)
    {
        Dimension::Id dim = *it;
        writeValue(point, dim, layout->dimType(dim));
        ++it;
        if (m_format == Format::Ascii && it != m_dims.end())
            *m_stream << " ";
    }
    if (m_format == Format::Ascii)
        *m_stream << std::endl;
}


void PlyWriter::writeTriangle(const Triangle& t, size_t offset)
{
    if (m_format == Format::Ascii)
    {
        *m_stream << "3 " << (t.m_a + offset) << " " <<
            (t.m_b + offset) << " " << (t.m_c + offset) << std::endl;
    }
    else if (m_format == Format::BinaryLe)
    {
        OLeStream out(m_stream);
        unsigned char count = 3;
        uint32_t a = (uint32_t)(t.m_a + offset);
        uint32_t b = (uint32_t)(t.m_b + offset);
        uint32_t c = (uint32_t)(t.m_c + offset);
        out << count << a << b << c;
    }
    else if (m_format == Format::BinaryBe)
    {
        OBeStream out(m_stream);
        unsigned char count = 3;
        uint32_t a = (uint32_t)(t.m_a + offset);
        uint32_t b = (uint32_t)(t.m_b + offset);
        uint32_t c = (uint32_t)(t.m_c + offset);
        out << count << a << b << c;
    }
}


// Deferring write until this time allows both points and faces from multiple
// point views to be written.
void PlyWriter::done(PointTableRef table)
{
    for (auto& v : m_views)
    {
        PointRef point(*v, 0);
        for (PointId idx = 0; idx < v->size(); ++idx)
        {
            point.setPointId(idx);
            writePoint(point, table.layout());
        }
    }
    if (m_faces)
    {
        PointId offset = 0;
        for (auto& v : m_views)
        {
            TriangularMesh *mesh = v->mesh();
            if (mesh)
            {
                for (size_t id = 0; id < mesh->size(); ++id)
                {
                    const Triangle& t = (*mesh)[id];
                    writeTriangle(t, offset);
                }
            }
            offset += v->size();
        }
    }
    Utils::closeFile(m_stream);
    m_stream = nullptr;
    getMetadata().addList("filename", m_filename);
}

} // namespace pdal
