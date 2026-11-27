//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wethifiprocessor.h"
#include "wethificontroller.h"
#include "wethificids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "WetHiFi"

using namespace Steinberg::Vst;
using namespace Yonie;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------

BEGIN_FACTORY_DEF ("WET VST",
                   "https://github.com/yonie",
                   "mailto:contact@wetvst.com")

	DEF_CLASS2 (INLINE_UID_FROM_FUID(kWetHiFiProcessorUID),
				PClassInfo::kManyInstances,
				kVstAudioEffectClass,
				stringPluginName,
				Vst::kDistributable,
				WetHiFiVST3Category,
				FULL_VERSION_STR,
				kVstVersionString,
				WetHiFiProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kWetHiFiControllerUID),
				PClassInfo::kManyInstances,
				kVstComponentControllerClass,
				stringPluginName "Controller",
				0,
				"",
				FULL_VERSION_STR,
				kVstVersionString,
				WetHiFiController::createInstance)

END_FACTORY
