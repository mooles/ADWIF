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

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <vector>
#include <memory>
#include <sstream>

#include <boost/thread.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/thread/recursive_mutex.hpp>

#include "scheduler.hpp"
#include "config.hpp"

namespace ADWIF
{
  enum LogLevel
  {
    Debug,
    Info,
    Error,
    Fatal
  };

#ifdef ADWIF_LOGGING
  class Log
  {
  public:
    Log(const std::shared_ptr<const class Engine> & engine, const std::string & source, LogLevel level = LogLevel::Info)
      : myEngine(engine), mySource(source), myLevel(level) { }
    Log(const Log & other): myMessage(other.myMessage.str()), myEngine(other.myEngine),
      myLevel(other.myLevel), mySource(other.mySource) { other.myEngine.reset(); }
    Log& operator= (const Log & other) {
      myMessage << other.myMessage.str();
      myEngine = other.myEngine;
      myLevel = other.myLevel;
      mySource = other.mySource;
      other.myEngine.reset();
      return *this;
    }
    ~Log() { flush(); }
    template <typename T>
    Log& operator, (const T & message) { myMessage << message; return *this; }
    void flush();
  private:
    mutable std::shared_ptr<const class Engine> myEngine;
    mutable std::ostringstream myMessage;
    mutable std::string mySource;
    mutable LogLevel myLevel;
  };
#else
  class Log
  {
  public:
    Log(const std::shared_ptr<const class Engine> & engine, const std::string & source, LogLevel level = LogLevel::Info) { }
    Log(const Log & other) {  }
    Log& operator= (const Log & other) {  return *this; }
    ~Log() { }
    template <typename T>
    Log& operator, (const T & message) { }
    void flush() { }
  };
#endif

  class LogProvider
  {
  public:
    virtual void logMessage(LogLevel level, const std::string & source, const std::string & message) = 0;
  };

  class StdErrLogProvider: public LogProvider
  {
  public:
    virtual void logMessage(LogLevel level, const std::string & source, const std::string & message)
    {
      boost::recursive_mutex::scoped_lock guard (myLogMutex);
      switch(level)
      {
        case LogLevel::Debug: std::cerr << "[Debug] "; break;
        case LogLevel::Info: std::cerr << "[Info] "; break;
        case LogLevel::Error: std::cerr << "[Error] "; break;
        case LogLevel::Fatal: std::cerr << "[Fatal] "; break;
      }
      std::cerr << source << ": " << message << std::endl;
    }
  private:
    boost::recursive_mutex myLogMutex;
  };

  class Engine: public std::enable_shared_from_this<Engine>
  {
  public:
    Engine(const std::shared_ptr<class Renderer> renderer, const std::shared_ptr<class Input> input);
    ~Engine();

    std::shared_ptr<class Renderer> renderer() const { return myRenderer; }
    std::shared_ptr<class Input> input() const { return myInput; }

    unsigned int delay() const;
    void delay(unsigned int delay);

    bool running() const { return myRunningFlag; }
    void running(bool r) { myRunningFlag = r; }

    int start();

    void sleep(unsigned ms);

    void addState(std::shared_ptr<class GameState> & state);

    Log log(const std::string & source, LogLevel level = LogLevel::Info) const { return Log(shared_from_this(), source, level); }
    void logMessage(LogLevel level, const std::string & source, const std::string & msg) const
    {
      myLog->logMessage(level, source, msg);
    }

    std::shared_ptr<boost::cgl::scheduler> scheduler() const { return myScheduler; }
    boost::asio::io_service & service() const { return myScheduler->io_service(); }

    const std::shared_ptr<LogProvider> & logProvider() const { return myLog; }
    void logProvider(const std::shared_ptr<LogProvider> & provider) { myLog = provider; }

  private:
    bool checkScreenSize();
  private:
    std::shared_ptr<class Renderer> myRenderer;
    std::shared_ptr<class Input> myInput;
    std::vector <
      std::shared_ptr<class GameState>
    > myStates;
    unsigned int myDelay;
    bool myRunningFlag;
    std::shared_ptr<boost::cgl::scheduler> myScheduler;
    mutable std::shared_ptr<LogProvider> myLog;
  };
}

#endif // ENGINE_HPP
