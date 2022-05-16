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

#include <cmath>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

#include <planner/common/traffic_lattice.h>

namespace planner {

TrafficLattice::TrafficLattice(
    const std::vector<VehicleTuple>& vehicles,
    const boost::shared_ptr<CarlaMap>& map,
    const boost::shared_ptr<utils::FastWaypointMap>& fast_map,
    const boost::shared_ptr<router::Router>& router,
    boost::optional<std::unordered_set<size_t>&> disappear_vehicles) :
  map_(map), fast_map_(fast_map) {

  this->router_ = router;

  // Find the waypoints each of the input vehicle.
  std::unordered_map<size_t, VehicleWaypoints>
    vehicle_waypoints = vehicleWaypoints(vehicles);

  // Find the start waypoint and range of the lattice based
  // on the given vehicles.
  boost::shared_ptr<CarlaWaypoint> start_waypoint = nullptr;
  double range = 0.0;
  latticeStartAndRange(vehicles, vehicle_waypoints, start_waypoint, range);

  // Now we can construct the lattice.
  // FIXME: The following is just a copy of the Lattice custom constructor.
  //        Can we avoid this code duplication?
  baseConstructor(start_waypoint, range, 1.0, router);

  // Register the vehicles onto the lattice nodes.
  std::unordered_set<size_t> remove_vehicles;
  if(!registerVehicles(vehicles, vehicle_waypoints, remove_vehicles)) {
    std::string error_msg(
        "TrafficLattice::TrafficLattice(): "
        "collision detected within the given vehicles.\n");

    std::string vehicle_msg;
    boost::format vehicle_format("vehicle %1%: x:%2% y:%3% z:%4% r:%5% p:%6% y:%7%.\n");

    for (const auto& vehicle : vehicles) {
      size_t id; CarlaTransform transform;
      std::tie(id, transform, std::ignore) = vehicle;
      vehicle_msg += (vehicle_format
          % id
          % transform.location.x
          % transform.location.y
          % transform.location.z
          % transform.rotation.roll
          % transform.rotation.pitch
          % transform.rotation.yaw).str();
    }

    throw std::runtime_error(error_msg + vehicle_msg);
  }
  if (disappear_vehicles) *disappear_vehicles = remove_vehicles;

  return;
}

TrafficLattice::TrafficLattice(
    const std::vector<boost::shared_ptr<const CarlaVehicle>>& vehicles,
    const boost::shared_ptr<CarlaMap>& map,
    const boost::shared_ptr<utils::FastWaypointMap>& fast_map,
    const boost::shared_ptr<router::Router>& router,
    boost::optional<std::unordered_set<size_t>&> disappear_vehicles) :
  map_(map), fast_map_(fast_map) {

  this->router_ = router;

  std::vector<VehicleTuple> vehicle_tuples;
  for (const auto& vehicle : vehicles) {
    vehicle_tuples.push_back(std::make_tuple(
          vehicle->GetId(),
          vehicle->GetTransform(),
          vehicle->GetBoundingBox()));
  }

  // Find waypoints for each of the input vehicle.
  std::unordered_map<size_t, VehicleWaypoints>
    vehicle_waypoints = vehicleWaypoints(vehicle_tuples);

  // Find the start waypoint and range of the lattice based
  // on the given vehicles.
  boost::shared_ptr<CarlaWaypoint> start_waypoint = nullptr;
  double range = 0.0;
  latticeStartAndRange(vehicle_tuples, vehicle_waypoints, start_waypoint, range);

  // Now we can construct the lattice.
  // FIXME: The following is just a copy of the Lattice custom constructor.
  //        Can we avoid this code duplication?
  baseConstructor(start_waypoint, range, 1.0, router);

  // Register the vehicles onto the lattice nodes.
  std::unordered_set<size_t> remove_vehicles;
  if(!registerVehicles(vehicle_tuples, vehicle_waypoints, remove_vehicles)) {
    std::string error_msg(
        "TrafficLattice::TrafficLattice(): "
        "collision detected within the given vehicles.\n");

    std::string vehicle_msg;
    boost::format vehicle_format("vehicle %1%: x:%2% y:%3% z:%4% r:%5% p:%6% y:%7%.\n");

    for (const auto& vehicle : vehicle_tuples) {
      size_t id; CarlaTransform transform;
      std::tie(id, transform, std::ignore) = vehicle;
      vehicle_msg += (vehicle_format
          % id
          % transform.location.x
          % transform.location.y
          % transform.location.z
          % transform.rotation.roll
          % transform.rotation.pitch
          % transform.rotation.yaw).str();
    }

    throw std::runtime_error(error_msg + vehicle_msg);
  }
  if (disappear_vehicles) *disappear_vehicles = remove_vehicles;

  return;
}

TrafficLattice::TrafficLattice(const TrafficLattice& other) :
  Base(other) {

  // Make sure the weak pointers point to the stuff within this object.
  vehicle_to_nodes_table_ = other.vehicle_to_nodes_table_;

  for (auto& vehicle : vehicle_to_nodes_table_) {
    for (auto& node : vehicle.second) {
      const size_t id = node.lock()->waypoint()->GetId();
      node = this->waypoint_to_node_table_[id];
    }
  }

  // Carla map and fast map won't be copied. \c map_ of different objects point to the
  // same piece of memory.
  map_ = other.map_;
  fast_map_ = other.fast_map_;

  return;
}

void TrafficLattice::swap(TrafficLattice& other) {

  Base::swap(other);
  std::swap(vehicle_to_nodes_table_, other.vehicle_to_nodes_table_);
  std::swap(map_, other.map_);
  std::swap(fast_map_, other.fast_map_);

  return;
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::front(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::front(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // head of the vehicle.
  boost::shared_ptr<const Node> start = vehicleHeadNode(vehicle);
  return frontVehicle(start);
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::back(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::back(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // back of the vehicle.
  boost::shared_ptr<const Node> start = vehicleRearNode(vehicle);
  return backVehicle(start);
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::leftFront(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::leftFront(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // head of the vehicle.
  boost::shared_ptr<const Node> start = vehicleHeadNode(vehicle);
  if (!start) {
    std::string error_msg = (boost::format(
          "TrafficLattice::leftFront(): "
          "head of vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the left node of the start.
  // If there is no left node, there is no left front vehicle.
  boost::shared_ptr<const Node> left = start->left();
  if (!left) return boost::none;

  if (!left->vehicle()) {
    // If there is no vehicle at the left node, the case is easy.
    // Just search forward from this left node to find the front vehicle.
    return frontVehicle(left);
  } else {
    // If there is a vehicle at the left node, this is the left front vehicle,
    // since the head of this vehicle must be at least the same distance with
    // the head of the query vehicle.
    const size_t left_vehicle = *(left->vehicle());
    const double distance = vehicleRearNode(left_vehicle)->distance() -
                            start->distance();
    return std::make_pair(left_vehicle, distance);
  }
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::leftBack(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::leftBack(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // rear of the vehicle.
  boost::shared_ptr<const Node> start = vehicleRearNode(vehicle);
  if (!start) {
    std::string error_msg = (boost::format(
          "TrafficLattice::leftBack(): "
          "rear of vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the left node of the start.
  // If there is no left node, there is no left back vehicle.
  boost::shared_ptr<const Node> left = start->left();
  if (!left) return boost::none;

  if (!left->vehicle()) {
    // If there is no vehicle at the left node, the case is easy.
    // Just search backward from this left node to find the back vehicle.
    return backVehicle(left);
  } else {
    // If there is a vehicle at the left node, this is the left back vehicle,
    // since the rear of this vehicle must be at least the same distance with
    // the rear of the query vehicle.
    const size_t left_vehicle = *(left->vehicle());
    const double distance = start->distance() -
                            vehicleHeadNode(left_vehicle)->distance();
    return std::make_pair(left_vehicle, distance);
  }
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::rightFront(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::rightFront(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // head of the vehicle.
  boost::shared_ptr<const Node> start = vehicleHeadNode(vehicle);
  if (!start) {
    std::string error_msg = (boost::format(
          "TrafficLattice::rightFront(): "
          "head of vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the right node of the start.
  // If there is no right node, there is no right front vehicle.
  boost::shared_ptr<const Node> right = start->right();
  if (!right) return boost::none;

  if (!right->vehicle()) {
    // If there is no vehicle at the right node, the case is easy.
    // Just search forward from this right node to find the front vehicle.
    return frontVehicle(right);
  } else {
    // If there is a vehicle at the right node, this is the right front vehicle,
    // since the head of this vehicle must be at least the same distance with
    // the head of the query vehicle.
    const size_t right_vehicle = *(right->vehicle());
    const double distance = vehicleRearNode(right_vehicle)->distance() -
                            start->distance();
    return std::make_pair(right_vehicle, distance);
  }
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::rightBack(const size_t vehicle) const {

  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::rightBack(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the node in the lattice which corresponds to the
  // rear of the vehicle.
  boost::shared_ptr<const Node> start = vehicleRearNode(vehicle);
  if (!start) {
    std::string error_msg = (boost::format(
          "TrafficLattice::rightBack(): "
          "rear of vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  // Find the right node of the start.
  // If there is no right node, there is no right back vehicle.
  boost::shared_ptr<const Node> right = start->right();
  if (!right) return boost::none;

  if (!right->vehicle()) {
    // If there is no vehicle at the right node, the case is easy.
    // Just search backward from this right node to find the back vehicle.
    return backVehicle(right);
  } else {
    // If there is a vehicle at the right node, this is the right back vehicle,
    // since the rear of this vehicle must be at least the same distance with
    // the rear of the query vehicle.
    const size_t right_vehicle = *(right->vehicle());
    const double distance = start->distance() -
                            vehicleHeadNode(right_vehicle)->distance();
    return std::make_pair(right_vehicle, distance);
  }
}

std::unordered_set<size_t> TrafficLattice::vehicles() const {
  std::unordered_set<size_t> vehicles;
  for (const auto& item : vehicle_to_nodes_table_)
    vehicles.insert(item.first);

  return vehicles;
}

int32_t TrafficLattice::isChangingLane(const size_t vehicle) const {
  if (vehicle_to_nodes_table_.count(vehicle) == 0) {
    std::string error_msg = (boost::format(
          "TrafficLattice::isChangingLane(): "
          "Input vehicle [%1%] is not on lattice.\n") % vehicle).str();
    throw std::runtime_error(error_msg);
  }

  boost::shared_ptr<const Node> rear_node = vehicleRearNode(vehicle);
  boost::shared_ptr<const Node> head_node = vehicleHeadNode(vehicle);
  const int length = vehicle_to_nodes_table_.find(vehicle)->second.size();

  // Find the \c front_node on the same lane of the \c read_node, which is
  // also at the same distance of the \c head_node.
  // FIXME: We assume there the \c front_node is always available.
  boost::shared_ptr<const Node> front_node = rear_node;
  for (int i = 0; i < length; ++i) {
    front_node = front_node->front();
    if (!front_node) {
      std::string error_msg = (boost::format(
            "TrafficLattice::isChangingLane(): "
            "Cannot find a front node %1% steps ahead of the rear node on vehicle [%2%].\n")
          % i
          % vehicle).str();
      throw std::runtime_error(
          error_msg +
          rear_node->string("rear node: ") +
          head_node->string("head node: "));
    }
  }

  if (front_node->id() == head_node->id()) return 0;
  if (front_node->left() && front_node->left()->id() == head_node->id()) return -1;
  if (front_node->right() && front_node->right()->id() == head_node->id()) return 1;

  std::string error_msg("Cannot match front node to the head node.\n");
  throw std::runtime_error(
      error_msg +
      front_node->string("front node: ") +
      head_node->string("head node: ") +
      rear_node->string("rear node: "));
}

int32_t TrafficLattice::deleteVehicle(const size_t vehicle) {
  // If the vehicle is not being tracked, there is nothing to be deleted.
  if (vehicle_to_nodes_table_.count(vehicle) == 0) return 0;

  // Otherwise, we have to first unregister the vehicle at the
  // corresponding nodes. Then remove the vehicle from the table.
  for (auto& node : vehicle_to_nodes_table_[vehicle])
    if (node.lock()) node.lock()->vehicle() = boost::none;

  vehicle_to_nodes_table_.erase(vehicle);
  return 1;
}

int32_t TrafficLattice::addVehicle(const VehicleTuple& vehicle) {
  size_t id; CarlaTransform transform; CarlaBoundingBox bounding_box;
  std::tie(id, transform, bounding_box) = vehicle;

  VehicleWaypoints waypoints;
  waypoints[0] = vehicleRearWaypoint(transform, bounding_box);
  waypoints[1] = vehicleWaypoint(transform);
  waypoints[2] = vehicleHeadWaypoint(transform, bounding_box);

  return addVehicle(vehicle, waypoints);
}

int32_t TrafficLattice::addVehicle(
    const VehicleTuple& vehicle,
    const VehicleWaypoints& waypoints) {

  // Get the ID, transform, and bounding box of the vehicle to be added.
  size_t id; CarlaTransform transform; CarlaBoundingBox bounding_box;
  std::tie(id, transform, bounding_box) = vehicle;

  // If the vehicle is already on the lattice, the vehicle won't be
  // updated with the new position. The function API is provided to
  // add a new vehicle only.
  if (vehicle_to_nodes_table_.count(id) != 0) {
    //std::printf("Already has this vehicle.\n");
    return 0;
  }

  // Find the waypoints (head and rear) of this vehicle.
  //boost::shared_ptr<const CarlaWaypoint> head_waypoint =
  //  vehicleHeadWaypoint(transform, bounding_box);
  //boost::shared_ptr<const CarlaWaypoint> rear_waypoint =
  //  vehicleRearWaypoint(transform, bounding_box);
  //boost::shared_ptr<const CarlaWaypoint> mid_waypoint =
  //  vehicleWaypoint(transform);
  boost::shared_ptr<const CarlaWaypoint> head_waypoint = waypoints[2];
  boost::shared_ptr<const CarlaWaypoint> rear_waypoint = waypoints[0];
  boost::shared_ptr<const CarlaWaypoint> mid_waypoint  = waypoints[1];

  // Find the nodes occupied by this vehicle.
  boost::shared_ptr<Node> head_node = this->closestNode(
      head_waypoint, this->longitudinal_resolution_);
  boost::shared_ptr<Node> rear_node = this->closestNode(
      rear_waypoint, this->longitudinal_resolution_);
  boost::shared_ptr<Node> mid_node = this->closestNode(
      mid_waypoint, this->longitudinal_resolution_);

  // If we can not add the whole vehicle onto the lattice, we won't add it.
  if (!head_node || !rear_node || !mid_node) {
    //if (!head_node) std::printf("Cannot find vehicle head.\n");
    //if (!rear_node) std::printf("Cannot find vehicle rear.\n");
    //if (!mid_node)  std::printf("Cannot find vehicle mid.\n");
    return 0;
  }

  // Collect the nodes that are occupied by this vehicle.
  // The following is the procedure to do this:
  //
  // 1) Move forward from the rear node, stops until the mid node or
  //    neighbors (left or right) of the mid node is met.
  // 2) Move backward from the head node, stops until the mid node or
  //    neighbors (left or right) of the mid node is met.
  // 3) Reverse the results from the second step, since we want the
  //    resulted vector to store all occupied nodes in sequence, i.e.
  //    the first node is the rear node, and the last node is the head.
  // 4) Merge the rear nodes, mid node, and head nodes into a single vector.
  //
  // The steps are certainly tedious but are necessary, especially to
  // handle vehicles that are in the process of changing lanes. In which
  // case, two portions, separated by the mid node, of the vehicles are
  // on different lanes.

  std::vector<boost::weak_ptr<Node>> rear_node_forward;
  boost::shared_ptr<Node> next_node = rear_node;
  while (true) {
    if (next_node->id() == mid_node->id()) break;
    if (mid_node->left().lock() &&
        next_node->id() == mid_node->left().lock()->id()) break;
    if (mid_node->right().lock() &&
        next_node->id() == mid_node->right().lock()->id()) break;

    rear_node_forward.emplace_back(next_node);
    if (!next_node->front().lock()) break;
    next_node = next_node->front().lock();
  }

  std::vector<boost::weak_ptr<Node>> head_node_backward;
  next_node = head_node;
  while (true) {
    if (next_node->id() == mid_node->id()) break;
    if (mid_node->left().lock() &&
        next_node->id() == mid_node->left().lock()->id()) break;
    if (mid_node->right().lock() &&
        next_node->id() == mid_node->right().lock()->id()) break;

    head_node_backward.emplace_back(next_node);
    if (!next_node->back().lock()) break;
    next_node = next_node->back().lock();
  }
  std::reverse(head_node_backward.begin(), head_node_backward.end());

  std::vector<boost::weak_ptr<Node>> nodes;
  nodes.insert(nodes.end(), rear_node_forward.begin(), rear_node_forward.end());
  nodes.emplace_back(mid_node);
  nodes.insert(nodes.end(), head_node_backward.begin(), head_node_backward.end());

  // If there is already a vehicle on any of the found nodes,
  // it indicates there is a collision.
  bool collision_flag = false;
  for (auto& node : nodes) {
    if (node.lock()->vehicle()) {
      collision_flag = true;
      break;
    }
    else node.lock()->vehicle() = id;
  }

  if (!collision_flag) {
    // If there is no collision, we can add the vehicle successfully.
    vehicle_to_nodes_table_[id] = nodes;
    return 1;
  } else {
    // If there is a collision, we should erase the vehicle on the touched nodes,
    // and leave the object in a valid state.
    for (auto& node : nodes) {
      if (!(node.lock()->vehicle())) continue;
      if (*(node.lock()->vehicle()) != id) continue;
      node.lock()->vehicle() = boost::none;
    }
    return -1;
  }
}

bool TrafficLattice::moveTrafficForward(
    const std::vector<VehicleTuple>& vehicles,
    boost::optional<std::unordered_set<size_t>&> disappear_vehicles) {

  // We require there is an update for every vehicle that is
  // currently being tracked, not more or less.
  std::unordered_set<size_t> existing_vehicles;
  for (const auto& item : vehicle_to_nodes_table_)
    existing_vehicles.insert(item.first);

  std::unordered_set<size_t> update_vehicles;
  for (const auto& item : vehicles)
    update_vehicles.insert(std::get<0>(item));

  if (existing_vehicles != update_vehicles) {
    std::string error_msg(
        "TrafficLattice::moveTrafficForward(): "
        "update vehicles does not match existing vehicles.\n");

    std::string existing_vehicles_msg("Existing vehicles: ");
    for (const auto id : existing_vehicles)
      existing_vehicles_msg += std::to_string(id) + " ";
    existing_vehicles_msg += "\n";

    std::string update_vehicles_msg("Update vehicles: ");
    for (const auto id : update_vehicles)
      update_vehicles_msg += std::to_string(id) + " ";
    update_vehicles_msg += "\n";

    throw std::runtime_error(error_msg + existing_vehicles_msg + update_vehicles_msg);
  }

  // Clear all vehicles for the moment, will add them back later.
  for (auto& item : vehicle_to_nodes_table_) {
    for (auto& node : item.second) {
      if (node.lock()) node.lock()->vehicle() = boost::none;
    }
  }
  vehicle_to_nodes_table_.clear();

  // Find waypoints for each of the input vehicle.
  std::unordered_map<size_t, VehicleWaypoints>
    vehicle_waypoints = vehicleWaypoints(vehicles);

  // Re-search for the start and range of the lattice.
  boost::shared_ptr<CarlaWaypoint> update_start = nullptr;
  double update_range = 0.0;
  latticeStartAndRange(vehicles, vehicle_waypoints, update_start, update_range);

  // Modify the lattice to agree with the new start and range.
  boost::shared_ptr<Node> update_start_node = this->closestNode(
      update_start, this->longitudinal_resolution_);

  if (!update_start_node) {
    std::string error_msg(
        "TrafficLattice::moveTrafficForward(): "
        "cannot find the new start waypoint on the existing lattice.\n");
    std::string new_start_msg = (
        boost::format(
          "new start waypoint %1%: "
          "x:%2% y:%3% z:%4% r:%5% p:%6% y:%7% road:%8% lane:%9%.\n")
        % update_start->GetId()
        % update_start->GetTransform().location.x
        % update_start->GetTransform().location.y
        % update_start->GetTransform().location.z
        % update_start->GetTransform().rotation.roll
        % update_start->GetTransform().rotation.pitch
        % update_start->GetTransform().rotation.yaw
        % update_start->GetRoadId()
        % update_start->GetLaneId()).str();
    throw std::runtime_error(error_msg + new_start_msg + this->string());
  }

  this->shorten(this->range()-update_start_node->distance());
  this->extend(update_range);

  // Register the vehicles onto the lattice.
  std::unordered_set<size_t> remove_vehicles;
  const bool valid = registerVehicles(vehicles, vehicle_waypoints, remove_vehicles);
  if (disappear_vehicles) *disappear_vehicles = remove_vehicles;

  return valid;
}

bool TrafficLattice::moveTrafficForward(
    const std::vector<boost::shared_ptr<const CarlaVehicle>>& vehicles,
    boost::optional<std::unordered_set<size_t>&> disappear_vehicles) {

  // Convert vehicle objects into tuples.
  std::vector<VehicleTuple> vehicle_tuples;
  for (const auto& vehicle : vehicles) {
    vehicle_tuples.push_back(std::make_tuple(
          vehicle->GetId(),
          vehicle->GetTransform(),
          vehicle->GetBoundingBox()));
  }

  std::unordered_set<size_t> remove_vehicles;
  const bool valid = moveTrafficForward(vehicle_tuples, remove_vehicles);
  if (disappear_vehicles) *disappear_vehicles = remove_vehicles;

  return valid;
}

void TrafficLattice::baseConstructor(
    const boost::shared_ptr<const CarlaWaypoint>& start,
    const double range,
    const double longitudinal_resolution,
    const boost::shared_ptr<router::Router>& router) {

  this->longitudinal_resolution_ = longitudinal_resolution;
  this->router_ = router;

  if (range <= this->longitudinal_resolution_) {
    std::string error_msg = (boost::format(
            "TrafficLattice::baseConstructor(): "
            "range [%1%] < longitudinal resolution [%2%].\n")
          % range
          % longitudinal_resolution).str();
    throw std::runtime_error(error_msg);
  }

  // Create the start node.
  boost::shared_ptr<Node> start_node = boost::make_shared<Node>(start);
  start_node->distance() = 0.0;
  this->lattice_exits_.push_back(start_node);

  this->augmentWaypointToNodeTable(start->GetId(), start_node);
  this->augmentRoadlaneToWaypointsTable(start);

  // Construct the lattice.
  this->extend(range);

  return;
}

void TrafficLattice::latticeStartAndRange(
    const std::vector<VehicleTuple>& vehicles,
    const std::unordered_map<size_t, VehicleWaypoints>& vehicle_waypoints,
    boost::shared_ptr<CarlaWaypoint>& start,
    double& range) const {

  // Arrange the vehicles by id.
  //std::printf("Arrange the vehicles by IDs.\n");
  std::unordered_map<size_t, CarlaTransform> vehicle_transforms;
  std::unordered_map<size_t, CarlaBoundingBox> vehicle_bounding_boxes;
  for (const auto& vehicle : vehicles) {
    size_t id; CarlaTransform transform; CarlaBoundingBox bounding_box;
    std::tie(id, transform, bounding_box) = vehicle;

    vehicle_transforms[id] = transform;
    vehicle_bounding_boxes[id] = bounding_box;

    // Check if we are missing any vehicle in \c vehicle_waypoints.
    if (vehicle_waypoints.count(id) == 0) {
      std::string error_msg(
          "TrafficLattice::latticeStartAndRange(): "
          "vehicle tuples and vehicle waypoints does not match.\n");

      std::string vehicle_tuples_msg("vehicle tuples: ");
      for (const auto& vehicle : vehicles)
        vehicle_tuples_msg += std::to_string(std::get<0>(vehicle)) + " ";
      vehicle_tuples_msg += "\n";

      std::string vehicle_waypoints_msg("vehicle waypoints: ");
      for (const auto& vehicle : vehicle_waypoints)
        vehicle_waypoints_msg += std::to_string(vehicle.first) + " ";
      vehicle_waypoints_msg += "\n";

      throw std::runtime_error(
          error_msg + vehicle_tuples_msg + vehicle_waypoints_msg);
    }
  }

  // Arrange the critial waypoint according to roads.
  // In case a waypoint is not on any of the roads on route,
  // the waypoint is ignored.
  std::unordered_map<
    size_t,
    std::vector<boost::shared_ptr<CarlaWaypoint>>> road_to_waypoints_table;

  for (const auto& vehicle : vehicle_transforms) {

    VehicleWaypoints waypoints = vehicle_waypoints.find(vehicle.first)->second;

    for (const auto& waypoint : waypoints) {
      const size_t road = waypoint->GetRoadId();
      if (!(this->router_->hasRoad(road))) continue;

      if (road_to_waypoints_table.count(road) == 0) {
        road_to_waypoints_table[road] =
          std::vector<boost::shared_ptr<CarlaWaypoint>>();
      }
      road_to_waypoints_table[road].push_back(waypoint);
    }
  }

  // Sort the waypoints on each road based on its distance.
  // Waypoints with smaller distance are at the beginning of the vector.
  for (auto& road : road_to_waypoints_table) {
    std::sort(road.second.begin(), road.second.end(),
        [this](const boost::shared_ptr<CarlaWaypoint>& w0,
               const boost::shared_ptr<CarlaWaypoint>& w1)->bool{
          const double d0 = waypointToRoadStartDistance(w0);
          const double d1 = waypointToRoadStartDistance(w1);
          return d0 < d1;
        });
  }

  // Connect the roads into a chain.
  //std::printf("Connect the roads into a chain.\n");
  std::unordered_set<size_t> roads;
  for (const auto& road : road_to_waypoints_table)
    roads.insert(road.first);

  //std::printf("roads to be sorted: ");
  //for (const size_t road : roads) std::printf("%lu ", road);
  //std::printf("\n");
  std::deque<size_t> sorted_roads;
  try {
    sorted_roads = sortRoads(roads);
  } catch(std::exception& e) {
    std::string vehicles_msg;
    for (const auto& vehicle : vehicles) {
      std::string vehicle_msg = (boost::format(
            "vehicle %1%: x:%2% y:%3% z:%4% r:%5% p:%6% y:%7%\n")
          % std::get<0>(vehicle)
          % std::get<1>(vehicle).location.x
          % std::get<1>(vehicle).location.y
          % std::get<1>(vehicle).location.z
          % std::get<1>(vehicle).rotation.roll
          % std::get<1>(vehicle).rotation.pitch
          % std::get<1>(vehicle).rotation.yaw).str();
      vehicles_msg += vehicle_msg;
    }
    throw std::runtime_error(e.what() + vehicles_msg);
  }

  // Find the first (minimum distance) and last (maximum distance)
  // waypoint of all available waypoints.
  boost::shared_ptr<CarlaWaypoint> first_waypoint =
    road_to_waypoints_table[sorted_roads.front()].front();
  boost::shared_ptr<CarlaWaypoint> last_waypoint  =
    road_to_waypoints_table[sorted_roads.back()].back();

  // Set the output start.
  start = first_waypoint;

  // Find the range of the traffic lattice
  // (the distance between the rear of the first vehicle and
  //  the front of the last vehicle).
  //
  // Some special care is required since the first and last waypoints
  // may not be on the existing roads. If not, just extend the range
  // a bit (5m in this case).
  //std::printf("Find the range of the traffic lattice.\n");
  range = 0.0;
  for (const size_t id : sorted_roads) {
    range += map_->GetMap().GetMap().GetRoad(id).GetLength();
  }

  if (first_waypoint->GetRoadId() == sorted_roads.front()) {
    range -= waypointToRoadStartDistance(first_waypoint);
  } else {
    range+= 5.0;
  }

  if (last_waypoint->GetRoadId() == sorted_roads.back()) {
    range -= map_->GetMap().GetMap().GetRoad(sorted_roads.back()).GetLength() -
             waypointToRoadStartDistance(last_waypoint);
  } else {
    range += 5.0;
  }

  return;
}

bool TrafficLattice::registerVehicles(
    const std::vector<VehicleTuple>& vehicles,
    const std::unordered_map<size_t, VehicleWaypoints>& vehicle_waypoints,
    boost::optional<std::unordered_set<size_t>&> disappear_vehicles) {

  // Clear the \c vehicle_to_node_table_.
  vehicle_to_nodes_table_.clear();

  // Add vehicles onto the lattice, keep track of the disappearred/removed vehicles as well.
  std::unordered_set<size_t> removed_vehicles;
  for (const auto& vehicle : vehicles) {
    size_t id;
    std::tie(id, std::ignore, std::ignore) = vehicle;

    if (vehicle_waypoints.count(id) == 0) {
      std::string error_msg(
          "TrafficLattice::registerVehicles(): "
          "vehicle tuples and vehicle waypoints does not match.\n");

      std::string vehicle_tuples_msg("vehicle tuples: ");
      for (const auto& vehicle : vehicles)
        vehicle_tuples_msg += std::to_string(std::get<0>(vehicle)) + " ";
      vehicle_tuples_msg += "\n";

      std::string vehicle_waypoints_msg("vehicle waypoints: ");
      for (const auto& vehicle : vehicle_waypoints)
        vehicle_waypoints_msg += std::to_string(vehicle.first) + " ";
      vehicle_waypoints_msg += "\n";

      throw std::runtime_error(
          error_msg + vehicle_tuples_msg + vehicle_waypoints_msg);
    }

    const int32_t valid = addVehicle(vehicle, vehicle_waypoints.find(id)->second);
    if (valid == 0) removed_vehicles.insert(std::get<0>(vehicle));
    else if (valid == -1) return false;
  }

  if (disappear_vehicles) *disappear_vehicles = removed_vehicles;
  return true;
}

std::deque<size_t> TrafficLattice::sortRoads(
    const std::unordered_set<size_t>& roads) const {

  // Keep track of the road IDs we have not dealt with.
  std::unordered_set<size_t> remaining_roads(roads);

  // Keep track of the sorted roads.
  std::deque<size_t> sorted_roads;

  // Start from a random road in the given set.
  sorted_roads.push_back(*(remaining_roads.begin()));
  remaining_roads.erase(remaining_roads.begin());

  // We will only expand 8 times.
  for (size_t i = 0; i < 8; ++i) {
    // Current first and last road in the chain.
    const size_t first_road = sorted_roads.front();
    const size_t last_road = sorted_roads.back();

    // New first and last road in the chain.
    boost::optional<size_t> new_first_road = this->router_->prevRoad(first_road);
    boost::optional<size_t> new_last_road = this->router_->nextRoad(last_road);

    if (new_first_road) {
      sorted_roads.push_front(*new_first_road);
      if (!remaining_roads.empty()) remaining_roads.erase(*new_first_road);
    }
    if (new_last_road) {
      sorted_roads.push_back(*new_last_road);
      if (!remaining_roads.empty()) remaining_roads.erase(*new_last_road);
    }
    if (remaining_roads.empty()) break;
  }

  // If for some weired reason, there is still some road remaining
  // which cannot be sorted, throw a runtime error.
  if (!remaining_roads.empty()) {
    std::string error_msg(
        "TrafficLattice::sortRoads(): "
        "Some of the roads cannot be sorted, "
        "which is probably because the vehicles does not construct a local traffic.\n");

    std::string input_roads_msg("roads to be sorted: ");
    for (const auto road : roads)
      input_roads_msg += std::to_string(road) + " ";
    input_roads_msg += "\n";

    std::string remaining_roads_msg("roads cannot be sorted: ");
    for (const auto road : remaining_roads)
      remaining_roads_msg += std::to_string(road) + " ";
    remaining_roads_msg += "\n";

    throw std::runtime_error(
        error_msg + input_roads_msg + remaining_roads_msg);
  }

  // Trim the sorted road vector so that both the first and last road
  // in the vector are within the given roads.
  while (roads.count(sorted_roads.front()) == 0)
    sorted_roads.pop_front();
  while (roads.count(sorted_roads.back()) == 0)
    sorted_roads.pop_back();

  return sorted_roads;
}

boost::shared_ptr<typename TrafficLattice::CarlaWaypoint>
  TrafficLattice::vehicleHeadWaypoint(
    const CarlaTransform& transform,
    const CarlaBoundingBox& bounding_box) const {

  const double sin = std::sin(transform.rotation.yaw/180.0*M_PI);
  const double cos = std::cos(transform.rotation.yaw/180.0*M_PI);

  // Be careful here! We are dealing with the left hand coordinates.
  // Do not really care about the z-axis.
  carla::geom::Location waypoint_location;
  waypoint_location.x = cos*bounding_box.extent.x + transform.location.x;
  waypoint_location.y = sin*bounding_box.extent.x + transform.location.y;
  waypoint_location.z = transform.location.z;

  //std::printf("vehicle waypoint location: x:%f y:%f z:%f\n",
  //    transform.location.x, transform.location.y, transform.location.z);
  //std::printf("head waypoint location: x:%f y:%f z:%f\n",
  //    waypoint_location.x, waypoint_location.y, waypoint_location.z);

  //return map_->GetWaypoint(waypoint_location);
  return fast_map_->waypoint(waypoint_location);
}

std::unordered_map<size_t, typename TrafficLattice::VehicleWaypoints>
  TrafficLattice::vehicleWaypoints(
    const std::vector<VehicleTuple>& vehicles) const {

  std::unordered_map<size_t, VehicleWaypoints> vehicle_waypoints;

  for (const auto& vehicle : vehicles) {
    size_t id; CarlaTransform transform; CarlaBoundingBox bounding_box;
    std::tie(id, transform, bounding_box) = vehicle;

    vehicle_waypoints[id] = VehicleWaypoints();
    vehicle_waypoints[id][0] = vehicleRearWaypoint(transform, bounding_box);
    vehicle_waypoints[id][1] = vehicleWaypoint(transform);
    vehicle_waypoints[id][2] = vehicleHeadWaypoint(transform, bounding_box);
  }

  return vehicle_waypoints;
}

boost::shared_ptr<typename TrafficLattice::CarlaWaypoint>
  TrafficLattice::vehicleRearWaypoint(
    const CarlaTransform& transform,
    const CarlaBoundingBox& bounding_box) const {

  const double sin = std::sin(transform.rotation.yaw/180.0*M_PI);
  const double cos = std::cos(transform.rotation.yaw/180.0*M_PI);

  // Be careful here! We are dealing with the left hand coordinates.
  // Do not really care about the z-axis.
  carla::geom::Location waypoint_location;
  waypoint_location.x = -cos*bounding_box.extent.x + transform.location.x;
  waypoint_location.y = -sin*bounding_box.extent.x + transform.location.y;
  waypoint_location.z = transform.location.z;

  //std::printf("vehicle waypoint location: x:%f y:%f z:%f\n",
  //    transform.location.x, transform.location.y, transform.location.z);
  //std::printf("rear waypoint location: x:%f y:%f z:%f\n",
  //    waypoint_location.x, waypoint_location.y, waypoint_location.z);
  //return map_->GetWaypoint(waypoint_location);
  return fast_map_->waypoint(waypoint_location);
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::frontVehicle(
      const boost::shared_ptr<const Node>& start) const {

  if (!start) {
    std::string error_msg(
        "TrafficLattice::frontVehicle(): "
        "the input start node does not exist on lattice.\n");
    throw std::runtime_error(error_msg);
  }

  boost::shared_ptr<const Node> front = start->front();
  while (front) {
    // If we found a vehicle at the front node, this is it.
    if (front->vehicle())
      return std::make_pair(*(front->vehicle()), front->distance()-start->distance());
    // Otherwise, keep moving forward.
    front = front->front();
  }

  // There is no front vehicle from the given node.
  return boost::none;
}

boost::optional<std::pair<size_t, double>>
  TrafficLattice::backVehicle(
      const boost::shared_ptr<const Node>& start) const {

  if (!start) {
    std::string error_msg(
        "TrafficLattice::backVehicle(): "
        "the input start node does not exist on lattice.\n");
    throw std::runtime_error(error_msg);
  }

  boost::shared_ptr<const Node> back = start->back();
  while (back) {
    // If we found a vehicle at the front node, this is it.
    if (back->vehicle())
      return std::make_pair(*(back->vehicle()), start->distance()-back->distance());
    // Otherwise, keep moving backward.
    back = back->back();
  }

  // There is no back vehicle from the given node.
  return boost::none;
}

std::string TrafficLattice::string(const std::string& prefix) const {

  std::string lattice_msg = Base::string(prefix);

  std::string vehicles_msg;
  for (const auto& vehicle : vehicle_to_nodes_table_) {
    std::string vehicle_msg = (boost::format("vehicle %1%:\n") % vehicle.first).str();
    for (const auto& node : vehicle.second)
      vehicle_msg += node.lock()->string();
    vehicles_msg += vehicle_msg;
  }

  return lattice_msg + vehicles_msg;
}

} // End namespace planner.
