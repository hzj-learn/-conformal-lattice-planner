/*
 * Copyright 2020 Ke Sun
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string>
#include <boost/format.hpp>
#include <algorithm>
#include <stdexcept>
#include <carla/road/Road.h>
#include <carla/road/Lane.h>
#include <router/loop_router/loop_router.h>

namespace router {

LoopRouter::LoopRouter() :
  road_sequence_({47, 558, 48, 887, 49, 717, 50, 42, 276, 43, 35, 636, 36,
                  540, 37, 1021, 38, 678, 39, 728, 40, 841, 41, 6, 45, 103,
                  46, 659}){ return; }

boost::shared_ptr<LoopRouter::CarlaWaypoint> LoopRouter::waypointOnRoute(
    const boost::shared_ptr<const CarlaWaypoint>& waypoint) const {

  std::vector<boost::shared_ptr<CarlaWaypoint>> candidates = waypoint->GetNext(0.01);
  for (const auto& candidate : candidates) {
    std::vector<size_t>::const_iterator iter = std::find(
        road_sequence_.begin(), road_sequence_.end(), candidate->GetRoadId());
    if (iter != road_sequence_.end())
      return candidate;
  }

  return nullptr;
}

boost::optional<size_t> LoopRouter::nextRoad(const size_t road) const {
  std::vector<size_t>::const_iterator iter = std::find(
      road_sequence_.begin(), road_sequence_.end(), road);
  if (iter == road_sequence_.end())
    throw std::runtime_error((boost::format(
            "LoopRouter::nextRoad(): "
            "given road %1% is not on the route.\n") % road).str());

  if (iter != road_sequence_.end()-1) return *(++iter);
  else return road_sequence_.front();
}

boost::optional<size_t> LoopRouter::prevRoad(const size_t road) const {
  std::vector<size_t>::const_iterator iter = std::find(
      road_sequence_.begin(), road_sequence_.end(), road);
  if (iter == road_sequence_.end())
    throw std::runtime_error((boost::format(
            "LoopRouter::nextRoad(): "
            "given road %1% is not on the route.\n") % road).str());

  if (iter != road_sequence_.begin()) return *(--iter);
  else return road_sequence_.back();
}

boost::optional<size_t> LoopRouter::nextRoad(
    const boost::shared_ptr<const CarlaWaypoint>& waypoint) const {
  return nextRoad(waypoint->GetRoadId());
}

boost::optional<size_t> LoopRouter::prevRoad(
    const boost::shared_ptr<const CarlaWaypoint>& waypoint) const {
  return prevRoad(waypoint->GetRoadId());
}

boost::shared_ptr<LoopRouter::CarlaWaypoint> LoopRouter::frontWaypoint(
    const boost::shared_ptr<const CarlaWaypoint>& waypoint,
    const double distance) const {

  if (distance <= 0.0) {
    std::string error_msg("LoopRouter::frontWaypoint(): distance < 0 when searching for the front waypoint.\n");
    std::string waypoint_msg =
      (boost::format("waypoint %1% x:%2% y:%3% z:%4% r:%5% p:%6% y:%7% road:%8% lane:%9%.\n")
       % waypoint->GetId()
       % waypoint->GetTransform().location.x
       % waypoint->GetTransform().location.y
       % waypoint->GetTransform().location.z
       % waypoint->GetTransform().rotation.roll
       % waypoint->GetTransform().rotation.pitch
       % waypoint->GetTransform().rotation.yaw
       % waypoint->GetRoadId()
       % waypoint->GetLaneId()).str();
    std::string distance_msg = (boost::format("Distance:%1%\n") % distance).str();
    throw std::runtime_error(error_msg + waypoint_msg + distance_msg);
  }

  std::vector<boost::shared_ptr<CarlaWaypoint>> candidates = waypoint->GetNext(distance);
  const size_t this_road = waypoint->GetRoadId();

  boost::optional<size_t> is_next_road = nextRoad(this_road);
  const size_t next_road = is_next_road ? *is_next_road : -1;

  boost::shared_ptr<CarlaWaypoint> next_waypoint = nullptr;
  for (const auto& candidate : candidates) {
    // If we find a candidate on the same road with the given waypoint, this is it.
    if (candidate->GetRoadId() == this_road) return candidate;
    // Otherwise we keep track of which candidate is on the next road.
    if (candidate->GetRoadId() == next_road) next_waypoint = candidate;
  }

  return next_waypoint;
}

} // End namespace router.
