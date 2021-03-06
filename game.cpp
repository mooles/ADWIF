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
#include "config.hpp"
#include "game.hpp"
#include "adwif.hpp"
#include "engine.hpp"
#include "player.hpp"
#include "renderer.hpp"
#include "input.hpp"
//#include "scripting.hpp"
#include "util.hpp"
#include "jsonutils.hpp"
#include "introanimation.hpp"
#include "map.hpp"
#include "mapgenerator.hpp"
#include "serialisationutils.hpp"
#include "imageutils.hpp"
#include "fileutils.hpp"

#include <string>
#include <iostream>
#include <physfs.hpp>
#include <sstream>
#include <unistd.h>

#ifdef ADWIF_UNICODE
#include <utf8.h>
#endif

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

namespace ADWIF
{

  Game::Game(const std::shared_ptr<ADWIF::Engine> & engine): myEngine(engine), myPlayer(nullptr),
    myMap(nullptr), myRaces(), myProfessions(), mySkills(), myFactions(), myElements(), myBiomes()
  {
  }

  Game::~Game()
  {
    shutdown();
  }

  void Game::player(const std::shared_ptr<Player> & player)
  {
    myPlayer = player;
  }

  void Game::init()
  {
    reloadData();
  }

  void Game::shutdown(bool graceful)
  {
    if (myGenerator)
      myGenerator->abort();
    clearData();
  }

  void Game::reloadData()
  {
    clearData();

    if (!PhysFS::exists("races.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'races.json'";
      return;
    }

    if (!PhysFS::exists("factions.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'factions.json'";
      return;
    }

    if (!PhysFS::exists("professions.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'professions.json'";
      return;
    }

    if (!PhysFS::exists("skills.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'skills.json'";
      return;
    }

    if (!PhysFS::exists("biomes.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'biomes.json'";
      return;
    }

    if (!PhysFS::exists("elements.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'elements.json'";
      return;
    }

    if (!PhysFS::exists("materials.json"))
    {
      engine()->log("Game", LogLevel::Fatal), "could not locate 'materials.json'";
      return;
    }

    PhysFS::ifstream fraces("/races.json"), fprofessions("/professions.json"),
      fskills("/skills.json"), ffactions("/factions.json"),
      fbiomes("/biomes.json"), felements("/elements.json"), fmaterials("/materials.json");

    if (!fraces)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'races.json'";
      return;
    }

    if (!ffactions)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'factions.json'";
      return;
    }

    if (!fprofessions)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'professions.json'";
      return;
    }

    if (!fskills)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'skills.json'";
      return;
    }

    if (!fbiomes)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'biomes.json'";
      return;
    }

    if (!felements)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'elements.json'";
      return;
    }

    if (!fmaterials)
    {
      engine()->log("Game", LogLevel::Fatal), "could not open 'materials.json'";
      return;
    }

    Json::Value races, professions, skills, factions, biomes, elements, materials;
    Json::Reader reader;

    try
    {
      // Read skills **************************************************************************************

      if (!reader.parse(fskills, skills))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'skills.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadSkills(skills);

      if (mySkills.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'skills.json': no skill information found";
        return;
      }

      // Read professions **************************************************************************************

      if (!reader.parse(fprofessions, professions))
      {
        engine()->log("Game", LogLevel::Fatal),
          "error parsing 'professions.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadProfessions(professions);

      if (myProfessions.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'professions.json': no profession information found";
        return;
      }

      // Read factions ***********************************************************************************

      if (!reader.parse(ffactions, factions))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'factions.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadFactions(factions);

      if (myFactions.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'factions.json': no faction information found";
        return;
      }

      // Read races **************************************************************************************

      if (!reader.parse(fraces, races))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'races.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadRaces(races);

      if (myRaces.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'races.json': no race information found";
        return;
      }

      // Read elements **********************************************************************************

      if (!reader.parse(felements, elements))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'elements.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadElements(elements);

      if (myElements.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'elements.json': no material information found";
        return;
      }

      // Read materials **********************************************************************************

      if (!reader.parse(fmaterials, materials))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'materials.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadMaterials(materials);

      if (myMaterials.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'materials.json': no material information found";
        return;
      }

      // Read biomes *************************************************************************************

      if (!reader.parse(fbiomes, biomes))
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'biomes.json':\n" + reader.getFormattedErrorMessages();
        return;
      }

      loadBiomes(biomes);

      if (myBiomes.empty())
      {
        engine()->log("Game", LogLevel::Fatal), "error parsing 'biomes.json': no biome information found";
        return;
      }

      sanityCheck();
    }
    catch (std::runtime_error & e)
    {
      engine()->log("Game", LogLevel::Fatal), "exception: ", e.what();
    }
  }

  void Game::clearData()
  {
    for (auto & i : myRaces)
      delete i.second;
    for (auto & i : myProfessions)
      delete i.second;
    for (auto & i : myFactions)
      delete i.second;
    for (auto & i : mySkills)
      delete i.second;
    for (auto & i : myBiomes)
      delete i.second;
    for (auto & i : myElements)
      delete i.second;
    for (auto & i : myMaterials)
      delete i.second;
    myRaces.clear();
    myProfessions.clear();
    myFactions.clear();
    mySkills.clear();
    myBiomes.clear();
    myElements.clear();
    myMaterials.clear();
  }

  void Game::createMap()
  {
    MapCell bg;
    bg.clear();
    myMap.reset(new Map(engine(), saveDir / "map", false, 512, 512, 32, bg));

    myGenerator.reset(new MapGenerator(shared_from_this()));

    fipImage mapImg;
    fipImage hmapImg;

    loadImageFromPhysFS("/map/map.png", mapImg);
    loadImageFromPhysFS("/map/heightmap.png", hmapImg);

    mapImg.flipVertical();
    hmapImg.flipVertical();

    myGenerator->mapImage(mapImg);
    myGenerator->heightmapImage(hmapImg);

    myGenerator->chunkSizeX(400);
    myGenerator->chunkSizeY(240);
    myGenerator->chunkSizeZ(8);

    myGenerator->depth(128);

    if (boost::filesystem::exists(saveDir / "generator"))
      boost::filesystem::remove(saveDir / "generator");

    myGenerator->init();
  }

  void Game::loadMap()
  {
    MapCell bg;
    bg.clear();
    myMap.reset(new Map(engine(), saveDir / "map", true, 512, 512, 32, bg));

    myGenerator.reset(new MapGenerator(shared_from_this()));

    if (boost::filesystem::exists(saveDir / "generator"))
    {
      boost::iostreams::file_source fs((saveDir / "generator").native());
      boost::iostreams::filtering_istream os;
      os.push(boost::iostreams::bzip2_decompressor());
      os.push(fs);
      boost::archive::binary_iarchive ia(os);
      ia & *myGenerator;
    }

    myGenerator->init();
  }

  void Game::saveMap()
  {
    myGenerator->abort();
    myMap->save();

    boost::iostreams::file_sink fs((saveDir / "generator").native());
    boost::iostreams::filtering_ostream os;
    os.push(boost::iostreams::bzip2_compressor());
    os.push(fs);
    boost::archive::binary_oarchive oa(os);
    oa & *myGenerator;

    myGenerator->notifySave();
  }

  void Game::load(const std::string & fileName)
  {

  }

  void Game::save(const std::string & fileName)
  {
    saveMap();
  }

  void Game::loadSkills(const Json::Value & skills)
  {
    const std::string errorMessage = "Error parsing 'skills.json': ";

    PhysFS::ifstream fschema("/schema/skills.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fschema, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/skills.json':\n" + reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/skills.json", skills);

    try
    {
      for (auto value : skills)
      {
        Skill * skill = Skill::parse(value);
        if (mySkills.find(skill->name) != mySkills.end())
        {
          delete skill;
          throw ParsingException("Skill '" + skill->name + "' is already defined");
        }
        else
          mySkills.insert( {skill->name, skill});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadProfessions(const Json::Value & professions)
  {
    const std::string errorMessage = "error parsing 'professions.json': ";

    PhysFS::ifstream fschema("/schema/professions.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fschema, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/professions.json':\n" + reader.getFormattedErrorMessages() ;
      return;
    }

    validateJsonSchema(schema, "/professions.json", professions);

    try
    {
      for (auto value : professions)
      {
        Profession * profession = Profession::parse(value);
        if (myProfessions.find(profession->name) != myProfessions.end())
        {
          delete profession;
          throw ParsingException("Profession '" + profession->name + "' is already defined");
        }
        myProfessions.insert( {profession->name, profession});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadRaces(const Json::Value & races)
  {
    const std::string errorMessage = "error parsing 'races.json': ";

    PhysFS::ifstream fschema("/schema/races.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fschema, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/races.json':\n" + reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/races.json", races);

    try
    {
      for (auto value : races)
      {
        Race * race = Race::parse(value);
        if (myRaces.find(race->name) != myRaces.end())
        {
          delete race;
          throw ParsingException("Race '" + race->name + "' is already defined");
        }
        myRaces.insert( { race->name, race});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadFactions(const Json::Value & factions)
  {
    const std::string errorMessage = "error parsing 'factions.json': ";

    PhysFS::ifstream fschema("/schema/factions.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fschema, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/factions.json':\n" + reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/factions.json", factions);

    try
    {
      for (auto value : factions)
      {
        Faction * faction = Faction::parse(value);
        if (myFactions.find(faction->name) != myFactions.end())
        {
          delete faction;
          throw ParsingException("faction '" + faction->name + "' is already defined");
        }
        myFactions.insert( { faction->name, faction});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadElements(const Json::Value & elements)
  {
    Element * air = new Element;
    dispEntry airDisp = {' ', { Cyan, Cyan, Style::Dim }};

    air->disp[TerrainType::Floor] = { airDisp };
    air->disp[TerrainType::Wall] = { airDisp };
    air->disp[TerrainType::Hole] = { airDisp };
    air->disp[TerrainType::RampD] = { airDisp };
    air->disp[TerrainType::RampU] = { airDisp };
    air->style = { Cyan, Cyan, Style::Dim };
    air->desc = "Air";

    myElements.insert({"Air", air});

    const std::string errorMessage = "error parsing 'elements.json': ";

    PhysFS::ifstream felements("/schema/elements.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(felements, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/elements.json':\n" +
        reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/elements.json", elements);

    try
    {
      for (auto value : elements)
      {
        Element * element = Element::parse(value);
        if (myElements.find(element->name) != myElements.end())
        {
          delete element;
          throw ParsingException("Element '" + element->name + "' is already defined");
        }
        else
          myElements.insert( {element->name, element});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadMaterials(const Json::Value & materials)
  {
    const std::string errorMessage = "error parsing 'materials.json': ";

    PhysFS::ifstream fmaterials("/schema/materials.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fmaterials, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/materials.json':\n" + reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/materials.json", materials);

    try
    {
      for (auto value : materials)
      {
        Material * material = Material::parse(value);
        if (myMaterials.find(material->name) != myMaterials.end())
        {
          delete material;
          throw ParsingException("Material '" + material->name + "' is already defined");
        }
        else
          myMaterials.insert( {material->name, material});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::loadBiomes(const Json::Value biomes)
  {

    const std::string errorMessage = "error parsing 'biomes.json': ";

    PhysFS::ifstream fbiomes("/schema/biomes.json");
    Json::Reader reader;
    Json::Value schema;

    if (!reader.parse(fbiomes, schema))
    {
      engine()->log("Game", LogLevel::Fatal), "error parsing schema 'schema/biomes.json':\n" + reader.getFormattedErrorMessages();
      return;
    }

    validateJsonSchema(schema, "/biomes.json", biomes);

    try
    {
      for (auto value : biomes)
      {
        Biome * biome = Biome::parse(value);
        if (myBiomes.find(biome->name) != myBiomes.end())
        {
          delete biome;
          throw ParsingException("Biome '" + biome->name + "' is already defined");
        }
        else
          myBiomes.insert( {biome->name, biome});
      }
    }
    catch (std::runtime_error & e)
    {
      throw ParsingException(errorMessage + e.what());
    }
  }

  void Game::sanityCheck()
  {
    for (auto const & s : mySkills)
      for (auto const & d : s.second->dependencies)
        if (mySkills.find(d) == mySkills.end())
          throw ParsingException("skill dependency '" + d + "' for skill '" + s.second->name + "' not found");
    for (auto const & p : myProfessions)
      for (auto const & s : p.second->skills)
        if (mySkills.find(s) == mySkills.end())
          throw ParsingException("skill '" + s + "' for profession '" + p.second->name + "' not found");
    for (auto const & r : myRaces)
      for (auto const & p : r.second->allowedProfessions)
        if (myProfessions.find(p) == myProfessions.end())
          throw ParsingException("profession '" + p + "' for race '" + r.second->name + "' not found");
    for (auto const & r : myRaces)
      for (auto const & f : r.second->factions)
        if (myFactions.find(f) == myFactions.end())
          throw ParsingException("faction '" + f + "' for race '" + r.second->name + "' not found");
   for (auto const & m : myMaterials)
     for (auto const & s: m.second->states)
       for (auto const & e : s.second)
         if (myElements.find(e) == myElements.end())
           throw ParsingException("element '" + e + "' for material '" + m.second->name + "' not found");
  }

  Skill * Skill::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());
    else
    {

      Skill * skill = new Skill { name };

      if (!value["description"].empty())
        skill->desc = value["description"].asString();

      if (!value["dispName"].empty())
        skill->dispName = value["dispName"].asString();
      else
        skill->dispName = skill->name;

      if (!value["title"].empty())
        skill->title = value["title"].asString();
      else
        skill->title = skill->name;

      if (!value["activeSkill"].empty())
        skill->activeSkill = value["activeSkill"].asBool();
      else
        skill->activeSkill = false;

      if (!value["dependencies"].empty())
      {
        for (auto dep : value["dependencies"])
        {
          std::string dependency = dep.asString();
          if (trim(dependency).empty())
          {
            delete skill;
            throw ParsingException("empty name provided for skill dependency\n" + value.toStyledString());
          }
          skill->dependencies.push_back(dependency);
        }
      }

      skill->jsonValue = value;
      return skill;
    }
  }

  Profession * Profession::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());
    else
    {
      Profession * profession = new Profession { name };

      if (!value["description"].empty())
        profession->desc = value["description"].asString();

      for (unsigned int j = 0; j < Profession::NameKind::NameKindMax; j++)
      {
        std::string dispName = value["dispName"][j].asString();
        if (!trim(dispName).empty())
          profession->dispName[j] = dispName;
        else
        {
          delete profession;
          throw ParsingException("problem with entry '" + profession->name + "':\n attribute 'dispName' must be"
                                  " an array of 3 non-empty strings\n" + value.toStyledString());
        }
      }

      if (!value["maleOnly"].empty())
        profession->maleOnly = value["maleOnly"].asBool();
      else
        profession->maleOnly = false;

      if (!value["femaleOnly"].empty())
        profession->maleOnly = value["femaleOnly"].asBool();
      else
        profession->femaleOnly = false;

      profession->jsonValue = value;

      return profession;
    }
  }

  Faction * Faction::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());

    Faction * faction = new Faction;

    faction->name = name;

    if (!value["dispName"].empty())
      faction->dispName = value["dispName"].asString();
    else
      faction->dispName = name;
    if (!value["description"].empty())
      faction->desc = value["description"].asString();
    if (!value["surname"].empty())
      faction->surname = value["surname"].asString();
    if (!value["home"].empty())
      faction->home = value["home"].asString();
    if (!value["joinable"].empty())
      faction->joinable = value["joinable"].asBool();
    else
      faction->joinable = true;

    faction->jsonValue = value;

    return faction;
  }

  Race * Race::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
    {
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());
    }

    Race * race = new Race { name };

    if (!value["description"].empty())
      race->desc = value["description"].asString();

    if (!value["allowedProfessions"].empty())
    {
      for (auto const & profession : value["allowedProfessions"])
      {
        std::string name = profession.asString();
        if (trim(name).empty())
        {
          delete race;
          throw ParsingException("empty name provided for profession\n" + value.toStyledString());
        }
        else
          race->allowedProfessions.push_back(name);
      }
    }

    if (!value["factions"].empty())
    {
      for (auto const & faction : value["factions"])
      {
        std::string name = faction.asString();
        if (trim(name).empty())
        {
          delete race;
          throw ParsingException("empty name provided for faction\n" + value.toStyledString());
        }
        else
          race->factions.push_back(name);
      }
    }

    race->jsonValue = value;
    return race;
  }

  Element * Element::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());

    Element * element = new Element;

    element->name = name;
    element->style = jsonToPalEntry(value["style"]);

    if (!value["description"].empty())
      element->desc = value["description"].asString();

    if (!value["dispName"].empty())
      element->dispName = value["dispName"].asString();

    if (!value["state"].empty())
      element->state = strMaterialState(value["state"].asString());
    else
      element->state = MaterialState::Solid;

    for (auto const & i : value["disp"].getMemberNames())
    {
      for(auto const & o : value["disp"][i])
      {
        std::string u8;
        if (!o["usym"].empty() && options.count("unicode"))
          u8 = o["usym"].asString();
        else
          if (o["sym"].empty() && !options.count("unicode"))
            continue;
        else
          u8 = o["sym"].asString();
#ifdef ADWIF_UNICODE
        int codePoint = utf8::peek_next(u8.begin(), u8.end());
#else
        if (o["sym"].empty()) continue;
        int codePoint = o["sym"].asString()[0];
#endif
        element->disp[strTerrainType(i)].push_back({codePoint, jsonToPalEntry(o["style"], element->style)});
      }
    }

    element->jsonValue = value;

    return element;
  }

  Material * Material::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());

    Material * material = new Material;

    material->name = name;

    if (!value["description"].empty())
      material->desc = value["description"].asString();

    if (!value["dispName"].empty())
      material->dispName = value["dispName"].asString();

    for (auto const & i : value["states"]["solid"])
      material->states[MaterialState::Solid].insert(i.asString());
    for (auto const & i : value["states"]["liquid"])
      material->states[MaterialState::Liquid].insert(i.asString());
    for (auto const & i : value["states"]["gas"])
      material->states[MaterialState::Gas].insert(i.asString());

    material->jsonValue = value;

    return material;
  }

  Biome * Biome::parse(const Json::Value & value)
  {
    std::string name = value["name"].asString();

    if (trim(name).empty())
      throw ParsingException("attribute 'name' undefined or empty\n" + value.toStyledString());

    Biome * biome = new Biome;

    biome->name = name;
    biome->disp.style = jsonToPalEntry(value["style"]);

    if (!value["description"].empty())
      biome->desc = value["description"].asString();

    if (!value["layerStart"].empty())
      biome->layerStart = value["layerStart"].asInt();
    else
      biome->layerStart = 0;

    if (!value["layerEnd"].empty())
      biome->layerEnd = value["layerEnd"].asInt();
    else
      biome->layerEnd = 0;

    if (!value["mapColour"].empty())
      biome->mapColour = hexStringToColour(value["mapColour"].asString());
    else
      biome->mapColour = 0x000000;

    if (!value["background"].empty())
      biome->background = value["background"].asBool();
    else
      biome->background = false;

    if (!value["aquatic"].empty())
      biome->aquatic = value["aquatic"].asBool();
    else
      biome->aquatic = false;

    std::string u8;
#ifdef ADWIF_UNICODE
    if (value["usym"].empty())
      u8 = value["sym"].asString();
    else
      u8 = value["usym"].asString();
    std::u32string codePoint;
    utf8::utf8to32(u8.begin(), u8.end(), codePoint.begin());
    biome->disp.sym = codePoint[0];
#else
    u8 = value["sym"].asString();
    biome->disp.sym = u8[0];
#endif

    for (auto const & i : value["materials"])
      biome->materials.push_back(i.asString());

    for (auto const & i : value["liquids"])
      biome->liquids.push_back(i.asString());

    biome->jsonValue = value;

    return biome;
  }

}


