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

#pragma once

#include "host/commands/assemble_cvd/boot_config.h"
#include "host/libs/avb/avb.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/feature/feature.h"

namespace cuttlefish {

Result<void> RepackKernelRamdisk(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance, const Avb& avb);

Result<void> GeneratePersistentBootconfig(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);

Result<void> Gem5ImageUnpacker(const CuttlefishConfig&,
                               AutoSetup<RepackKernelRamdisk>::Type&);

Result<void> GeneratePersistentVbmeta(
    const CuttlefishConfig::InstanceSpecific&,
    AutoSetup<InitBootloaderEnvPartition>::Type&,
    AutoSetup<GeneratePersistentBootconfig>::Type&);

Result<void> InitializeFactoryResetProtected(
    const CuttlefishConfig::InstanceSpecific&);

Result<void> InitializeInstanceCompositeDisk(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
    AutoSetup<InitializeFactoryResetProtected>::Type&,
    AutoSetup<GeneratePersistentVbmeta>::Type&);

}  // namespace cuttlefish
