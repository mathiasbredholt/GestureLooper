/*
 MapLooper - Embedded Live-Looping Tools for Digital Musical Instruments
 Copyright (C) 2020 Mathias Bredholt

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "mapper/mapper.h"

namespace MapLooper {
class Loop {
 public:
  Loop(const char* name, mpr_dev dev, mpr_type type, int vectorSize)
      : _graph(mpr_obj_get_graph(dev)), _type(type), _vectorSize(vectorSize) {
    char sigName[128];
    float sigMin = 0.0f, sigMax = 1.0f;

    float lengthMin = 0.0f, lengthMax = 100.0f;
    float divisionMin = 1.0f, divisionMax = 96.0f;

    float defaultDivision = 16.0f;
    float defaultLength = 1.0f;

    int muteMin = 0, muteMax = 1;

    // Create control signals
    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "control/record");
    _sigRecord = mpr_sig_new(dev, MPR_DIR_OUT, sigName, 1, MPR_FLT, 0, &sigMin,
                             &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigRecord, 0, 1, MPR_FLT, &sigMin);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "control/length");
    _sigLength = mpr_sig_new(dev, MPR_DIR_OUT, sigName, 1, MPR_FLT, "beats",
                             &lengthMin, &lengthMax, 0, 0, 0);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "control/division");
    _sigDivision = mpr_sig_new(dev, MPR_DIR_OUT, sigName, 1, MPR_FLT, "ppqn",
                               &divisionMin, &divisionMax, 0, 0, 0);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "control/modulation");
    _sigModulation = mpr_sig_new(dev, MPR_DIR_OUT, sigName, 1, MPR_FLT, 0,
                                 &sigMin, &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigModulation, 0, 1, MPR_FLT, &sigMin);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "control/mute");
    _sigMute = mpr_sig_new(dev, MPR_DIR_OUT, sigName, 1, MPR_INT32, 0, &muteMin,
                           &muteMax, 0, 0, 0);
    mpr_sig_set_value(_sigMute, 0, 1, MPR_INT32, &muteMin);

    // Create input and output signals
    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "input");
    _sigIn = mpr_sig_new(dev, MPR_DIR_IN, sigName, _vectorSize, _type, 0,
                         &sigMin, &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigIn, 0, _vectorSize, _type, &sigMin);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "output");
    _sigOut = mpr_sig_new(dev, MPR_DIR_OUT, sigName, _vectorSize, _type, 0,
                          &sigMin, &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigOut, 0, _vectorSize, _type, &sigMin);

    // Create local send/receive signals
    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "local/send");
    _sigLocalSend = mpr_sig_new(dev, MPR_DIR_OUT, sigName, _vectorSize, _type, 0,
                              &sigMin, &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigLocalSend, 0, _vectorSize, _type, &sigMin);

    std::snprintf(sigName, sizeof(sigName), "%s/%s", name, "local/recv");
    _sigLocalReceive = mpr_sig_new(dev, MPR_DIR_IN, sigName, _vectorSize, _type, 0,
                               &sigMin, &sigMax, 0, 0, 0);
    mpr_sig_set_value(_sigLocalReceive, 0, _vectorSize, _type, &sigMin);

    // Create map
    _loopMap = mpr_map_new_from_str(
        "del=_%x*_%x;%y=_%x*%x+(1-_%x)*y{-del,100}+_%x*(uniform(2.0)-1)",
        _sigLength, _sigDivision, _sigLocalReceive, _sigRecord, _sigLocalSend,
        _sigRecord, _sigModulation);
    mpr_obj_push(_loopMap);

    while (!mpr_map_get_is_ready(_loopMap)) {
      mpr_dev_poll(dev, 10);
    }

    // Length and division must be set after map initialization
    mpr_sig_set_value(_sigLength, 0, 1, MPR_FLT, &defaultLength);
    mpr_sig_set_value(_sigDivision, 0, 1, MPR_FLT, &defaultDivision);
  }

  ~Loop() {
    mpr_sig_free(_sigRecord);
    mpr_sig_free(_sigLength);
    mpr_sig_free(_sigModulation);
    mpr_sig_free(_sigDivision);
    mpr_sig_free(_sigIn);
    mpr_sig_free(_sigOut);
    mpr_sig_free(_sigLocalReceive);
    mpr_sig_free(_sigLocalSend);
  }

  void mapRecord(const char* src) { _mapFrom(src, &_sigRecord); }

  void mapLength(const char* src) { _mapFrom(src, &_sigLength); }

  void mapModulation(const char* src) { _mapFrom(src, &_sigModulation); }

  void mapInput(const char* src) { _mapFrom(src, &_sigIn); }

  void mapOutput(const char* dst) { _mapTo(&_sigOut, dst); }

  void update(double beats) {
    float division = *((float*)mpr_sig_get_value(_sigDivision, 0, 0));

    int now = beats * division;
    if (now != _lastUpdate) {
      // Check if ticks were missed
      if (now - _lastUpdate > 1) {
        printf("Missed %d ticks!\n", now - _lastUpdate - 1);
      }

      // Update local out
      const void* inputValue = mpr_sig_get_value(_sigIn, 0, 0);
      mpr_sig_set_value(_sigLocalSend, 0, _vectorSize, _type, inputValue);

      _lastUpdate = now;
    }

    // Check if muted
    bool muted = *((int*)mpr_sig_get_value(_sigMute, 0, 0));
    if (!muted) {
      const void* outputValue = mpr_sig_get_value(_sigLocalReceive, 0, 0);
      mpr_sig_set_value(_sigOut, 0, _vectorSize, _type, outputValue);
    }
  }

  mpr_sig getInputSignal() { return _sigIn; }

  mpr_sig getOutputSignal() { return _sigOut; }

  mpr_sig getModulationSignal() { return _sigModulation; }

  mpr_sig getDivisionSignal() { return _sigDivision; }

  mpr_sig getLengthSignal() { return _sigLength; }

  mpr_sig getRecordSignal() { return _sigRecord; }

  mpr_sig getMuteSignal() { return _sigMute; }

 private:
  void _mapFrom(const char* src, mpr_sig* dst) {
    struct MapData {
      const char* src;
      mpr_sig* dst;
    };
    MapData* mapData = new MapData{src, dst};

    mpr_graph_handler* handler = [](mpr_graph g, mpr_obj obj,
                                    const mpr_graph_evt evt, const void* data) {
      MapData* mapData = reinterpret_cast<MapData*>(const_cast<void*>(data));
      const char* found = mpr_obj_get_prop_as_str(obj, MPR_PROP_NAME, 0);
      if (strcmp(mapData->src, found) == 0) {
        mpr_obj_push(mpr_map_new(1, (mpr_sig*)&obj, 1, mapData->dst));
      }
    };
    mpr_graph_add_cb(_graph, handler, MPR_SIG, mapData);
  }

  void _mapTo(mpr_sig* src, const char* dst) {
    struct MapData {
      mpr_sig* src;
      const char* dst;
    };
    MapData* mapData = new MapData{src, dst};

    mpr_graph_handler* handler = [](mpr_graph g, mpr_obj obj,
                                    const mpr_graph_evt evt, const void* data) {
      MapData* mapData = reinterpret_cast<MapData*>(const_cast<void*>(data));
      const char* found = mpr_obj_get_prop_as_str(obj, MPR_PROP_NAME, 0);
      if (strcmp(mapData->dst, found) == 0) {
        mpr_obj_push(mpr_map_new(1, mapData->src, 1, (mpr_sig*)&obj));
      }
    };
    mpr_graph_add_cb(_graph, handler, MPR_SIG, mapData);
  }

  mpr_graph _graph;
  mpr_map _loopMap;
  mpr_sig _sigRecord;
  mpr_sig _sigLength;
  mpr_sig _sigModulation;
  mpr_sig _sigDivision;
  mpr_sig _sigIn;
  mpr_sig _sigOut;
  mpr_sig _sigLocalReceive;
  mpr_sig _sigLocalSend;
  mpr_sig _sigMute;

  int _lastUpdate = 0;

  mpr_type _type;
  int _vectorSize;
};
}  // namespace MapLooper
