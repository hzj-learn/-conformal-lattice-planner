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

#pragma once

#include <planner/common/traffic_lattice.h>

namespace planner {

/**
 * \brief TrafficManager is a helper class used to manage local traffic (vehicles)
 *        in a simulator.
 *
 * The class provides interface to add and delete vehicles on the lattice.
 * Meanwhile, the class is able to suggest locations to spawn new vehicles at
 * either front or back of the lattice.
 */
class TrafficManager : public TrafficLattice {

protected:

  using CarlaVehicle     = carla::client::Vehicle;
  using CarlaMap         = carla::client::Map;
  using CarlaWaypoint    = carla::client::Waypoint;
  using CarlaBoundingBox = carla::geom::BoundingBox;
  using CarlaTransform   = carla::geom::Transform;
  using CarlaRoad        = carla::road::Road;
  using CarlaLane        = carla::road::Lane;
  using CarlaRoadMap     = carla::road::Map;
  using CarlaMapData     = carla::road::MapData;

  using Node = WaypointNodeWithVehicle;
  /// FIXME: Don't really want to define a new struct for this.
  ///        Is there a better solution than \c tuple?
  using VehicleTuple = std::tuple<size_t, CarlaTransform, CarlaBoundingBox>;

  /// Stores the three waypoints for a vehicle. From index 0-2 are the
  /// waypoints corresponding to the vehicle rear, middle, and head.
  /// In the case a waypoint is missing, leave \c nullptr at the correspoinding
  /// entry.
  using VehicleWaypoints = std::array<boost::shared_ptr<CarlaWaypoint>, 3>;

private:

  using Base = TrafficLattice;

public:

  // Lift some protected functions in the base class into public.
  using Base::front;
  using Base::back;
  using Base::leftFront;
  using Base::frontLeft;
  using Base::leftBack;
  using Base::backLeft;
  using Base::rightFront;
  using Base::frontRight;
  using Base::rightBack;
  using Base::backRight;

  /**
   * \brief class constructor.
   * \param[in] start Start waypoint of the lattice.
   * \param[in] range Range of the lattice to be created.
   * \param[in] router A router object giving the road sequences.
   * \param[in] map A carla map object used to query roads and lanes.
   * \param[in] fast_map The fast map is to find waypoints based on locations.
   */
  TrafficManager(const boost::shared_ptr<CarlaWaypoint>& start,
                 const double range,
                 const boost::shared_ptr<router::Router>& router,
                 const boost::shared_ptr<CarlaMap>& map,
                 const boost::shared_ptr<utils::FastWaypointMap>& fast_map);

  /**
   * \brief Update the the vehcile postions in the lattice.
   *
   * The lattice will be modified to adapte to the update vehicle positions if necessary.
   * However, the range of the lattice won't change. The lattice will only be shifted
   * forward.
   *
   * \param[in] vehicles The updated vehicle transforms. The IDs of the input vehicles
   *                     should exactly match the IDs of the vehicles that is currently
   *                     being tracked by the object.
   * \param[in] shift_distance The distance to shift the lattice forward. For example,
   *                           if the caller wants to maintain a vehicle at a constant
   *                           distance on the lattice. The \c shift_distance should
   *                           be the distance that this vehicle has travelled between
   *                           the calls of this function.
   * \param[in] disappear_vehicles The vehicles that no longer stays on the lattice.
   *                               These vehicles are removed after calling this function.
   * \return False if a collision is detected after calling this function. In this case
   *         the object is no longer in a valid state and should not be used onwards.
   */
  bool moveTrafficForward(
      const std::vector<VehicleTuple>& vehicles,
      const double shift_distance,
      boost::optional<std::unordered_set<size_t>&> disappear_vehicles = boost::none);

  /**
   * \brief Update the the vehcile postions in the lattice.
   *
   * The lattice will be modified to adapte to the update vehicle positions if necessary.
   * However, the range of the lattice won't change. The lattice will only be shifted
   * forward.
   *
   * \param[in] vehicles The updated vehicle transforms. The IDs of the input vehicles
   *                     should exactly match the IDs of the vehicles that is currently
   *                     being tracked by the object.
   * \param[in] shift_distance The distance to shift the lattice forward. For example,
   *                           if the caller wants to maintain a vehicle at a constant
   *                           distance on the lattice. The \c shift_distance should
   *                           be the distance that this vehicle has travelled between
   *                           the calls of this function.
   * \param[in] disappear_vehicles The vehicles that no longer stays on the lattice.
   *                               These vehicles are removed after calling this function.
   * \return False if a collision is detected after calling this function. In this case
   *         the object is no longer in a valid state and should not be used onwards.
   */
  bool moveTrafficForward(
      const std::vector<boost::shared_ptr<const CarlaVehicle>>& vehicles,
      const double shift_distance,
      boost::optional<std::unordered_set<size_t>&> disappear_vehicles = boost::none);

  /**
   * \brief Suggest a waypoint to spawn a new vehicle at the front of the lattice.
   *
   * The \c min_range puts a threshold on the distance of the back vehicle to this
   * waypoint. In the case that a back vehicle is too close, the waypoint is considered
   * in valid. If multiple waypoints at the front of the lattice satisfy the
   * requirement, the one with the farthest back vehicle is returned.
   *
   * \param[in] min_range The tolerate distance between the spawned waypoint and the back vehicle.
   * \return If there is a waypoint found which meets the requirement, it will be returned
   *         together with the distance to the vehicle at its back. If there is no vehicle
   *         at its back, the returned distance will be the range of the lattice.
   */
  boost::optional<std::pair<double, boost::shared_ptr<const CarlaWaypoint>>>
    frontSpawnWaypoint(const double min_range) const;

  /**
   * \brief Suggest a waypoint to spawn a new vehicle at the back of the lattice.
   *
   * The \c min_range puts a threshold on the distance of the front vehicle to this
   * waypoint. In the case that a front vehicle is too close, the waypoint is considered
   * in valid. If multiple waypoints at the back of the lattice satisfy the
   * requirement, the one with the farthest front vehicle is returned.
   *
   * \param[in] min_range The tolerate distance between the spawned waypoint and the front vehicle.
   * \return If there is a waypoint found which meets the requirement, it will be returned
   *         together with the distance to the vehicle at its front. If there is no vehicle
   *         at its front, the returned distance will be the range of the lattice.
   */
  boost::optional<std::pair<double, boost::shared_ptr<const CarlaWaypoint>>>
    backSpawnWaypoint(const double min_range) const;

}; // End class TrafficManager.

} // End namespace planner.
