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

#include "common.h"
#include "utils.h"
#include "gmock/gmock.h"
#include "router_component_test.h"

Path g_origin_path;


/*
 * The layout for RPM and DEB uses /var/log/mysqlrouter as INSTALL_LOGDIR.
 * Since tests don't have access to this directory, for RPM and DEB those tests
 * are skipped.
 */
#ifndef SKIP_BOOTSTRAP_SYSTEM_DEPLOYMENT_TESTS

class RouterBootstrapSystemDeploymentTest : public RouterComponentTest, public ::testing::Test {
protected:
 void SetUp() override {
   set_origin(g_origin_path);
   RouterComponentTest::SetUp();
   init_tmp_dir();
 }

 void TearDown() override {
   purge_dir(tmp_dir_);
 }

 RouterComponentTest::CommandHandle launch_router(const std::string& router_path,
                                    const std::string &params,
                                    bool catch_stderr = true,
                                    bool with_sudo = false) const {
   std::string sudo_str(with_sudo ? "sudo --non-interactive " : "");
   std::string cmd = sudo_str + router_path;

   return launch_command(cmd, params, catch_stderr);
 }

 /*
  * Create temporary directory that represents system deployment
  * layout for mysql bootstrap. A mysql executable is copied to
  * tmp_dir_/stage/bin/ and then an execution permission is assign to it.
  *
  * After the test is completed, the whole temporary directory is deleted.
  */
 void init_tmp_dir() {
   tmp_dir_ = get_tmp_dir();
   mysqlrouter::mkdir(tmp_dir_ + "/stage", 0700);
   mysqlrouter::mkdir(tmp_dir_ + "/stage/bin", 0700);
   exec_file_ = tmp_dir_ + "/stage/bin/mysqlrouter";
   mysqlrouter::copy_file(get_mysqlrouter_exec().str(), exec_file_);
#ifndef _WIN32
   chmod(exec_file_.c_str(), 0100);
#endif
   config_file_ = tmp_dir_ + "/stage/mysqlrouter.conf";
 }

 RouterComponentTest::CommandHandle run_server_mock() {
   const std::string json_stmts = get_data_dir().join("bootstrapper.json").str();
   server_port_ = port_pool_.get_next_available();

   // launch mock server and wait for it to start accepting connections
   auto server_mock = launch_mysql_server_mock(json_stmts, server_port_);
   bool ready = wait_for_port_ready(server_port_, 1000);
   EXPECT_TRUE(ready) << "Timed out waiting for mock server port ready\n"
                      << server_mock.get_full_output();
   return server_mock;
 }

 TcpPortPool port_pool_;

 std::string tmp_dir_;
 std::string exec_file_;
 std::string config_file_;

 unsigned server_port_;
};

TEST_F(RouterBootstrapSystemDeploymentTest, BootstrapPass) {
  auto server_mock = run_server_mock();

  // launch the router in bootstrap mode
  auto router = launch_router(exec_file_, "--bootstrap=127.0.0.1:" + std::to_string(server_port_));

  // add login hook
  router.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  // check if the bootstraping was successful
  EXPECT_TRUE(router.expect_output("MySQL Router  has now been configured for the InnoDB cluster 'test'")
    ) << router.get_full_output() << std::endl << "server: " << server_mock.get_full_output();
  EXPECT_EQ(router.wait_for_exit(), 0);
}

TEST_F(RouterBootstrapSystemDeploymentTest, No_mysqlrouter_conf_tmp_WhenBootstrapFailed) {
  /*
   * Create directory with the same name as mysql router's config file to force
   * bootstrap to fail.
   */
  mysqlrouter::mkdir(config_file_, 0700);
  auto server_mock = run_server_mock();

  // launch the router in bootstrap mode
  auto router = launch_router(exec_file_, "--bootstrap=127.0.0.1:" + std::to_string(server_port_));

  // add login hook
  router.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  EXPECT_TRUE(router.expect_output("Error: Could not save configuration file to final location", false)
      ) << router.get_full_output() << std::endl << "server: " << server_mock.get_full_output();
  EXPECT_EQ(router.wait_for_exit(), 1);

  mysql_harness::Path mysqlrouter_conf_tmp_path(tmp_dir_ + "/stage/mysqlrouter.conf.tmp");
  EXPECT_FALSE(mysqlrouter_conf_tmp_path.exists());
}

TEST_F(RouterBootstrapSystemDeploymentTest, No_mysqlrouter_key_WhenBootstrapFailed) {
  /*
   * Create directory with the same name as mysql router's config file to force
   * bootstrap to fail.
   */
  mysqlrouter::mkdir(config_file_, 0700);
  auto server_mock = run_server_mock();

  // launch the router in bootstrap mode
  auto router = launch_router(exec_file_, "--bootstrap=127.0.0.1:" + std::to_string(server_port_));

  // add login hook
  router.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  EXPECT_TRUE(router.expect_output("Error: Could not save configuration file to final location", false)
      ) << router.get_full_output() << std::endl << "server: " << server_mock.get_full_output();
  EXPECT_EQ(router.wait_for_exit(), 1);

  mysql_harness::Path mysqlrouter_key_path(tmp_dir_ + "/stage/mysqlrouter.key");
  EXPECT_FALSE(mysqlrouter_key_path.exists());
}

TEST_F(RouterBootstrapSystemDeploymentTest, IsKeyringRevertedWhenBootstrapFail) {
  static const char kMasterKeyFileSignature[] = "MRKF";

  {
    std::ofstream keyring_file(tmp_dir_ + "/stage/mysqlrouter.key",
        std::ios_base::binary | std::ios_base::trunc | std::ios_base::out);

    mysql_harness::make_file_private(tmp_dir_ + "/stage/mysqlrouter.key");
    keyring_file.write(kMasterKeyFileSignature, strlen(kMasterKeyFileSignature));
  }

  /*
   * Create directory with the same name as mysql router's config file to force
   * bootstrap to fail.
   */
  mysqlrouter::mkdir(config_file_, 0700);
  auto server_mock = run_server_mock();

  // launch the router in bootstrap mode
  auto router = launch_router(exec_file_, "--bootstrap=127.0.0.1:" + std::to_string(server_port_));

  // add login hook
  router.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  EXPECT_TRUE(router.expect_output("Error: Could not save configuration file to final location", false)
      ) << router.get_full_output() << std::endl << "server: " << server_mock.get_full_output();

  EXPECT_EQ(router.wait_for_exit(), 1);

  mysql_harness::Path mysqlrouter_key_path(tmp_dir_ + "/stage/mysqlrouter.key");
  EXPECT_TRUE(mysqlrouter_key_path.exists());

  std::ifstream keyring_file(tmp_dir_ + "/stage/mysqlrouter.key", std::ios_base::binary|std::ios_base::in);

  char buf[10] = {0};
  keyring_file.read(buf, sizeof(buf));
  EXPECT_THAT(keyring_file.gcount(), 4);
  EXPECT_THAT(std::strncmp(buf, kMasterKeyFileSignature, 4), testing::Eq(0));
}

TEST_F(RouterBootstrapSystemDeploymentTest, Keep_mysqlrouter_log_WhenBootstrapFailed) {
  /*
   * Create directory with the same name as mysql router's config file to force
   * bootstrap to fail.
   */
  mysqlrouter::mkdir(config_file_, 0700);
  auto server_mock = run_server_mock();

  // launch the router in bootstrap mode
  auto router = launch_router(exec_file_, "--bootstrap=127.0.0.1:" + std::to_string(server_port_));

  // add login hook
  router.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  EXPECT_TRUE(router.expect_output("Error: Could not save configuration file to final location", false)
      ) << router.get_full_output() << std::endl << "server: " << server_mock.get_full_output();
  EXPECT_EQ(router.wait_for_exit(), 1);

  mysql_harness::Path mysqlrouter_log_path(tmp_dir_ + "/stage/mysqlrouter.log");
  EXPECT_TRUE(mysqlrouter_log_path.exists());
}

#endif

int main(int argc, char *argv[]) {
  init_windows_sockets();
  g_origin_path = Path(argv[0]).dirname();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
