//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/vhost_device_vsock.h"

#include <unistd.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fmt/core.h>
#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/wait_for_unix_socket.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {
namespace {

class VhostDeviceVsock : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(VhostDeviceVsock(LogTeeCreator& log_tee,
                          const CuttlefishConfig::InstanceSpecific& instance,
                          const CuttlefishConfig& cfconfig));

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override;

  // SetupFeature
  std::string Name() const override;
  bool Enabled() const override;

  Result<void> WaitForAvailability() const override;

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  LogTeeCreator& log_tee_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  const CuttlefishConfig& cfconfig_;
};

VhostDeviceVsock::VhostDeviceVsock(
    LogTeeCreator& log_tee, const CuttlefishConfig::InstanceSpecific& instance,
    const CuttlefishConfig& cfconfig)
    : log_tee_(log_tee), instance_(instance), cfconfig_(cfconfig) {}

Result<std::vector<MonitorCommand>> VhostDeviceVsock::Commands() {
  auto instances = cfconfig_.Instances();
  if (instance_.serial_number() != instances[0].serial_number()) {
    return {};
  }

  Command command(ProcessRestarterBinary());
  command.AddParameter("-when_killed");
  command.AddParameter("-when_dumped");
  command.AddParameter("-when_exited_with_failure");
  command.AddParameter("--");
  command.AddParameter(HostBinaryPath("vhost_device_vsock"));
  command.AddEnvironmentVariable("RUST_BACKTRACE", "1");
  command.AddEnvironmentVariable("RUST_LOG", "debug");

  for (auto i : instances) {
    std::string isolation_groups = "";
    if (!i.vsock_guest_group().empty()) {
      isolation_groups = ",groups=" + i.vsock_guest_group();
    }

    auto param = fmt::format(
        "guest-cid={1},socket={0}/vsock_{1}_{2}/vhost.socket,uds-path={0}/"
        "vsock_{1}_{2}/vm.vsock{3}",
        TempDir(), i.vsock_guest_cid(), getuid(), isolation_groups);
    command.AddParameter("--vm");
    command.AddParameter(param);
    LOG(INFO) << "VhostDeviceVsock::vhost param is:" << param;
  }

  std::vector<MonitorCommand> commands;
  commands.emplace_back(
      CF_EXPECT(log_tee_.CreateFullLogTee(command, "vhost_device_vsock")));
  commands.emplace_back(std::move(command));
  return commands;
}

std::string VhostDeviceVsock::Name() const { return "VhostDeviceVsock"; }

bool VhostDeviceVsock::Enabled() const { return instance_.vhost_user_vsock(); }

Result<void> VhostDeviceVsock::WaitForAvailability() const {
  if (Enabled()) {
    CF_EXPECT(WaitForUnixSocket(
        fmt::format("{}/vsock_{}_{}/vm.vsock", TempDir(),
                    instance_.vsock_guest_cid(), std::to_string(getuid())),
        30));
  }
  return {};
}

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
VhostDeviceVsockComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, VhostDeviceVsock>()
      .addMultibinding<CommandSource, VhostDeviceVsock>()
      .addMultibinding<SetupFeature, VhostDeviceVsock>();
}

}  // namespace cuttlefish
