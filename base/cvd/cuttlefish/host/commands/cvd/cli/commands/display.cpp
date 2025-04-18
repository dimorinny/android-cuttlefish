/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/display.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace {
constexpr char kSummaryHelpText[] =
    R"(Enables hotplug/unplug of displays from running cuttlefish virtual devices)";

constexpr char kDetailedHelpText[] =
    R"(

usage: cvd display <command> <args>

Commands:
    help <command>      Print help for a command.
    add                 Adds a new display to a given device.
    list                Prints the currently connected displays.
    remove              Removes a display from a given device.
)";

class CvdDisplayCommandHandler : public CvdCommandHandler {
 public:
  CvdDisplayCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager} {}

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    const cvd_common::Envs& env = request.Env();

    std::vector<std::string> subcmd_args = request.SubcommandArguments();

    bool is_help = CF_EXPECT(IsHelp(subcmd_args));
    // may modify subcmd_args by consuming in parsing
    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, subcmd_args, env))
                : CF_EXPECT(NonHelpCommand(request, subcmd_args, env));

    siginfo_t infop;  // NOLINT(misc-include-cleaner)
    command.Start().Wait(&infop, WEXITED);

    CF_EXPECT(CheckProcessExitedNormally(infop));
    return {};
  }

  cvd_common::Args CmdList() const override { return {"display"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<Command> HelpCommand(const CommandRequest& request,
                              const cvd_common::Args& subcmd_args,
                              cvd_common::Envs envs) {
    auto android_host_out = CF_EXPECT(AndroidHostPath(envs));
    auto cvd_display_bin_path =
        ConcatToString(android_host_out, "/bin/", kDisplayBin);
    std::string home = Contains(envs, "HOME") ? envs.at("HOME")
                                              : CF_EXPECT(SystemWideUserHome());
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;
    ConstructCommandParam construct_cmd_param{.bin_path = cvd_display_bin_path,
                                              .home = home,
                                              .args = subcmd_args,
                                              .envs = std::move(envs),
                                              .working_dir = CurrentDirectory(),
                                              .command_name = kDisplayBin};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<Command> NonHelpCommand(const CommandRequest& request,
                                 cvd_common::Args& subcmd_args,
                                 cvd_common::Envs envs) {
    // test if there is --instance_num flag
    int instance_num = -1;
    Flag instance_num_flag = GflagsCompatFlag("instance_num", instance_num);

    CF_EXPECT(ConsumeFlags({instance_num_flag}, subcmd_args));

    auto [instance, group] =
        instance_num >= 0
            ? CF_EXPECT(instance_manager_.FindInstanceWithGroup(
                  {.instance_id = instance_num}))
            : CF_EXPECT(selector::SelectInstance(instance_manager_, request));
    const auto& home = group.Proto().home_directory();

    const auto& android_host_out = group.Proto().host_artifacts_path();
    auto cvd_display_bin_path =
        ConcatToString(android_host_out, "/bin/", kDisplayBin);

    cvd_common::Args cvd_env_args{subcmd_args};
    cvd_env_args.push_back(ConcatToString("--instance_num=", instance.id()));
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::stringstream command_to_issue;
    std::cerr << "HOME=" << home << " " << kAndroidHostOut << "="
              << android_host_out << " " << kAndroidSoongHostOut << "="
              << android_host_out << " " << cvd_display_bin_path << " ";
    for (const auto& arg : cvd_env_args) {
      std::cerr << arg << " ";
    }

    ConstructCommandParam construct_cmd_param{.bin_path = cvd_display_bin_path,
                                              .home = home,
                                              .args = cvd_env_args,
                                              .envs = std::move(envs),
                                              .working_dir = CurrentDirectory(),
                                              .command_name = kDisplayBin};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<bool> IsHelp(const cvd_common::Args& cmd_args) const {
    // cvd display --help, --helpxml, etc or simply cvd display
    if (cmd_args.empty() || CF_EXPECT(HasHelpFlag(cmd_args))) {
      return true;
    }
    // cvd display help <subcommand> format
    return (cmd_args.front() == "help");
  }

  InstanceManager& instance_manager_;
  static constexpr char kDisplayBin[] = "cvd_internal_display";
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdDisplayCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdCommandHandler>(
      new CvdDisplayCommandHandler(instance_manager));
}

}  // namespace cuttlefish
