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

#include <list>
#include <planner/idm_lattice_planner/idm_lattice_planner.h>

namespace planner {
namespace idm_lattice_planner {

const double IDMTrafficSimulator::egoAcceleration() const {

  // The logic for computing the acceleration for the ego vehicle is simple.
  // Regardless whether the ego vehicle is in the process of lane changing
  // or not, we will only consider the front vehicle (if any) on the same
  // lane with the head of the ego vehicle.
  //
  // TODO: Should we consider the front vehicle on two lanes, both the
  //       old lane and the target lane?

  double accel = 0.0;
  boost::optional<std::pair<size_t, double>> lead =
    snapshot_.trafficLattice()->front(snapshot_.ego().id());

  if (lead) {
    const double lead_speed = snapshot_.vehicle(lead->first).speed();
    const double following_distance = lead->second;
    accel = idm_->idm(snapshot_.ego().speed(),
                      snapshot_.ego().policySpeed(),
                      lead_speed,
                      following_distance);
  } else {
    accel = idm_->idm(snapshot_.ego().speed(),
                      snapshot_.ego().policySpeed());
  }

  return accel;
}

const double IDMTrafficSimulator::agentAcceleration(const size_t agent) const {

  // We assume all agent vehicles are lane followers for now.
  double accel = 0.0;
  boost::optional<std::pair<size_t, double>> lead =
    snapshot_.trafficLattice()->front(agent);

  if (lead) {
    const double lead_speed = snapshot_.vehicle(lead->first).speed();
    const double following_distance = lead->second;
    accel = idm_->idm(snapshot_.vehicle(agent).speed(),
                      snapshot_.vehicle(agent).policySpeed(),
                      lead_speed,
                      following_distance);
  } else {
    accel = idm_->idm(snapshot_.vehicle(agent).speed(),
                      snapshot_.vehicle(agent).policySpeed());
  }

  return accel;
}

void Station::updateOptimalParent() {

  // Set the \c optimal_parent_ to an existing parent. It does not
  // matter which parent is used for now.
  if (!optimal_parent_) {
    if (left_parent_) optimal_parent_ = left_parent_;
    else if (back_parent_) optimal_parent_ = back_parent_;
    else if (right_parent_) optimal_parent_ = right_parent_;
    else {
      throw std::runtime_error(
          "Station::updateOptimalParent(): "
          "cannot update optimal parent since there is no parent available.\n");
    }
  }

  // Set the \c optimal_parent_ to the existing parent with the minimum cost-to-come.
  // With the same cost-to-come, the back parent is preferred.
  if (left_parent_ && std::get<1>(*left_parent_) <= std::get<1>(*optimal_parent_))
    optimal_parent_ = left_parent_;
  if (right_parent_ && std::get<1>(*right_parent_) <= std::get<1>(*optimal_parent_))
    optimal_parent_ = right_parent_;
  if (back_parent_ && std::get<1>(*back_parent_) <= std::get<1>(*optimal_parent_))
    optimal_parent_ = back_parent_;

  // Update the snapshot at this station.
  snapshot_ = std::get<0>(*optimal_parent_);

  return;
}

void Station::updateLeftParent(
    const Snapshot& snapshot,
    const double cost_to_come,
    const boost::shared_ptr<Station>& parent_station) {
  left_parent_ = std::make_tuple(snapshot, cost_to_come, parent_station);
  updateOptimalParent();
  return;
}

void Station::updateBackParent(
    const Snapshot& snapshot,
    const double cost_to_come,
    const boost::shared_ptr<Station>& parent_station) {
  back_parent_ = std::make_tuple(snapshot, cost_to_come, parent_station);
  updateOptimalParent();
  return;
}

void Station::updateRightParent(
    const Snapshot& snapshot,
    const double cost_to_come,
    const boost::shared_ptr<Station>& parent_station) {
  right_parent_ = std::make_tuple(snapshot, cost_to_come, parent_station);
  updateOptimalParent();
  return;
}

void Station::updateLeftChild(
    const ContinuousPath& path,
    const double stage_cost,
    const boost::shared_ptr<Station>& child_station) {
  left_child_ = std::make_tuple(path, stage_cost, child_station);
  return;
}

void Station::updateFrontChild(
    const ContinuousPath& path,
    const double stage_cost,
    const boost::shared_ptr<Station>& child_station) {
  front_child_ = std::make_tuple(path, stage_cost, child_station);
  return;
}

void Station::updateRightChild(
    const ContinuousPath& path,
    const double stage_cost,
    const boost::shared_ptr<Station>& child_station) {
  right_child_ = std::make_tuple(path, stage_cost, child_station);
  return;
}

std::string Station::string(const std::string& prefix) const {
  std::string output = prefix;
  output += "id: " + std::to_string(id()) + "\n";
  output += "snapshot: \n" + snapshot_.string();

  boost::format parent_format("id:%1% cost to come:%2%\n");

  output += "back parent: ";
  if (back_parent_)
    output += ((parent_format) % std::get<2>(*back_parent_).lock()->id()
                               % std::get<1>(*back_parent_)).str();
  else output += "\n";

  output += "left parent: ";
  if (left_parent_)
    output += ((parent_format) % std::get<2>(*left_parent_).lock()->id()
                               % std::get<1>(*left_parent_)).str();
  else output += "\n";

  output += "right parent: ";
  if (right_parent_)
    output += ((parent_format) % std::get<2>(*right_parent_).lock()->id()
                               % std::get<1>(*right_parent_)).str();
  else output += "\n";

  output += "optimal parent: ";
  if (optimal_parent_)
    output += ((parent_format) % std::get<2>(*optimal_parent_).lock()->id()
                               % std::get<1>(*optimal_parent_)).str();
  else output += "\n";

  boost::format child_format("id:%1% path length:%2% stage cost:%3%\n");

  output += "front child: ";
  if (front_child_)
    output += ((child_format) % std::get<2>(*front_child_).lock()->id()
                              % std::get<0>(*front_child_).range()
                              % std::get<1>(*front_child_)).str();
  else output += "\n";

  output += "left child: ";
  if (left_child_)
    output += ((child_format) % std::get<2>(*left_child_).lock()->id()
                              % std::get<0>(*left_child_).range()
                              % std::get<1>(*left_child_)).str();
  else output += "\n";

  output += "right child: ";
  if (right_child_)
    output += ((child_format) % std::get<2>(*right_child_).lock()->id()
                              % std::get<0>(*right_child_).range()
                              % std::get<1>(*right_child_)).str();
  else output += "\n";

  return output;
}

std::vector<boost::shared_ptr<const WaypointNode>>
  IDMLatticePlanner::nodes() const {

  std::vector<boost::shared_ptr<const WaypointNode>> nodes;
  for (const auto& item : node_to_station_table_)
    nodes.push_back(item.second->node().lock());

  return nodes;
}

std::vector<ContinuousPath> IDMLatticePlanner::edges() const {

  std::vector<ContinuousPath> paths;
  for (const auto& item : node_to_station_table_) {
    const boost::shared_ptr<const Station> station = item.second;

    if (station->hasFrontChild())
      paths.push_back(std::get<0>(*(station->frontChild())));

    if (station->hasLeftChild())
      paths.push_back(std::get<0>(*(station->leftChild())));

    if (station->hasRightChild())
      paths.push_back(std::get<0>(*(station->rightChild())));
  }

  return paths;
}

DiscretePath IDMLatticePlanner::planPath(
    const size_t ego, const Snapshot& snapshot) {

  if (ego != snapshot.ego().id()) {
    std::string error_msg(
        "IDMLatticePlanner::planPath(): "
        "The IDM lattice planner can only plan for the ego.\n");
    std::string id_msg = (boost::format(
          "Target vehicle ID:%1% Ego vehicle ID:%2%\n")
        % ego % snapshot.ego().id()).str();

    throw std::runtime_error(error_msg + id_msg);
  }

  // Update the waypoint lattice.
  updateWaypointLattice(snapshot);

  // Prune the station graph.
  std::deque<boost::shared_ptr<Station>> station_queue = pruneStationGraph(snapshot);

  // No immedinate front nodes can be connected.
  if (station_queue.size() == 0) {
    std::string error_msg(
        "IDMLatticePlanner::planPath(): "
        "The ego cannot reach any immediate next nodes.\n");
    throw std::runtime_error(
        error_msg +
        snapshot.string("Input snapshot:\n") +
        waypoint_lattice_->string("waypoint lattice:\n"));

    //waypoint_lattice_ = nullptr;
    //node_to_station_table_.clear();

    //updateWaypointLattice(snapshot);
    //station_queue = pruneStationGraph(snapshot);
  }

  // Construct the station graph.
  constructStationGraph(station_queue);

  // Select the optimal path sequence from the station graph.
  std::list<ContinuousPath> optimal_path_seq;
  std::list<boost::weak_ptr<Station>> optimal_station_seq;
  selectOptimalPath(optimal_path_seq, optimal_station_seq);

  // Merge the path sequence into one discrete path.
  DiscretePath optimal_path = mergePaths(optimal_path_seq);

  // Update the cached next station.
  cached_next_station_ = *(++optimal_station_seq.begin());

  return optimal_path;
}

bool IDMLatticePlanner::immediateNextStationReached(
    const Snapshot& snapshot) const {

  //std::printf("immediateNextStationReached(): \n");

  boost::shared_ptr<const WaypointLattice> waypoint_lattice =
    boost::const_pointer_cast<const WaypointLattice>(waypoint_lattice_);

  // Find out the current distance of the ego on the lattice.
  boost::shared_ptr<const WaypointNode> ego_node = waypoint_lattice->closestNode(
      fast_map_->waypoint(snapshot.ego().transform().location),
      waypoint_lattice->longitudinalResolution());
  const double ego_distance = ego_node->distance();

  // Find out the distance the ego need to achieve.
  const double target_distance = cached_next_station_.lock()->node().lock()->distance();

  // If the difference is less than 0.5, or the ego has travelled beyond the
  // the target distance, the immediate station is considered to be reached.
  if (target_distance-ego_distance < 0.5) return true;
  else return false;
}

void IDMLatticePlanner::updateWaypointLattice(const Snapshot& snapshot) {

  //std::printf("updateWaypointLattice(): \n");

  // If the waypoint lattice has not been initialized, a new one is created with
  // the start waypoint as where the ego currently is. Meanwhile, the range of
  // the lattice is set to the spatial horizon. The resolution is hardcoded as 1.0m.
  if (!waypoint_lattice_) {
    //std::printf("Create new waypoint lattice.\n");
    boost::shared_ptr<CarlaWaypoint> ego_waypoint =
      fast_map_->waypoint(snapshot.ego().transform().location);
    waypoint_lattice_ = boost::make_shared<WaypointLattice>(
        ego_waypoint, spatial_horizon_+30.0, 1.0, router_);
    return;
  }

  // If the waypoint lattice has been created before, we will choose to either
  // update the lattice or leave it as it currently is based on whether the ego
  // has reached one of the child stations of the root.
  if (immediateNextStationReached(snapshot)) {
    boost::shared_ptr<const WaypointLattice> waypoint_lattice =
      boost::const_pointer_cast<const WaypointLattice>(waypoint_lattice_);

    boost::shared_ptr<const WaypointNode> ego_node = waypoint_lattice->closestNode(
        fast_map_->waypoint(snapshot.ego().transform().location),
        waypoint_lattice->longitudinalResolution());
    //std::printf("Shift the lattice by %f\n", ego_node->distance()-5.0);
    const double shift_distance = ego_node->distance() - 5.0;
    waypoint_lattice_->shift(shift_distance);
  }

  return;
}

std::deque<boost::shared_ptr<Station>>
  IDMLatticePlanner::pruneStationGraph(const Snapshot& snapshot) {

  //std::printf("pruneStationGraph(): \n");

  // Stores the stations to be explored.
  std::deque<boost::shared_ptr<Station>> station_queue;

  // There are two cases we can basically start fresh in constructing the station graph:
  // 1) This is the first time the \c plan() interface is called.
  // 2) The ego reached one of the immediate child of the root station.
  if ((!root_.lock()) || immediateNextStationReached(snapshot)) {
    node_to_station_table_.clear();

    // Initialize the new root station.
    boost::shared_ptr<Station> root =
      boost::make_shared<Station>(snapshot, waypoint_lattice_, fast_map_);
    node_to_station_table_[root->id()] = root;
    root_ = root;

    station_queue.push_back(root);
    return station_queue;
  }

  // If the ego is in the progress of approaching one of the immedidate
  // child of the root node, we have to keep these immediate child nodes
  // where they are.

  // Create the new root station.
  boost::shared_ptr<Station> new_root =
    boost::make_shared<Station>(snapshot, waypoint_lattice_, fast_map_);

  // Read the immedinate next waypoint node to be reached.
  boost::shared_ptr<const WaypointNode> next_node =
    cached_next_station_.lock()->node().lock();
  const double distance_to_next_node =
    next_node->distance() - new_root->node().lock()->distance();

  // Find the next nodes to be reached.
  boost::shared_ptr<const WaypointNode> front_node =
    waypoint_lattice_->front(new_root->node().lock()->waypoint(), distance_to_next_node);
  boost::shared_ptr<const WaypointNode> left_front_node =
    waypoint_lattice_->frontLeft(new_root->node().lock()->waypoint(), distance_to_next_node);
  boost::shared_ptr<const WaypointNode> right_front_node =
    waypoint_lattice_->frontRight(new_root->node().lock()->waypoint(), distance_to_next_node);

  // Clear all old stations.
  // We are good with the previously created nodes. All stations will be newly created.
  node_to_station_table_.clear();

  // Try to connect the new root with above nodes.
  boost::shared_ptr<Station> front_station =
    connectStationToFrontNode(new_root, front_node);
  boost::shared_ptr<Station> left_front_station =
    connectStationToLeftFrontNode(new_root, left_front_node);
  boost::shared_ptr<Station> right_front_station =
    connectStationToRightFrontNode(new_root, right_front_node);

  // Save the new root to the table.
  root_ = new_root;
  node_to_station_table_[new_root->id()] = new_root;

  // Save the newly created stations to the table and queue
  // if they are successfully created.
  if (front_station) {
    node_to_station_table_[front_station->id()] = front_station;
    if (front_station->id() == front_node->id())
      station_queue.push_back(front_station);
  }

  if (left_front_station) {
    node_to_station_table_[left_front_station->id()] = left_front_station;
    if (left_front_station->id() == left_front_node->id())
      station_queue.push_back(left_front_station);
  }

  if (right_front_station) {
    node_to_station_table_[right_front_station->id()] = right_front_station;
    if (right_front_station->id() == right_front_node->id())
      station_queue.push_back(right_front_station);
  }

  return station_queue;
}

void IDMLatticePlanner::constructStationGraph(
    std::deque<boost::shared_ptr<Station>>& station_queue) {

  //std::printf("constructStationGraph(): \n");

  auto addStationToTableAndQueue = [this, &station_queue](
      const boost::shared_ptr<Station>& station,
      const boost::shared_ptr<const WaypointNode>& node)->void{
    if ((!station) || (!node)) return;

    if (node_to_station_table_.count(station->id()) == 0) {
      node_to_station_table_[station->id()] = station;
      if (station->id() == node->id()) station_queue.push_back(station);
    }
  };

  while (!station_queue.empty()) {
    boost::shared_ptr<Station> station = station_queue.front();
    station_queue.pop_front();

    // Try to connect to the front node.
    boost::shared_ptr<const WaypointNode> front_node =
      waypoint_lattice_->front(station->node().lock()->waypoint(), 50.0);
    boost::shared_ptr<Station> front_station =
      connectStationToFrontNode(station, front_node);

    addStationToTableAndQueue(front_station, front_node);

    // Try to connect to the left front node.
    boost::shared_ptr<const WaypointNode> left_front_node =
      waypoint_lattice_->frontLeft(station->node().lock()->waypoint(), 50.0);
    boost::shared_ptr<Station> left_front_station =
      connectStationToLeftFrontNode(station, left_front_node);

    addStationToTableAndQueue(left_front_station, left_front_node);

    // Try to connect to the right front node.
    boost::shared_ptr<const WaypointNode> right_front_node =
      waypoint_lattice_->frontRight(station->node().lock()->waypoint(), 50.0);
    boost::shared_ptr<Station> right_front_station =
      connectStationToRightFrontNode(station, right_front_node);

    addStationToTableAndQueue(right_front_station, right_front_node);
  }

  //std::printf("station #: %lu\n", node_to_station_table_.size());

  //for (const auto& item : node_to_station_table_) {
  //  if (!item.second) throw std::runtime_error("node is not available.");
  //  std::cout << item.second->string() << std::endl;;
  //}

  return;
}

boost::shared_ptr<Station> IDMLatticePlanner::connectStationToFrontNode(
    const boost::shared_ptr<Station>& station,
    const boost::shared_ptr<const WaypointNode>& target_node) {

  //std::printf("connectStationToFrontNode(): \n");

  // Return directly if the target node does not exist.
  if (!target_node) return nullptr;

  // Plan a path between the node at the current station to the target node.
  //std::printf("Compute Kelly-Nagy path.\n");
  boost::shared_ptr<ContinuousPath> path = nullptr;
  try {
    path = boost::make_shared<ContinuousPath>(
        std::make_pair(station->snapshot().ego().transform(),
                       station->snapshot().ego().curvature()),
        std::make_pair(target_node->waypoint()->GetTransform(),
                       target_node->curvature(map_)),
        ContinuousPath::LaneChangeType::KeepLane);
  } catch (std::exception& e) {
    // If for whatever reason, the path cannot be created, the station
    // cannot be created either.
    std::printf("%s", e.what());
    return nullptr;
  }

  // Now, simulate the traffic forward with ego following the created path.
  //std::printf("Simulate the traffic.\n");
  IDMTrafficSimulator simulator(station->snapshot(), map_, fast_map_);
  double simulation_time = 0.0; double stage_cost = 0.0;
  try {
    const bool no_collision = simulator.simulate(
        *path, sim_time_step_, 5.0, simulation_time, stage_cost);
    // There a collision is detected in the simulation, this option is ignored.
    if (!no_collision) return nullptr;
  } catch(std::exception& e) {
    std::printf("IDMLatticePlanner::connectStationToFrontNode(): WARNING\n"
                "%s", e.what());
    return nullptr;
  }

  // Either create a new station or used the one has been already created.
  //std::printf("Create child station.\n");
  boost::shared_ptr<Station> next_station = boost::make_shared<Station>(
      simulator.snapshot(), waypoint_lattice_, fast_map_);
  if (node_to_station_table_.count(next_station->id()) != 0)
    next_station = node_to_station_table_[next_station->id()];

  // Set the child station of the parent station.
  //std::printf("Update the child station of the input station.\n");
  station->updateFrontChild(*path, stage_cost, next_station);

  // Set the parent station of the child station.
  //std::printf("Update the parent station of the new station.\n");
  if (station->hasParent()) {
    next_station->updateBackParent(
        simulator.snapshot(), station->costToCome()+stage_cost, station);
  } else {
    next_station->updateBackParent(
        simulator.snapshot(), stage_cost, station);
  }

  return next_station;
}

boost::shared_ptr<Station> IDMLatticePlanner::connectStationToLeftFrontNode(
    const boost::shared_ptr<Station>& station,
    const boost::shared_ptr<const WaypointNode>& target_node) {

  //std::printf("connectStationToLeftFrontNode(): \n");

  // Return directly if the target node does not exisit.
  if (!target_node) return nullptr;

  // Return directly if the target node is already very close to the station.
  // It is not reasonable to change lane with this short distance.
  if (target_node->distance()-station->node().lock()->distance() < 20.0)
    return nullptr;

  // If the ego is on the right of the lane center, connecting to the
  // left lane is forbidden.
  if (utils::distanceToLaneCenter(
        station->snapshot().ego().transform().location,
        station->node().lock()->waypoint()) > 0.5)
    return nullptr;

  // Check the left front and left back vehicles.
  //
  // If there are vehicles at the left front or left back of the ego,
  // meanwhile those vehicles has non-positive distance to the ego, we will
  // ignore this path option.
  //
  // TODO: Should we increase the margin of the check?
  boost::optional<std::pair<size_t, double>> left_front =
    station->snapshot().trafficLattice()->leftFront(station->snapshot().ego().id());
  boost::optional<std::pair<size_t, double>> left_back =
    station->snapshot().trafficLattice()->leftBack(station->snapshot().ego().id());

  if (left_front && left_front->second <= 0.0) return nullptr;
  if (left_back  && left_back->second  <= 0.0) return nullptr;

  // Plan a path between the node at the current station to the target node.
  //std::printf("Compute Kelly-Nagy path.\n");
  boost::shared_ptr<ContinuousPath> path = nullptr;
  try {
    path = boost::make_shared<ContinuousPath>(
        std::make_pair(station->snapshot().ego().transform(),
                       station->snapshot().ego().curvature()),
        std::make_pair(target_node->waypoint()->GetTransform(),
                       target_node->curvature(map_)),
        ContinuousPath::LaneChangeType::LeftLaneChange);
  } catch (const std::exception& e) {
    // If for whatever reason, the path cannot be created,
    // just ignore this option.
    std::printf("%s", e.what());
    return nullptr;
  }

  // Now, simulate the traffic forward with ego following the created path.
  //std::printf("Simulate the traffic.\n");
  IDMTrafficSimulator simulator(station->snapshot(), map_, fast_map_);
  double simulation_time = 0.0; double stage_cost = 0.0;
  try {
    const bool no_collision = simulator.simulate(
        *path, sim_time_step_, 5.0, simulation_time, stage_cost);
    // There a collision is detected in the simulation, this option is ignored.
    if (!no_collision) return nullptr;
  } catch (std::exception& e) {
    std::printf("IDMLatticePlanner::connectStationToLeftFrontNode(): WARNING\n"
                "%s", e.what());
    return nullptr;
  }

  // Either create a new station or used the one has been already created.
  //std::printf("Create child station.\n");
  boost::shared_ptr<Station> next_station = boost::make_shared<Station>(
      simulator.snapshot(), waypoint_lattice_, fast_map_);
  if (node_to_station_table_.count(next_station->id()) != 0)
    next_station = node_to_station_table_[next_station->id()];

  // Set the child station of the parent station.
  //std::printf("Update the child station of the input station.\n");
  station->updateLeftChild(*path, stage_cost, next_station);

  // Set the parent station of the child station.
  //std::printf("Update the parent station of the new station.\n");
  if (station->hasParent()) {
    next_station->updateRightParent(
        simulator.snapshot(), station->costToCome()+stage_cost, station);
  } else {
    next_station->updateRightParent(
        simulator.snapshot(), stage_cost, station);
  }

  return next_station;
}

boost::shared_ptr<Station> IDMLatticePlanner::connectStationToRightFrontNode(
    const boost::shared_ptr<Station>& station,
    const boost::shared_ptr<const WaypointNode>& target_node) {

  //std::printf("connectStationToRightFrontNode(): \n");

  // Return directly if the target node does not exisit.
  if (!target_node) return nullptr;

  // Return directly if the target node is already very close to the station.
  // It is not reasonable to change lane with this short distance.
  if (target_node->distance()-station->node().lock()->distance() < 20.0)
    return nullptr;

  // If the ego is on the left of the lane center, connecting to the
  // right lane is forbidden.
  if (utils::distanceToLaneCenter(
        station->snapshot().ego().transform().location,
        station->node().lock()->waypoint()) < -0.5)
    return nullptr;

  // Check the right front and right back vehicles.
  //
  // If there are vehicles at the right front or right back of the ego,
  // meanwhile those vehicles has non-positive distance to the ego, we will
  // ignore this path option.
  //
  // TODO: Should we increase the margin of the check?
  boost::optional<std::pair<size_t, double>> right_front =
    station->snapshot().trafficLattice()->rightFront(station->snapshot().ego().id());
  boost::optional<std::pair<size_t, double>> right_back =
    station->snapshot().trafficLattice()->rightBack(station->snapshot().ego().id());

  if (right_front && right_front->second <= 0.0) return nullptr;
  if (right_back  && right_back->second  <= 0.0) return nullptr;

  // Plan a path between the node at the current station to the target node.
  //std::printf("Compute Kelly-Nagy path.\n");
  boost::shared_ptr<ContinuousPath> path = nullptr;
  try {
    path = boost::make_shared<ContinuousPath>(
        std::make_pair(station->snapshot().ego().transform(),
                       station->snapshot().ego().curvature()),
        std::make_pair(target_node->waypoint()->GetTransform(),
                       target_node->curvature(map_)),
        ContinuousPath::LaneChangeType::RightLaneChange);
  } catch (std::exception& e) {
    // If for whatever reason, the path cannot be created,
    // just ignore this option.
    std::printf("%s", e.what());
    return nullptr;
  }

  // Now, simulate the traffic forward with the ego following the created path.
  //std::printf("Simulate the traffic.\n");
  IDMTrafficSimulator simulator(station->snapshot(), map_, fast_map_);
  double simulation_time = 0.0; double stage_cost = 0.0;
  try {
    const bool no_collision = simulator.simulate(
        *path, sim_time_step_, 5.0, simulation_time, stage_cost);
    // There a collision is detected in the simulation, this option is ignored.
    if (!no_collision) return nullptr;
  } catch (std::exception& e) {
    std::printf("IDMLatticePlanner::connectStationToRightFrontNode(): WARNING\n"
                "%s", e.what());
    return nullptr;
  }

  // Either create a new station or used the one has been already created.
  //std::printf("Create child station.\n");
  boost::shared_ptr<Station> next_station = boost::make_shared<Station>(
      simulator.snapshot(), waypoint_lattice_, fast_map_);
  if (node_to_station_table_.count(next_station->id()) != 0)
    next_station = node_to_station_table_[next_station->id()];

  // Set the child station of the parent station.
  //std::printf("Update the child station of the input station.\n");
  station->updateRightChild(*path, stage_cost, next_station);

  // Set the parent station of the child station.
  //std::printf("Update the parent station of the new station.\n");
  if (station->hasParent()) {
    next_station->updateLeftParent(
        simulator.snapshot(), station->costToCome()+stage_cost, station);
  } else {
    next_station->updateLeftParent(
        simulator.snapshot(), stage_cost, station);
  }

  return next_station;
}

const double IDMLatticePlanner::terminalSpeedCost(
    const boost::shared_ptr<Station>& station) const {

  if (station->hasChild()) {
    std::string error_msg(
        "IDMLatticePlanner::terminalSpeedCost(): "
        "The input station is not a terminal.\n");
    throw std::runtime_error(error_msg + station->string());
  }

  static std::unordered_map<int, double> cost_map {
    {0, 4.0}, {1, 4.0}, {2, 4.0}, {3, 3.0}, {4, 3.0},
    {5, 2.0}, {6, 2.0}, {7, 1.0}, {8, 1.0}, {9, 0.0},
  };

  const double ego_speed = station->snapshot().ego().speed();
  const double ego_policy_speed = station->snapshot().ego().policySpeed();
  if (ego_speed < 0.0 || ego_policy_speed < 0.0) {
    std::string error_msg(
        "IDMLatticePlanner::terminalSpeedCost(): "
        "ego speed<0.0 or ego policy speed<0.0.\n");
    std::string speed_msg = (boost::format(
          "ego speed:%1% ego policy speed:%2%\n")
          % ego_speed
          % ego_policy_speed).str();
    throw std::runtime_error(error_msg + speed_msg);
  }

  // FIXME: What if the policy speed is 0.
  //        Should not worry about this too much here since,
  //        1) IDM does not support 0 policy speed.
  //        2) 0 policy speed may cause some invalidity in carla, such as
  //           looking for a waypoint 0m ahead.
  const double speed_ratio = ego_speed / ego_policy_speed;

  // There is no cost if the speed of the ego matches or exceeds the policy speed.
  if (speed_ratio >= 1.0) return 0.0;
  else return cost_map[static_cast<int>(speed_ratio*10.0)];
}

const double IDMLatticePlanner::terminalDistanceCost(
    const boost::shared_ptr<Station>& station) const {

  if (station->hasChild()) {
    std::string error_msg(
        "IDMLatticePlanner::terminalDistanceCost(): "
        "The input station is not a terminal.\n");
    throw std::runtime_error(error_msg + station->string());
  }

  static std::unordered_map<int, double> cost_map {
    {0, 20.0}, {1, 20.0}, {2, 20.0}, {3, 20.0}, {4, 20.0},
    {5, 20.0}, {6, 20.0}, {7, 20.0}, {8, 10.0},  {9, 5.0},
  };
  //static std::unordered_map<int, double> cost_map {
  //  {0, 8.0}, {1, 7.0}, {2, 6.0}, {3, 5.0}, {4, 5.0},
  //  {5, 3.0}, {6, 2.0}, {7, 2.0}, {8, 1.0}, {9, 1.0},
  //};

  // Find the current spatial planning horizon.
  boost::shared_ptr<const Station> root_child;
  if (root_.lock()->hasFrontChild())
    root_child = std::get<2>(*(root_.lock()->frontChild())).lock();
  else if (root_.lock()->hasLeftChild())
    root_child = std::get<2>(*(root_.lock()->leftChild())).lock();
  else if (root_.lock()->hasRightChild())
    root_child = std::get<2>(*(root_.lock()->rightChild())).lock();

  const double spatial_horizon =
    spatial_horizon_ - 50.0 +
    root_child->node()->distance() -
    root_.lock()->node().lock()->distance();

  const double distance = station->node().lock()->distance() -
                          root_.lock()->node().lock()->distance();

  const double distance_ratio = distance / spatial_horizon;
  //std::printf("station distance:%f spatial horizon:%f distance ratio: %f\n",
  //    distance, spatial_horizon, distance_ratio);

  if (distance_ratio >= 1.0) return 0.0;
  else return cost_map[static_cast<int>(distance_ratio*10.0)];
}

const double IDMLatticePlanner::costFromRootToTerminal(
    const boost::shared_ptr<Station>& terminal) const {

  if (terminal->hasChild()) {
    std::string error_msg(
        "IDMLatticePlanner::costFromRootToTerminal(): "
        "The input station is not a terminal.\n");
    throw std::runtime_error(error_msg + terminal->string());
  }

  const double path_cost = terminal->costToCome();
  const double terminal_speed_cost = terminalSpeedCost(terminal);
  const double terminal_distance_cost = terminalDistanceCost(terminal);
  //std::printf("path cost: %f speed cost: %f distance cost:%f\n",
  //    path_cost, terminal_speed_cost, terminal_distance_cost);

  // TODO: Weight the cost properly.
  return path_cost + terminal_speed_cost + terminal_distance_cost;
}

void IDMLatticePlanner::selectOptimalPath(
    std::list<ContinuousPath>& path_sequence,
    std::list<boost::weak_ptr<Station>>& station_sequence) const {

  //std::printf("selectOptimalPath():\n");

  //std::printf("Find optimal terminal station.\n");
  boost::shared_ptr<Station> optimal_station = nullptr;
  // Set the initial cost to a large enough number.
  double optimal_cost = 1.0e10;

  for (const auto& item : node_to_station_table_) {

    boost::shared_ptr<Station> station = item.second;
    //std::printf("=============================================\n");
    //std::cout << station->string();
    // Only terminal stations are considered, i.e. stations without children.
    if (station->hasChild()) continue;
    const double station_cost = costFromRootToTerminal(station);
    //std::printf("cost:%f\n", station_cost);
    //std::printf("=============================================\n");

    // Update the optimal station if the candidate has small cost.
    if (station_cost < optimal_cost) {
      optimal_station = station;
      optimal_cost = station_cost;
    }
  }

  // The optimal station should be set no matter what.
  if (!optimal_station) {
    throw std::runtime_error(
        "IDMLatticePlanner::selectOptimalPath(): "
        "no terminal station in the graph.\n");
  }

  // There should always be parent stations for a terminal station.
  if (!optimal_station->hasParent()) {
    throw std::runtime_error(
        "IDMLatticePlanner::selectOptimalPath(): "
        "the graph only has root station.\n");
  }

  // Lambda functions to get the child station IDs given a parent station.
  auto frontChildId = [this](const boost::shared_ptr<Station>& station)->boost::optional<size_t>{
    if (!(station->frontChild())) return boost::none;
    else return std::get<2>(*(station->frontChild())).lock()->id();
  };
  auto leftChildId = [this](const boost::shared_ptr<Station>& station)->boost::optional<size_t>{
    if (!(station->leftChild())) return boost::none;
    else return std::get<2>(*(station->leftChild())).lock()->id();
  };
  auto rightChildId = [this](const boost::shared_ptr<Station>& station)->boost::optional<size_t>{
    if (!(station->rightChild())) return boost::none;
    else return std::get<2>(*(station->rightChild())).lock()->id();
  };

  // Trace back from the terminal station to find all the paths.
  path_sequence.clear();
  station_sequence.clear();

  boost::shared_ptr<Station> station = optimal_station;
  station_sequence.push_front(boost::weak_ptr<Station>(station));

  //std::printf("Trace back parent stations.\n");
  while (station->hasParent()) {

    //std::cout << station->string() << std::endl;

    boost::shared_ptr<Station> parent_station =
      std::get<2>((*(station->optimalParent()))).lock();
    if (!parent_station) {
      std::string error_msg(
          "IDMLatticePlanner::selectOptimalPath(): "
          "cannot find parent when tracing back optimal path from the station.\n");
      throw std::runtime_error(error_msg + station->string());
    }

    station_sequence.push_front(boost::weak_ptr<Station>(parent_station));

    // The station is the front child station of the parent.
    if (frontChildId(parent_station) &&
        frontChildId(parent_station) == station->id()) {
      path_sequence.push_front(std::get<0>(*(parent_station->frontChild())));
      station = parent_station;
      continue;
    }

    // The station is the left child station of the parent.
    if (leftChildId(parent_station) &&
        leftChildId(parent_station) == station->id()) {
      path_sequence.push_front(std::get<0>(*(parent_station->leftChild())));
      station = parent_station;
      continue;
    }

    // The station is the right child station of the parent.
    if (rightChildId(parent_station) &&
        rightChildId(parent_station) == station->id()) {
      path_sequence.push_front(std::get<0>(*(parent_station->rightChild())));
      station = parent_station;
      continue;
    }
  }

  return;
}

DiscretePath IDMLatticePlanner::mergePaths(
    const std::list<ContinuousPath>& paths) const {

  //std::printf("mergePaths(): \n");
  //std::printf("path #: %lu\n", paths.size());

  DiscretePath path(paths.front());
  for (std::list<ContinuousPath>::const_iterator iter = ++(paths.begin());
       iter != paths.end(); ++iter) path.append(*iter);
  return path;
}

} // End namespace idm_lattice_planner
} // End namespace planner.
