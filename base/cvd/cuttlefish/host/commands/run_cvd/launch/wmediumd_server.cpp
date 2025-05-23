//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/run_cvd/launch/wmediumd_server.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/wait_for_unix_socket.h"
#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {
namespace {

// SetupFeature class for waiting wmediumd server to be settled.
// This class is used by the instance that does not launches wmediumd.
// TODO(b/276832089) remove this when run_env implementation is completed.
class ValidateWmediumdService : public SetupFeature {
 public:
  INJECT(ValidateWmediumdService(
      const CuttlefishConfig& config,
      const CuttlefishConfig::EnvironmentSpecific& environment,
      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), environment_(environment), instance_(instance) {}
  std::string Name() const override { return "ValidateWmediumdService"; }
  bool Enabled() const override {
    return environment_.enable_wifi() && config_.virtio_mac80211_hwsim() &&
           !instance_.start_wmediumd_instance();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    if (!environment_.wmediumd_api_server_socket().empty()) {
      CF_EXPECT(
          WaitForUnixSocket(environment_.wmediumd_api_server_socket(), 30));
    }
    CF_EXPECT(WaitForUnixSocket(environment_.vhost_user_mac80211_hwsim(), 30));

    return {};
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::EnvironmentSpecific& environment_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

WmediumdServer::WmediumdServer(
    const CuttlefishConfig::EnvironmentSpecific& environment,
    const CuttlefishConfig::InstanceSpecific& instance, LogTeeCreator& log_tee,
    GrpcSocketCreator& grpc_socket)
    : environment_(environment),
      instance_(instance),
      log_tee_(log_tee),
      grpc_socket_(grpc_socket) {}

Result<std::vector<MonitorCommand>> WmediumdServer::Commands() {
  Command cmd(WmediumdBinary());
  cmd.AddParameter("-u", environment_.vhost_user_mac80211_hwsim());
  cmd.AddParameter("-a", environment_.wmediumd_api_server_socket());
  cmd.AddParameter("-c", config_path_);

  cmd.AddParameter("--grpc_uds_path=", grpc_socket_.CreateGrpcSocket(Name()));

  std::vector<MonitorCommand> commands;
  commands.emplace_back(CF_EXPECT(log_tee_.CreateFullLogTee(cmd, "wmediumd")));
  commands.emplace_back(std::move(cmd));
  return commands;
}

std::string WmediumdServer::Name() const { return "WmediumdServer"; }

bool WmediumdServer::Enabled() const {
  return instance_.start_wmediumd_instance();
}

Result<void> WmediumdServer::WaitForAvailability() const {
  if (Enabled()) {
    if (!environment_.wmediumd_api_server_socket().empty()) {
      CF_EXPECT(
          WaitForUnixSocket(environment_.wmediumd_api_server_socket(), 30));
    }
    CF_EXPECT(WaitForUnixSocket(environment_.vhost_user_mac80211_hwsim(), 30));
  }

  return {};
}

std::unordered_set<SetupFeature*> WmediumdServer::Dependencies() const {
  return {};
}

Result<void> WmediumdServer::ResultSetup() {
  // If wmediumd configuration is given, use it
  if (!environment_.wmediumd_config().empty()) {
    config_path_ = environment_.wmediumd_config();
    return {};
  }
  // Otherwise, generate wmediumd configuration using the current wifi mac
  // prefix before start
  config_path_ = environment_.PerEnvironmentPath("wmediumd.cfg");
  Command gen_config_cmd(WmediumdGenConfigBinary());
  gen_config_cmd.AddParameter("-o", config_path_);
  gen_config_cmd.AddParameter("-p", environment_.wmediumd_mac_prefix());

  int success = gen_config_cmd.Start().Wait();
  CF_EXPECT(success == 0, "Unable to run " << gen_config_cmd.Executable()
                                           << ". Exited with status "
                                           << success);

  return {};
}

fruit::Component<fruit::Required<
    const CuttlefishConfig, const CuttlefishConfig::EnvironmentSpecific,
    const CuttlefishConfig::InstanceSpecific, LogTeeCreator, GrpcSocketCreator>>
WmediumdServerComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, WmediumdServer>()
      .addMultibinding<CommandSource, WmediumdServer>()
      .addMultibinding<SetupFeature, WmediumdServer>()
      .addMultibinding<SetupFeature, ValidateWmediumdService>();
}

}  // namespace cuttlefish
