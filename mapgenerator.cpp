/*  Copyright (c) 2013, Abdullah A. Hassan <voodooattack@hotmail.com>
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 *  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 *  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include "mapgenerator.hpp"
#include "engine.hpp"
#include "game.hpp"
#include "map.hpp"
#include "jsonutils.hpp"
#include "util.hpp"
#include "threadingutils.hpp"

#include <string>
#include <algorithm>

#include <physfs.hpp>

#include <boost/container/flat_set.hpp>
#include <boost/polygon/polygon.hpp>
#include <boost/polygon/voronoi.hpp>
#include <boost/multi_array.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
// #include <boost/geometry.hpp>
// #include <boost/geometry/geometries/geometries.hpp>
// #include <boost/geometry/geometries/adapted/boost_polygon.hpp>
// #include <boost/assign.hpp>
// #include <boost/iterator.hpp>

#ifdef NOISE_DIR_IS_LIBNOISE
#include <libnoise/noise.h>
#else
#include <noise/noise.h>
#endif

template<class T>
std::ostream & operator<< (std::ostream & os, const boost::polygon::point_data<T> & p)
{
  os << p.x() << "," << p.y();
  return os;
}

namespace ADWIF
{
  class HeightMapModule: public noise::module::Module
  {
  public:
    inline static double cubicInterpolate (const double p[4], double x) {
      return p[1] + 0.5 * x*(p[2] - p[0] + x*(2.0*p[0] - 5.0*p[1] +
        4.0*p[2] - p[3] + x*(3.0*(p[1] - p[2]) + p[3] - p[0])));
    }

    inline static double bicubicInterpolate (const double p[4][4], double x, double y) {
      double arr[4];
      arr[0] = cubicInterpolate(p[0], y);
      arr[1] = cubicInterpolate(p[1], y);
      arr[2] = cubicInterpolate(p[2], y);
      arr[3] = cubicInterpolate(p[3], y);
      return cubicInterpolate(arr, x);
    }

    HeightMapModule(const boost::multi_array<BiomeCell, 2> & biomeMap,
                    int chunkSizeX, int chunkSizeY): Module(0), myBiomeMap(biomeMap),
                    myChunkSizeX(chunkSizeX), myChunkSizeY(chunkSizeY) { }

    virtual int GetSourceModuleCount() const { return 0; }

    virtual double GetValue(double x, double y, double z) const {
      int chunkX = x / myChunkSizeX, chunkY = y / myChunkSizeY;
      int xmin = myBiomeMap.index_bases()[0], xmax = myBiomeMap.shape()[0];
      int ymin = myBiomeMap.index_bases()[1], ymax = myBiomeMap.shape()[1];

      if (chunkX < xmin || chunkY < ymin || chunkX >= xmax || chunkY >= ymax)
        return 0;

//       if (myBiomeMap[chunkX][chunkY].flat)
//         return myBiomeMap[chunkX][chunkY].height;

      double m[4][4];

      std::memset(m, 0, 4 * 4 * sizeof(double));

//       for (int j = 0; j < 4; j++)
//         for (int i = 0; i < 4; i++)
//           m[i][j] = myBiomeMap[chunkX][chunkY].height;

      int startx = 0, endx = 3;
      int starty = 0, endy = 3;

      while (chunkX + startx < xmin) startx++;
      while (chunkX + startx > xmax) starty--;
      while (chunkY + starty < ymin) starty++;
      while (chunkY + starty > ymax) starty--;

      while (chunkX + endx < xmin) endx++;
      while (chunkX + endx > xmax) endy--;
      while (chunkY + endy < ymin) endy++;
      while (chunkY + endy > ymax) endy--;

      //TODO: Shift the interpolation grid outside flat cells

      for (int j = starty; j <= endy; j++)
        for (int i = startx; i <= endx; i++)
          m[i][j] = myBiomeMap[chunkX+i-1][chunkY+j-1].height;

      double vx = fmod(x, myChunkSizeX) / myChunkSizeX, vy = fmod(y, myChunkSizeY) / myChunkSizeY;
      return bicubicInterpolate(m, 0.25 + vx, 0.25 + vy);
    }

    int myChunkSizeX, myChunkSizeY;
    const boost::multi_array<BiomeCell, 2> & myBiomeMap;
  };

  typedef boost::polygon::segment_data<double> segment;
  typedef boost::polygon::voronoi_diagram<double> voronoi_diagram;

  using boost::container::flat_set;

  MapGenerator::MapGenerator(const std::shared_ptr<Game> & game):
    myGame(game), myMapImg(), myHeightMap(), myChunkSizeX(32), myChunkSizeY(32), myChunkSizeZ(16),
    myColourIndex(), myRandomEngine(), myGenerationMap(), myPriorityFlags(),
    myBiomeMap(), myRegions(), myHeight(0), myWidth(0), myDepth(512),
    mySeed(boost::chrono::system_clock::now().time_since_epoch().count()),
    myGenerationLock(), myGeneratorCount(0), myGeneratorAbortFlag(false),
    myLastChunkX(-1), myLastChunkY(-1), myLastChunkZ(-1), myModules(), myHeightSource(), myInitialisedFlag(false)
  {
    myRandomEngine.seed(mySeed);
    myGeneratorAbortFlag.store(false);
  }

  MapGenerator::~MapGenerator() { }

  void MapGenerator::init()
  {
    if (!myInitialisedFlag)
    {
      generateBiomeMap();
      myInitialisedFlag = true;
    }

    myModules.clear();

    std::shared_ptr<HeightMapModule> heightmapSource(new HeightMapModule(myBiomeMap, myChunkSizeX, myChunkSizeY));
    std::shared_ptr<noise::module::Perlin> perlinSource(new noise::module::Perlin);
    std::shared_ptr<noise::module::ScaleBias> scaleBiasFilter(new noise::module::ScaleBias);
    std::shared_ptr<noise::module::Add> addFilter(new noise::module::Add);

    perlinSource->SetSeed(mySeed);
    perlinSource->SetFrequency(0.03);
    perlinSource->SetPersistence(0.2);
    perlinSource->SetLacunarity(1.50);
    perlinSource->SetOctaveCount(4);
    perlinSource->SetNoiseQuality(noise::QUALITY_STD);

    scaleBiasFilter->SetSourceModule(0, *perlinSource);
    scaleBiasFilter->SetBias(0.0);
    scaleBiasFilter->SetScale(0.005);

    addFilter->SetSourceModule(0, *heightmapSource);
    addFilter->SetSourceModule(1, *scaleBiasFilter);

    myModules.push_back(heightmapSource);
    myModules.push_back(perlinSource);
    myModules.push_back(scaleBiasFilter);
    myModules.push_back(addFilter);

    myHeightSource = addFilter;
    myHeightMapModule = heightmapSource;
  }

  void MapGenerator::generateBiomeMap()
  {
    for(auto const & b : game()->biomes())
      myColourIndex[b.second->mapColour] = b.second->name;
    myHeight = myMapImg.getHeight();
    myWidth = myMapImg.getWidth();
    myBiomeMap.resize(boost::extents[myWidth][myHeight]);
    myGenerationMap.resize(boost::extents[myWidth][myHeight][myDepth]);
    myPriorityFlags.resize(boost::extents[myWidth][myHeight][myDepth]);
    for(unsigned int y = 0; y < myHeight; y++)
      for(unsigned int x = 0; x < myWidth; x++)
      {
        uint32_t colour = getPixelColour(x, y, myMapImg);

        if (myColourIndex.find(colour) == myColourIndex.end())
        {
          game()->engine()->reportError(true,
            boost::str(boost::format("unknown biome colour: %x at pixel %ix%i") % colour % x % y));
          return;
        }

        RGBQUAD h;
        myHeightMap.getPixelColor(x, y, &h);
        double height = (-0.5 + (h.rgbBlue | h.rgbGreen | h.rgbRed) / 256.0);

        myBiomeMap[x][y].name = myColourIndex[colour];
        myBiomeMap[x][y].x = x;
        myBiomeMap[x][y].y = y;
        myBiomeMap[x][y].height = height;
        myBiomeMap[x][y].flat = game()->biomes()[myColourIndex[colour]]->flat;
      }

      // Clustering algorithm for terrain features

      boost::multi_array<bool,2> visited;
      visited.resize(boost::extents[myWidth][myHeight]);

      struct Cluster
      {
        std::string biome;
        flat_set<point> points;
      };

      std::vector<Cluster> clusters;

      int i = 0;

      for(unsigned int y = 0; y < myHeight; y++)
      {
        for (unsigned int x = 0; x < myWidth; x++)
        {
          if (visited[x][y]) continue;

          uint32_t colour = getPixelColour(x, y, myMapImg);

//           fipImage img(myMapImg);

          flat_set<point> points;
          std::deque<point> q;

          q.push_back(point(x,y));

          while(!q.empty())
          {
            point n = q.back(); q.pop_back();
            uint32_t pcolour = getPixelColour(n.x(), n.y(), myMapImg);

            if (!visited[n.x()][n.y()] && colour == pcolour)
            {
              visited[n.x()][n.y()] = true;

              bool edge = false;

              if (n.x() > 0) q.push_back(point(n.x() - 1,n.y())); else edge = true;
              if (n.x() < (signed)myWidth - 1) q.push_back(point(n.x() + 1,n.y())); else edge = true;
              if (n.y() > 0) q.push_back(point(n.x(),n.y() - 1)); else edge = true;
              if (n.y() < (signed)myHeight - 1) q.push_back(point(n.x(),n.y() + 1)); else edge = true;

              if (n.x() > 0 && n.y() > 0) q.push_back(point(n.x() - 1,n.y() - 1)); else edge = true;
              if (n.x() < (signed)myWidth - 1 && n.y() < (signed)myHeight - 1) q.push_back(point(n.x() + 1,n.y() + 1)); else edge = true;
              if (n.x() < (signed)myWidth - 1 && n.y() > 0) q.push_back(point(n.x() + 1,n.y() - 1)); else edge = true;
              if (n.x() > 0 && n.y() < (signed)myHeight - 1) q.push_back(point(n.x() - 1,n.y() + 1)); else edge = true;

              if (edge || ((int)n.x() % 2 == 0 && (int)n.y() % 2 == 0))
              //if (edge)
              {
                points.insert(n);

//                  if (edge)
//                  {
//                    RGBQUAD outer = { 0, 0, 0, 255 };
//                    img.setPixelColor(n.x(),n.y(), &outer);
//                  } else {
//                    RGBQUAD inner = { 0, 0, 255, 255 };
//                    img.setPixelColor(n.x(),n.y(), &inner);
//                  }
              }
            }
            else if (colour != pcolour)
            {

              points.insert(n);

//               RGBQUAD outer = { 0, 0, 0, 255 };
//               img.setPixelColor(n.x(),n.y(), &outer);
            }
          }

          if (points.empty()) continue;

          Cluster c;
          c.biome = myBiomeMap[x][y].name;
          c.points = points;

          clusters.push_back(c);

//           std::string fileName = saveDir + dirSep + "map" + dirSep + "png" + dirSep + boost::lexical_cast<std::string>(i++) + ".png";
//           img.flipVertical();
//           img.save(fileName.c_str());
        }
      }

      auto concaveHull = [](const flat_set<point> & in, polygon & poly, double alpha, int mendRadius)
      {
        auto getCoords = [](const point & p1, const point & p2, double radius, bool dir) -> point
        {
          double midX = (p1.x() + p2.x()) / 2.0;
          double midY = (p1.y() + p2.y()) / 2.0;
          double dx = (p1.x() - p2.x()) / 2.0;
          double dy = (p1.y() - p2.y()) / 2.0;
          double dist = std::sqrt(dx * dx + dy * dy);
          double pDist = std::sqrt(radius * radius - dist * dist);
          double pdx, pdy;
          if (dir) {
            pdx = dy * pDist / dist;
            pdy = -dx * pDist / dist;
          } else {
            pdx = -dy * pDist / dist;
            pdy = dx * pDist / dist;
          }
          return point(midX + pdx, midY + pdy);
        };

        flat_set<segment> segments;

        voronoi_diagram vd;
        boost::polygon::construct_voronoi(in.begin(), in.end(), &vd);

        for(const voronoi_diagram::edge_type & e : vd.edges())
        {
          const voronoi_diagram::cell_type * c1 = e.cell();
          const voronoi_diagram::cell_type * c2 = e.twin()->cell();

          const point p1 = *(in.begin() + c1->source_index()), p2 = *(in.begin() + c2->source_index());
          const point pp1 = getCoords(p1, p2, alpha, false);
          double dist = boost::polygon::distance_squared(p1, p2);

          //std::cerr << "detecting " << p1 << " to " << p2 << ": ";

          if (boost::polygon::distance_squared(p1, pp1) < dist || dist > alpha * alpha)
          {
            //std::cerr << "early abort, dist: " << dist << std::endl;
            continue;
          }

          bool obstructed = false;
//           point ob;

          for(const voronoi_diagram::cell_type & c : vd.cells())
          {
            const point p3 = *(in.begin() + c.source_index());
            if (p3 == p1 || p3 == p2) continue;
            if (boost::polygon::distance_squared(pp1, p3) < alpha * alpha / 2.0)
            {
//               ob = p3;
              obstructed = true;
              break;
            }
          }

          if (!obstructed)
          {
            segments.insert(segment(p1, p2));
            //std::cerr << "(" << p1 << " -> " << p2 << ") ";
          } //else std::cerr << "obstructed by " << ob;
          //std::cerr << std::endl;
        }

        std::vector<polygon> polygons;

        //std::cerr << "discovered " << segments.size() << " segments." << std::endl;

        std::map<segment, bool> mendMap;

        auto mendNearest = [&](const point & p, int r = 1)
        {
          if (r <= 0)
            return segments.end();

          auto sg = std::find_if(segments.begin(), segments.end(), [&](const segment & s)
          {
            for (int y = p.y() - r; y < p.y() + r; y++)
              for (int x = p.x() - r; x < p.x() + r; x++)
                if (((s.low().x() == x && s.low().y() == y) ||
                    (s.high().x() == x && s.high().y() == y)))
                      return true;
            return false;
          });

          if (sg == segments.end())
            return segments.end();

          for (int radius = 1; radius <= r; radius++)
          {
            int x = radius, y = 0;
            int error = 1-x;

            while(x >= y)
            {
              if
              (
                (sg->low().x() ==  x + p.x() && sg->low().y() ==  y + p.y()) ||
                (sg->low().x() ==  y + p.x() && sg->low().y() ==  x + p.y()) ||
                (sg->low().x() == -x + p.x() && sg->low().y() ==  y + p.y()) ||
                (sg->low().x() == -y + p.x() && sg->low().y() ==  x + p.y()) ||
                (sg->low().x() == -x + p.x() && sg->low().y() == -y + p.y()) ||
                (sg->low().x() == -y + p.x() && sg->low().y() == -x + p.y()) ||
                (sg->low().x() ==  x + p.x() && sg->low().y() == -y + p.y()) ||
                (sg->low().x() ==  y + p.x() && sg->low().y() == -x + p.y())
              )
              {
                segment s(p, sg->low());
                if (!mendMap[s])
                {
                  mendMap[s] = true;
                  return segments.insert(s).first - 1;
                }
              }
              else if
              (
                (sg->high().x() ==  x + p.x() && sg->high().y() ==  y + p.y()) ||
                (sg->high().x() ==  y + p.x() && sg->high().y() ==  x + p.y()) ||
                (sg->high().x() == -x + p.x() && sg->high().y() ==  y + p.y()) ||
                (sg->high().x() == -y + p.x() && sg->high().y() ==  x + p.y()) ||
                (sg->high().x() == -x + p.x() && sg->high().y() == -y + p.y()) ||
                (sg->high().x() == -y + p.x() && sg->high().y() == -x + p.y()) ||
                (sg->high().x() ==  x + p.x() && sg->high().y() == -y + p.y()) ||
                (sg->high().x() ==  y + p.x() && sg->high().y() == -x + p.y())
              )
              {
                segment s(sg->high(), p);
                if (!mendMap[s])
                {
                  mendMap[s] = true;
                  return segments.insert(s).first - 1;
                }
              }

              y++;
              if(error < 0)
                error += 2 * y + 1;
              else
              {
                x--;
                error += 2 * (y - x + 1);
              }
            }
          }

          return segments.end();
        };

        while(!segments.empty())
        {
          //std::cerr << "constructing polygon: ";
          std::list<point> pointsIndexed;
          segment seg = *segments.begin();
          segments.erase(segments.begin());
          pointsIndexed.push_back(seg.high());
          pointsIndexed.push_back(seg.low());
          //std::cerr << seg.low() << " -> " << seg.high();
          while(pointsIndexed.front() != pointsIndexed.back())
          {
            bool found = false;
            for (auto it = segments.begin(); it != segments.end(); ++it)
            {
//               if (it->low() == pointsIndexed.back())
//               {
//                 pointsIndexed.push_back(it->high());
//                 //std::cerr << " -> " << it->high();
//                 segments.erase(it);
//                 found = true;
//                 break;
//               }
              if (it->high() == pointsIndexed.back())
              {
                pointsIndexed.push_back(it->low());
                //std::cerr << " -> " << it->low();
                segments.erase(it);
                found = true;
                break;
              }
              if (it->low() == pointsIndexed.front())
              {
                pointsIndexed.push_front(it->high());
                //std::cerr << " -> " << it->high();
                segments.erase(it);
                found = true;
                break;
              }
//               if (it->high() == pointsIndexed.front())
//               {
//                 pointsIndexed.push_front(it->low());
//                 //std::cerr << " -> " << it->low();
//                 segments.erase(it);
//                 found = true;
//                 break;
//               }
              if (!found && it == segments.end() - 1 && mendRadius > 0)
              {
                //std::cerr << " {mending: ";
                auto m = mendNearest(pointsIndexed.back(), mendRadius);
                if (m == segments.end())
                  m = mendNearest(pointsIndexed.front(), mendRadius);
                if (m != segments.end())
                {
                  //std::cerr << "ok}";
                  it = m;
                }
                else
                {
                  //std::cerr << "n/a}";
                  break;
                }
              }
            }
            if (!found)
            {
              pointsIndexed.push_back(pointsIndexed.front());
              break;
            }
          }

          polygons.push_back(polygon(pointsIndexed.begin(), pointsIndexed.end()));
          //std::cerr << std::endl;
        }

        auto biggest = polygons.begin();
        double maxArea = 0;

        for (auto it = polygons.begin(); it != polygons.end(); it++)
        {
          double area = boost::polygon::area(*it);
          if (area > maxArea)
          {
            maxArea = area;
            biggest = it;
          }
        }

        if (biggest != polygons.end())
        {
          poly.set(biggest->begin(), biggest->end());
          polygons.erase(biggest);
          poly.set_holes(polygons.begin(), polygons.end());
        }

        //std::cerr << "discovered: " << polygons.size() << " holes" << std::endl;
      };

//       i = 0;

//       std::ofstream fsvg(saveDir + dirSep + "map.svg");
//       boost::geometry::svg_mapper<point> mapper(fsvg, myWidth, myHeight);

      boost::atomic_size_t clusterCompletionCount;
      boost::mutex regionMutex;

      clusterCompletionCount.store(clusters.size());

      auto calculateHullThread = [&](Cluster & c) -> void
      {
        AtomicRefCount<std::size_t> refCount(clusterCompletionCount, false);
        Region region;

        concaveHull(c.points, region.poly, 1.5, 0);

        boost::polygon::center(region.centroid, region.poly);

        region.biome = c.biome;
        region.area = boost::polygon::area(region.poly);

        {
          boost::mutex::scoped_lock guard(regionMutex);
          myRegions.push_back(boost::move(region));
        }
      };

      for (Cluster & c : clusters)
        game()->engine()->service().post(boost::bind<void>(calculateHullThread, c));

      while(clusterCompletionCount > 0) game()->engine()->service().poll_one();

      for (Region & r : myRegions)
      {
        fipImage imageOut = myMapImg;

        imageOut.convertToGrayscale();
        imageOut.convertTo32Bits();
//
//         std::ofstream fsvg(saveDir + dirSep + "map" + dirSep + "svg" + dirSep + boost::lexical_cast<std::string>(i) + ".svg");
//         boost::geometry::svg_mapper<point> mapper(fsvg, 200, 481);//,  "width=\"200\" height=\"480\"");
//
//         boost::geometry::model::polygon<point> po;

        for (const auto & p : r.poly)
        {
//           boost::geometry::append(po, p);
          //std::cerr << p << " ";
          RGBQUAD co = { 0, 0, 255, 255 };
          imageOut.setPixelColor(p.x(), p.y(), &co);
        }
//
//         //std::cerr << "\n";
//
        std::uniform_int_distribution<int> randomColour(64, 192);
//
        for(auto i = r.poly.begin_holes(); i != r.poly.end_holes(); ++i)
        {
//           boost::geometry::model::ring<point> ph;
//
          int r = randomColour(myRandomEngine),
              g = randomColour(myRandomEngine),
              b = randomColour(myRandomEngine);
//
          for (const auto & p : *i)
          {
//             boost::geometry::append(ph, p);
            RGBQUAD co = { (BYTE)r, (BYTE)g, (BYTE)b, 255 };
            imageOut.setPixelColor(p.x(), p.y(), &co);
          }
//
//           po.inners().push_back(ph);
        }

//         boost::geometry::correct(po);

//         mapper.add(po);
//         mapper.map(po, "fill-opacity:1.0;fill:" +
//           colourToHexString(myGame->biomes()[c.biome]->mapColour) +
//             ";stroke:rgb(0,0,0);stroke-width:1");
// //
        std::string fileName = saveDir + dirSep + "png" + dirSep + "out" + boost::lexical_cast<std::string>(i) + ".png";
        imageOut.flipVertical();
        imageOut.save(fileName.c_str());

        i++;
      }
  }

  void MapGenerator::generateAll()
  {
    for(int y = 0; y < myHeight; y++)
      for(int x = 0; x < myWidth; x++)
        for(int z = -myDepth / 2; z < myDepth / 2; z++)
        {
          generateOne(x, y, z);
        }
  }

  void MapGenerator::generateOne(int x, int y, int z, bool regenerate, bool cooperative)
  {
    if (x < 0 || y < 0 || x >= myHeight || y >= myWidth)
      return;

    if (myGeneratorAbortFlag)
      return;

    {
      boost::recursive_mutex::scoped_lock guard(myGenerationLock);
      if (myGenerationMap[x][y][z+myDepth/2] == boost::indeterminate) return;
      if (myGenerationMap[x][y][z+myDepth/2] == true && !regenerate) return;
      myGenerationMap[x][y][z+myDepth/2] = boost::indeterminate;
    }

    game()->engine()->log("MapGenerator"), boost::str(boost::format("generating cell: %ix%ix%i") % x % y % z);

    Map * map = game()->map().get();

    AtomicRefCount<std::size_t> refCount(myGeneratorCount);

    int offx = x * myChunkSizeX, offy = y * myChunkSizeY;
    int offz = z * myChunkSizeZ - myChunkSizeZ;

    Biome * biome = game()->biomes()[myBiomeMap[x][y].name];

    std::vector<Region> regions;

    for(auto & r : myRegions)
      if (boost::polygon::contains(r.poly, point(x,y), true))
        regions.push_back(r);

    if (regions.empty())
      throw std::runtime_error(boost::str(boost::format("could not find region for biome cell %ix%i") % x %y));

    std::vector<point> neighbours;

    {
      flat_set<point> vdpoints;
      const point coords(x,y);

      vdpoints.insert(coords);

      auto msg(game()->engine()->log("MapGenerator"));
      msg, boost::format("map cell %ix%ix%i intersects regions: ") % x % y % z;
      for(auto & r : regions)
      {
        msg, r.centroid, " (", r.biome, ") ";
        for(const point & p : r.poly)
          if (boost::polygon::distance_squared(p, coords) < 4)
            vdpoints.insert(p);
      }
      msg.flush();

      if (vdpoints.size() < 3)
      {
        for (auto const & p : vdpoints)
          if (p != coords)
            neighbours.push_back(p);
      }
      else
      {
        voronoi_diagram vd;

        boost::polygon::construct_voronoi(vdpoints.begin(), vdpoints.end(), &vd);

        for(const voronoi_diagram::cell_type & c : vd.cells())
        {
          const point & p = *(vdpoints.begin() + c.source_index());
          if (p == coords)
          {
            const voronoi_diagram::edge_type * edge = c.incident_edge();
            do
            {
  //             if (!edge->twin()) { edge = edge->next(); continue; } // bug?
              const voronoi_diagram::cell_type * nc = edge->twin()->cell();
              const point & np = *(vdpoints.begin() + nc->source_index());
              neighbours.push_back(np);
              edge = edge->next();
            } while(edge != c.incident_edge());
            break;
          }
        }
      }
    }

    auto msg(game()->engine()->log("MapGenerator"));
    msg, boost::format("found %i neighbours for cell %ix%ix%i: ") % neighbours.size() % x % y % z;
    for(const point & p : neighbours)
      msg, p, " (", myBiomeMap[p.x()][p.y()].name, ") ";
    msg.flush();

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    std::vector<std::string> possible;
    std::vector<double> probabilities;

    possible.assign(biome->materials.begin(), biome->materials.end());

    for (const point & p : neighbours)
    {
      Biome * b = game()->biomes()[myBiomeMap[p.x()][p.y()].name];
//       std::copy(b->materials.begin(), b->materials.end(), std::back_inserter(possible));
      for (const std::string m : b->materials)
//         if (!game()->materials()[m]->liquid)
      {
        double dist = boost::polygon::distance_squared(point(x,y), p);
        probabilities.push_back(dist ? 1/dist : 1);
        possible.push_back(m);
      }
    }

    std::discrete_distribution<int> dd(probabilities.begin(), probabilities.end());
    std::uniform_int_distribution<int> ud(0, biome->materials.size()-1);
    std::bernoulli_distribution bd(0.001);

    game()->engine()->log("MapGenerator"), "possible materials: ", possible.size();

    std::function<std::string(int,int,int,int)> getMaterial;

    noise::module::Perlin perlinMat;
    noise::module::ScaleBias scaleMat;
    noise::module::Clamp clampMat;

    if (possible.size() > 1)
    {
      perlinMat.SetSeed(mySeed << 2);
      perlinMat.SetFrequency(0.06);
      perlinMat.SetPersistence(0.4);
      perlinMat.SetLacunarity(1.50);
      perlinMat.SetOctaveCount(possible.size());
      perlinMat.SetNoiseQuality(noise::QUALITY_FAST);

      scaleMat.SetSourceModule(0, perlinMat);
      scaleMat.SetBias(possible.size());
      scaleMat.SetScale(possible.size());

      clampMat.SetSourceModule(0, scaleMat);
      clampMat.SetBounds(0, possible.size()-1);

      getMaterial = [&](int xx, int yy, int zz, int height) -> std::string
      {
//         int idx = floor(clampMat.GetValue(xx,yy,zz));
        int idx = dd(myRandomEngine);
//         std::cerr << clampType.GetValue(xx,yy,zz) << std::endl;
        return possible[idx];
      };
    }
    else
      getMaterial = [&](int xx, int yy, int zz, int height) -> std::string { return possible[0]; };

    int counter = 0;


    auto getHeight = [&](unsigned int x, unsigned int y) -> int {
      if (biome->flat)
        return 0;
//         return floor(myHeightMapModule->GetValue(x, y, 0) * myChunkSizeZ * (myDepth / 2));
      else
        return floor(myHeightSource->GetValue(x, y, 0) * myChunkSizeZ * (myDepth / 2));
    };

    auto generateCell = [&](int xx, int yy, int zz, int height)
    {
      MapCell c(map->get(xx, yy, zz));
      if (c.generated && !regenerate) return;
      if (!c.generated)
      {
        std::string smat = getMaterial(xx, yy, zz, height);
        c.material = smat;
        c.cmaterial = game()->materials()[smat];
      }
      if (zz == height)
      {
        std::uniform_int_distribution<int> ud2(0, c.cmaterial->disp[TerrainType::Floor].size() - 1);
        c.symIdx = ud2(myRandomEngine);
        c.biome = biome->name;
        c.generated = true;
        c.background = false;
        c.type = TerrainType::Floor;
        if ((getHeight(xx+1,yy) == height + 1 ||
          getHeight(xx,yy+1) == height + 1 ||
          getHeight(xx-1,yy) == height + 1 ||
          getHeight(xx,yy-1) == height + 1))
        {
          if (!c.cmaterial->liquid)
          {
            MapCell cc = c;
            c.type = TerrainType::RampU;
            cc.type = TerrainType::RampD;
            cc.generated = true;
            map->set(xx, yy, height+1, cc);
          }
        }
      }
      else if (zz < height)
      {
        if (c.cmaterial->disp.find(TerrainType::Wall) == c.cmaterial->disp.end())
          return;
        c.type = TerrainType::Wall;
        c.symIdx = 0;
        c.visible = false;
        c.generated = true;

        double hw = getHeight(xx-1, yy);
        double he = getHeight(xx+1, yy);
        double hn = getHeight(xx, yy-1);
        double hs = getHeight(xx, yy+1);

        if (he < zz || hw < zz || hs < zz || hn < zz)
          c.visible = true;
      }
      else
        return;
      map->set(xx, yy, zz, c);
    };

    // PASS 1 ============================================================================

    for (unsigned int yy = offy; yy < offy + myChunkSizeY; yy++)
    {
      if (cooperative && !myPriorityFlags[x][y][z+myDepth/2] && counter++ % 100 == 0)
        game()->engine()->scheduler()->yield();
      for (unsigned int xx = offx; xx < offx + myChunkSizeX; xx++)
      {
        if (myGeneratorAbortFlag)
        {
          boost::recursive_mutex::scoped_lock guard(myGenerationLock);
          myGenerationMap[x][y][z+myDepth/2] = false;
          myPriorityFlags[x][y][z+myDepth/2] = false;
          return;
        }
        int height = getHeight(xx,yy);
        for (int zz = offz; zz <= (offz + myChunkSizeZ > height ? height : offz + myChunkSizeZ); zz++)
          generateCell(xx, yy, zz, height);
      }
    }

    // PASS 2 ============================================================================

//     for (unsigned int yy = offy; yy < offy + myChunkSizeY; yy++)
//     {
//       if (cooperative && !myPriorityFlags[x][y][z+myDepth/2] && counter++ % 100 == 0)
//         game()->engine()->scheduler()->yield();
//       for (unsigned int xx = offx; xx < offx + myChunkSizeX; xx++)
//       {
//         if (myGeneratorAbortFlag)
//         {
//           boost::recursive_mutex::scoped_lock guard(myGenerationLock);
//           myGenerationMap[x][y][z+myDepth/2] = false;
//           return;
//         }
//
//         int height = getHeight(xx,yy);
//         for (int zz = offz; zz <= (offz + myChunkSizeZ > height ? height : offz + myChunkSizeZ); zz++)
//         {
//           MapCell c(map->get(xx, yy, zz));
//           if (c.cmaterial && c.cmaterial->liquid)
//           {
//             c.visible = true;
//             map->set(xx, yy, zz, c);
//             for (int i = -1; i <= 1; i++)
//               for (int j = -1; j <= 1; j++)
// //                 for (int k = -1; k <= 1; k++)
//                 {
//                   MapCell cc(map->get(xx+i,yy+j,zz/*+k*/));
//                   if (!cc.visible)
//                   {
//                     cc.visible = true;
//                     map->set(xx+i,yy+j,zz/*+k*/, cc);
//                   }
//                 }
//           }
//           else if (c.type == TerrainType::Wall)
//           {
//             const MapCell & w = map->get(xx-1, yy, zz);
//             const MapCell & e = map->get(xx+1, yy, zz);
//             const MapCell & n = map->get(xx, yy-1, zz);
//             const MapCell & s = map->get(xx, yy+1, zz);
//
//             double hw = getHeight(xx-1, yy);
//             double he = getHeight(xx+1, yy);
//             double hn = getHeight(xx, yy-1);
//             double hs = getHeight(xx, yy+1);
//
//             if (
//               ((he < zz || (!e.background && e.type != TerrainType::Wall)) ||
//                (hw < zz || (!w.background && w.type != TerrainType::Wall)) ||
//                (hs < zz || (!s.background && s.type != TerrainType::Wall)) ||
//                (hn < zz || (!n.background && n.type != TerrainType::Wall)) ||
//                (!e.background && e.cmaterial && e.cmaterial->liquid) ||
//                (!e.background && w.cmaterial && w.cmaterial->liquid) ||
//                (!e.background && s.cmaterial && s.cmaterial->liquid) ||
//                (!e.background && n.cmaterial && n.cmaterial->liquid)))
//               c.visible = true;
//             map->set(xx, yy, zz, c);
//           }
//         }
//       }
//     }

    {
      boost::recursive_mutex::scoped_lock guard(myGenerationLock);
      myGenerationMap[x][y][z+myDepth/2] = true;
      myPriorityFlags[x][y][z+myDepth/2] = false;
    }

    game()->engine()->log("MapGenerator"), boost::str(boost::format("generated cell: %ix%ix%i") % x % y % z);
  }

  void MapGenerator::notifyLoad() {  }
  void MapGenerator::notifySave() {  }

  void MapGenerator::generateAround(int x, int y, int z, int radius, int radiusZ)
  {
    int chunkX = x / myChunkSizeX;
    int chunkY = y / myChunkSizeY;
    int chunkZ = z / myChunkSizeZ;

    if (chunkX == myLastChunkX && chunkY == myLastChunkY && chunkZ == myLastChunkZ)
      return;

    myLastChunkX = chunkX; myLastChunkY = chunkY; myLastChunkZ = chunkZ;

    myPriorityFlags[chunkX][chunkY][chunkZ+myDepth/2] = true;

    if (isGenerated(chunkX, chunkY, chunkZ) == false)
      abort();

    for (int r = -radius; r <= radius; r++)
    {
      for (int i = -r; i <= r; i++)
        for (int j = -r; j <= r; j++)
          for (int k = -r; k <= r; k++)
            if (isGenerated(chunkX+i, chunkY+j, chunkZ+k) == false)
              if (!(i == 0 && j == 0 && k == 0))
              game()->engine()->scheduler()->schedule(boost::bind<void>(&MapGenerator::generateOne, this,
                                                      chunkX+i, chunkY+j, chunkZ+k, false, true));
    }

    generateOne(chunkX, chunkY, chunkZ);

    do
    {
      if (isGenerated(chunkX, chunkY, chunkZ) == true)
        break;
      else
        boost::this_thread::sleep_for(boost::chrono::microseconds(50));
    } while(true);
  }

  void MapGenerator::abort()
  {
    myGeneratorAbortFlag.store(true);
    while (myGeneratorCount) game()->engine()->service().poll_one();
    myGeneratorAbortFlag.store(false);
  }

}

