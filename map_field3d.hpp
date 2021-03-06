/*
 * Copyright (c) 2013, Abdullah A. Hassan <voodooattack@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef MAP_FIELD3D_H
#define MAP_FIELD3D_H

#include "mapcell.hpp"
#include "map.hpp"
#include "mapbank.hpp"

#include <Field3D/DenseField.h>
#include <Field3D/SparseField.h>
#include <Field3D/SparseFile.h>
#include <Field3D/Field3DFile.h>
#include <Field3D/InitIO.h>

#include <boost/atomic.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/functional/hash/extensions.hpp>
#include <boost/functional/hash/hash.hpp>

#include <memory>
#include <fstream>
#include <unordered_map>

namespace F3D = Field3D::v1_4;

FIELD3D_NAMESPACE_OPEN

  template<>
  inline DataTypeEnum DataTypeTraits<uint64_t>::typeEnum()
  {
    return DataTypeEnum::DataTypeDouble;
  }
  template <>
  inline hid_t DataTypeTraits<uint64_t>::h5type()
  {
    return H5T_STD_I64BE;
  }
  template <>
  inline int DataTypeTraits<uint64_t>::h5bits()
  {
    return 64;
  }

FIELD3D_NAMESPACE_HEADER_CLOSE

typedef F3D::V3i Vec3Type;

namespace std
{
  template <> struct hash<Vec3Type>
  {
    size_t operator()(const Vec3Type & vec) const
    {
      size_t h = 0;
      boost::hash_combine(h, vec.x);
      boost::hash_combine(h, vec.y);
      boost::hash_combine(h, vec.z);
      return h;
    }
  };
}

namespace ADWIF
{
  typedef F3D::DenseField<uint64_t> FieldType;

  class MapImpl
  {
    friend class Map;

    using clock_type = boost::chrono::steady_clock;
    using time_point = clock_type::time_point;
    using duration_type = clock_type::duration;

    struct Chunk
    {
      F3D::V3i pos;
      FieldType::Ptr field;
      boost::atomic<time_point> lastAccess;
      boost::atomic_bool dirty;
      std::string fileName;
      mutable boost::shared_mutex lock;
    };

  public:
    MapImpl(Map * parent, const std::shared_ptr<class Engine> & engine, const boost::filesystem::path & mapPath,
            bool load, unsigned int chunkSizeX, unsigned int chunkSizeY, unsigned int chunkSizeZ,
            const MapCell & bgValue = MapCell());
    ~MapImpl();

    const MapCell get(int x, int y, int z) const;
    void set(int x, int y, int z, const MapCell & cell);

    const MapCell & background() const;

    void prune(bool pruneAll = false) const;

  private:
    std::string getChunkName(const Vec3Type & v) const;
    std::shared_ptr<Chunk> & getChunk(int x, int y, int z) const;

    void loadChunk(std::shared_ptr<Chunk> & chunk, boost::upgrade_lock<boost::shared_mutex> & guard) const;
    void saveChunk(std::shared_ptr<Chunk> & chunk) const;

    void pruneTask();

  private:
    typedef std::unordered_map<Vec3Type, std::shared_ptr<Chunk>> GridMap;

    Map * myMap;
    std::weak_ptr<class Engine> myEngine;
    boost::filesystem::path myMapPath;
    mutable GridMap myChunks;
    std::fstream myIndexStream;
    std::shared_ptr<MapBank> myBank;
    F3D::V3i myChunkSize;
    uint64_t myBackgroundValue;
    mutable boost::recursive_mutex myLock;
    clock_type myClock;
    unsigned long int myMemThresholdMB;
    duration_type myDurationThreshold;
    duration_type myPruningInterval;
    mutable boost::atomic_bool myPruningInProgressFlag;
    boost::thread myPruneThread;
    boost::condition_variable myPruneThreadCond;
    boost::mutex myPruneThreadMutex;
    boost::atomic_bool myPruneThreadQuitFlag;

    static bool myInitialisedFlag;
  };
}

#endif // MAP_FIELD3D_H
