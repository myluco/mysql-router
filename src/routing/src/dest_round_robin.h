/*
  Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ROUTING_DEST_ROUND_ROBIN_INCLUDED
#define ROUTING_DEST_ROUND_ROBIN_INCLUDED

#include "destination.h"
#include "mysqlrouter/routing.h"

#include "mysql/harness/logging/logging.h"

class DestRoundRobin : public RouteDestination {
 public:
  using RouteDestination::RouteDestination;

  /** @brief Destructor */
  virtual ~DestRoundRobin();

  virtual void start() override {
    if (!quarantine_thread_.joinable()) {
      quarantine_thread_ = std::thread(&DestRoundRobin::quarantine_manager_thread, this);
    } else {
      log_debug("Tried to restart quarantine thread");
    }
  }

  int get_server_socket(std::chrono::milliseconds connect_timeout, int *error) noexcept override;

  /** @brief Returns number of quarantined servers
   *
   * @return size_t
   */
  size_t size_quarantine();

 protected:
  /** @brief Returns whether destination is quarantined
   *
   * Uses the given index to check whether the destination is
   * quarantined.
   *
   * @param index index of the destination to check
   * @return True if destination is quarantined
   */
  virtual bool is_quarantined(const size_t index) {
    return std::find(quarantined_.begin(), quarantined_.end(), index) != quarantined_.end();
  }

  /** @brief Adds server to quarantine
   *
   * Adds the given server address to the quarantine list. The index argument
   * is the index of the server in the destination list.
   *
   * @param index Index of the destination
   */
  virtual void add_to_quarantine(size_t index) noexcept;

  /** @brief Worker checking and removing servers from quarantine
   *
   * This method is meant to run in a thread and calling the
   * `cleanup_quarantine()` method.
   *
   * The caller is responsible for locking and unlocking the
   * mutex `mutex_quarantine_`.
   *
   */
  virtual void quarantine_manager_thread() noexcept;

  /** @brief Checks and removes servers from quarantine
   *
   * This method removes servers from quarantine while trying to establish
   * a connection. It is used in a seperate thread and will update the
   * quarantine list, and will keep trying until the list is empty.
   * A conditional variable is used to notify the thread servers were
   * quarantined.
   *
   */
  virtual void cleanup_quarantine() noexcept;


  /** @brief List of destinations which are quarantined */
  std::vector<size_t> quarantined_;

  /** @brief Conditional variable blocking quarantine manager thread */
  std::condition_variable condvar_quarantine_;

  /** @brief Mutex for quarantine manager thread */
  std::mutex mutex_quarantine_manager_;

  /** @brief Mutex for updating quarantine */
  std::mutex mutex_quarantine_;

  /** @brief Quarantine manager thread */
  std::thread quarantine_thread_;

  /** @brief Whether we are stopping */
  std::atomic_bool stopping_{false};
};


#endif // ROUTING_DEST_ROUND_ROBIN_INCLUDED
