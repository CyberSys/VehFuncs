﻿/*
    Author: Junior_Djjr - MixMods.com.br
    Created using https://github.com/DK22Pac/plugin-sdk
*/
#include "VehFuncs.h"

// Mod utilities & API
#include "IniReader/IniReader.h"
#include "AtomicsVisibility.h"
#include "IndieVehHandlingsAPI.h"
#include "CustomSeed.h"
#include "Matrixbackup.h"
#include "Utilities.h"
#include "DamageableRearWings.h" 
//#include "CheckRepair.h"

// Mod funcs
#include "Patches.h"
#include "FixMaterials.h"
#include "DigitalSpeedo.h"
#include "DigitalOdometer.h"
#include "Characteristics.h"
#include "RecursiveExtras.h"
#include "GearAndFan.h"
#include "Shake.h"
#include "Pedal.h"
#include "Footpegs.h"
#include "PopupLights.h"
#include "Trifork.h"
#include "Spoiler.h"
#include "Anims.h"
#include "Steer.h"
 
// Dependences
#include "../injector/assembly.hpp"
#include "extensions/ScriptCommands.h"
#include "CVisibilityPlugins.h"
#include "CTxdStore.h"
#include "CModelInfo.h"
#include "NodeName.h"
#include "CStreaming.h"
#include "CGeneral.h"
#include "CTask.h"
#include "CTimer.h"
#include "CVector.h"
#include "CText.h"
#include <time.h>
#include <stdio.h>
#include <string>
#include <iomanip>
#include <sstream>

// Disable warnings (caution!)
#pragma warning( disable : 4244 ) // data loss

// Global vars
int G_i = 0;
uintptr_t AtomicAlphaCallBack;
uint32_t txdIndexStart;
VehicleExtendedData<ExtendedData> xData;
fstream lg;
bool IVFinstalled = false, APPinstalled = false, bFirstFrame = false, bFirstScriptFrame = false, bNewFrame = false, bIndieVehicles = false;
CVehicle *curVehicle;
bool noChassis = false;
bool ignoreCrashInfo = false;
int lastRenderedVehicleModel = -1;
int tempVehicleModel = -1;
CVehicle *lastInitializedVehicle = nullptr;
int lastInitializedVehicleModel = -1;
extern RwTexDictionary *vehicletxdArray[4];
extern int vehicletxdIndexArray[4];
std::list<std::pair<unsigned int *, unsigned int>> resetMats;

// Ini settings
bool useLog = true;
float iniDefaultDirtMult = 1.0f;
float iniDefaultSteerAngle = 100.0f;
bool iniLogNoTextureFound = false;
bool iniLogModelRender = false;
bool iniShowCrashInfos = true;

///////////////////////////////////////////////////////////////////////////////////////////////////

class VehFuncs
{
public:
	VehFuncs()
	{

		// -- On plugin init
		CIniReader ini("VehFuncs.ini");

		if (ini.data.size() > 0)
		{
			useLog = ini.ReadInteger("Test", "Log", 1);
			iniLogNoTextureFound = ini.ReadInteger("Test", "LogNoTextureFound", 0);
			iniLogModelRender = ini.ReadInteger("Test", "LogModelRender", 0);
			iniShowCrashInfos = ini.ReadInteger("Test", "ShowCrashInfos", 0);
			iniDefaultDirtMult = ini.ReadFloat("Settings", "DefaultDirtMult", 100.0f);
			iniDefaultSteerAngle = ini.ReadFloat("Settings", "DefaultSteerAngle", 100.0f);
			if (ini.ReadInteger("Settings", "NoSwingingChassis", 0) == 1)
			{
				MakeInline<0x006AC104, 0x006AC104 + 7>([](reg_pack& regs)
				{
					if (regs.eax == 21) // misc, for firela
					{
						regs.eax = (uintptr_t)reinterpret_cast<CAutomobile*>(regs.esi)->m_aCarNodes[regs.eax]; //v161 = veh->m_aCarNodes[v160];
					}
					else regs.eax = 0; // just don't process it
				});
			}
		}
		 
		if (useLog) lg.open("VehFuncs.log", fstream::out | fstream::trunc);

		if (useLog) lg << "VF v2.1" << endl;

		if (ini.data.size() == 0) lg << "Unable to read 'VehFuncs.ini'\n";

		static bool reInit = false;
		xData = getExtData();

		// Fix for remap txd names. This also stores additional vehicle*.txd files. (NOT FOR COPCARLA YET)
		memset(vehicletxdArray, 0, sizeof(vehicletxdArray));
		memset(vehicletxdIndexArray, 0, sizeof(vehicletxdIndexArray));
		patch::RedirectCall(0x5B62C2, Patches::CustomAssignRemapTxd, true);

		// Preprocess hierarchy don't remove frames
		MakeJMP(0x004C8E30, CustomCollapseFramesCB);

		// Patch for additional vehicle*.txd. Need it even before vehicle.txd loading due to copcarla loading order.
		patch::RedirectCall(0x4C7533, Patches::Custom_RwTexDictionaryFindNamedTexture, true);

		// Fix "ug_" dummies outside "chassis" (ID 1) using first node (ID 0) (usually a wheel node).
		MakeInline<0x004C8FA1, 0x004C8FA1 + 6>([](reg_pack& regs)
		{
			if (regs.eax == 0) regs.eax = 1; // set frame ID 1 by default
			*(uint32_t*)(regs.esp + 0x64 - 0x34) = regs.eax;  //mov     [esp+64h+atomic2], eax ; frame visibility
			regs.eax = *(uint32_t*)regs.edx;  //mov     eax, [edx]
		});

		// -- Add some infos for common vehicle model crashes
		if (iniShowCrashInfos)
		{
			MakeInline<0x004C4441, 0x004C4441 + 5>([](reg_pack& regs)
			{
				regs.esi = regs.ecx; //mov     esi, ecx
				regs.eax = *(uintptr_t*)(regs.esi + 0x1C); //mov     eax, [esi + CAtomicModelInfo.base.m_pRwObject]
				if (regs.eax)
				{
					RpAtomic *atomic = (RpAtomic *)regs.eax;
					if ((uintptr_t)atomic->geometry < (uintptr_t)0x1000)
					{
						CAtomicModelInfo* modelInfo = reinterpret_cast<CAtomicModelInfo*>(regs.esi);
						LogVehicleModelWithText("GAME CRASH CAtomicModelInfo::DeleteRwObject: For TXD index (normally same as vehicle model ID) ", modelInfo->m_nTxdIndex, ": Game will crash. Check '0x004C444A' on MixMods' Crash List.");
					}
				}
			});

			MakeInline<0x0064E769, 0x0064E769 + 6>([](reg_pack& regs)
			{
				regs.edx = *(uint32_t*)(regs.ecx + 0xCC); //mov     edx, [ecx+0CCh]
				CVehicle *vehicle = (CVehicle *)regs.esi;
				tempVehicleModel = vehicle->m_nModelIndex;
			});
			MakeInline<0x006E3D9C, 0x006E3D9C + 6>([](reg_pack& regs)
			{
				if (regs.eax < (uintptr_t)0x1000)
				{
					LogVehicleModelWithText("GAME CRASH ComputeAnimDoorOffsets on vehicle model ID ", tempVehicleModel, ": Vehicle anim group not loaded. Maybe vehicle.ide line is wrong for handling.cfg line (wrong anim groups). Check '0x006E3D9C' on MixMods' Crash List.");
				}
				else
				{
					regs.eax = *(uint32_t*)(regs.eax + 0x10); //mov     eax, [eax+10h]
					regs.esi = *(uint32_t*)(regs.eax + 0x4); //mov     esi, [eax+4]
				}
				tempVehicleModel = -1;
			});

			MakeInline<0x004C7DAD, 0x004C7DAD + 7>([](reg_pack& regs)
			{
				// Note: I don't know if this crash is caused by VehFuncs itself.
				if (regs.ebp < (uintptr_t)0x1000)
				{
					CVehicleModelInfo *vehicleModelInfo = (CVehicleModelInfo *)regs.esi;
					if (vehicleModelInfo->m_pRwObject)
					{
						int wheelId = *(uint32_t*)(regs.esp + 0x8 + 0x4);
						RwFrame *frame = nullptr;
						switch (wheelId)
						{
						case 0:
							frame = CClumpModelInfo::GetFrameFromName((RpClump*)vehicleModelInfo->m_pRwObject, "wheel_lf_dummy");
							break;
						case 1:
							frame = CClumpModelInfo::GetFrameFromName((RpClump*)vehicleModelInfo->m_pRwObject, "wheel_lb_dummy");
							break;
						case 2:
							frame = CClumpModelInfo::GetFrameFromName((RpClump*)vehicleModelInfo->m_pRwObject, "wheel_rf_dummy");
							break;
						case 3:
							frame = CClumpModelInfo::GetFrameFromName((RpClump*)vehicleModelInfo->m_pRwObject, "wheel_rb_dummy");
							break;
						default:
							frame = CClumpModelInfo::GetFrameFromName((RpClump*)vehicleModelInfo->m_pRwObject, "wheel_lf_dummy");
							break;
						}
						if (frame)
						{
							regs.ebp = (uintptr_t)frame;
							if (useLog)
							{
								lg << "ERROR GetWheelPosn on vehicle model ID " << TheText.Get(vehicleModelInfo->m_szGameName) << " wheel index " << wheelId << " was going to crash. Fixed. Contact if this is caused by VehFuncs adaptation." << endl;
								lg.flush();
							}
						}
						else
						{
							string logText = "GAME CRASH GetWheelPosn on vehicle model ";
							logText.append(TheText.Get(vehicleModelInfo->m_szGameName));
							logText.append(": Required wheel index ");
							logText.append(to_string(wheelId));
							logText.append(" doesn't exist. Check '0x004C7DAD' on MixMods' Crash List.\n");
							LogCrashText(logText);
						}
					}
					else
					{
						LogVehicleModelWithText("GAME CRASH GetWheelPosn on vehicle model ID ", lastRenderedVehicleModel, " (this may be wrong): Problem with model load. Check '0x004C7DAD' on MixMods' Crash List.");
					}
				}
				regs.edx = *(uint32_t*)(regs.ebp + 0x40); //mov     edx, [ebp+40h]
				regs.eax = *(uint32_t*)(regs.esp + 0x10); //mov     eax, [esp+10h]
			});

			MakeJMP(0x00563281, Patches::CheckCrashWorldRemove, true);

			MakeJMP(0x0059BE3B, Patches::CheckCrashMatrixOperator, true);
		}

		patch::RedirectCall(0x004C5396, Patches::CheckCrashFillFrameArrayCB, true);

		// Damageable rear wings
		PatchDamageableRearWings();

		// -- On game init
		Events::initGameEvent += []
		{
			srand(time(0));
			StoreHandlingData();
			ApplyGSX(); 
			//ApplyCheckRepair();
			
			AtomicAlphaCallBack = ReadMemory<int>(0x4C7842, false);
			txdIndexStart = ReadMemory<uint32_t>(0x6D65D2 + 1, true);

			// Load generic vehicle*.txd files
			Patches::LoadAdditionalVehicleTxd();

			// Reload copcarla (because this shit is loaded before everything)
			CStreaming::SetModelIsDeletable(596);
			CStreaming::RemoveModel(596);
			CStreaming::RequestModel(596, (eStreamingFlags::PRIORITY_REQUEST | eStreamingFlags::KEEP_IN_MEMORY));
			CStreaming::LoadAllRequestedModels(true);

			// LODs (make our custom LOD always render)
			MakeNOP(0x00733241, 6);
			MakeJMP(0x00733241, Patches::ForceRenderCustomLOD);
			MakeNOP(0x00733F80, 6);
			MakeJMP(0x00733F80, Patches::ForceRenderCustomLODAlpha);
			MakeNOP(0x007344A0, 6);
			MakeJMP(0x007344A0, Patches::ForceRenderCustomLODBoatAlpha);
			MakeNOP(0x00733550, 6);
			MakeJMP(0x00733550, Patches::ForceRenderCustomLODBoat);
			MakeNOP(0x00733420, 6);
			MakeJMP(0x00733420, Patches::ForceRenderCustomLODBig);
			MakeNOP(0x00734370, 6);
			MakeJMP(0x00734370, Patches::ForceRenderCustomLODBigAlpha);
			MakeNOP(0x00733331, 6);
			MakeJMP(0x00733331, Patches::ForceRenderCustomLODTrain);
			MakeNOP(0x00734240, 6);
			MakeJMP(0x00734240, Patches::ForceRenderCustomLODTrainAlpha);
			
			

			// Preprocess hierarchy find damage atomics to apply damageable
			MakeCALL(0x4C9173, Patches::FindDamage::CustomFindDamageAtomics, true);
			WriteMemory<uint32_t>(0x4C916D + 1, memory_pointer(Patches::FindDamage::CustomFindDamageAtomicsCB).as_int(), true);


			// Render bus driver
			MakeNOP(0x0064BCB3, 6);
			MakeCALL(0x0064BCB3, Patches::RenderBus);


			// Cop functions
			MakeJMP(0x006D2379, Patches::IsLawEnforcement);
			Patches::PatchForCoplights();


			// Popup lights
			MakeInline<0x006E1CD4, 0x006E1CD4 + 6>([](reg_pack& regs)
			{
				//mov eax, [esi+590h]
				regs.eax = *(uint32_t*)(regs.esi + 0x590);

				CVehicle *veh = (CVehicle*)regs.esi;
				CEntity *entity = (CEntity*)veh;
				ExtendedData &xdata = xData.Get(veh);

				if (xdata.popupFrame[0] != nullptr)
				{
					bool noLights = false;
					CAutomobile *automobile = (CAutomobile*)veh;

					if (automobile->m_damageManager.GetLightStatus(LIGHT_FRONT_LEFT))
					{
						if (xdata.popupProgress[LIGHT_FRONT_RIGHT] != 1.0f)
						{
							noLights = true;
						}
					}
					else if (automobile->m_damageManager.GetLightStatus(LIGHT_FRONT_RIGHT))
					{
						if (xdata.popupProgress[LIGHT_FRONT_LEFT] != 1.0f)
						{
							noLights = true;
						}
					}
					else
					{
						if (xdata.popupProgress[LIGHT_FRONT_LEFT] != 1.0f && xdata.popupProgress[LIGHT_FRONT_RIGHT] != 1.0f)
						{
							noLights = true;
						}
					}

					if (noLights) *(uint32_t*)(regs.esp - 0x4) = 0x6E28EF; //don't process lights
				}
			});


			// Upgrade updated
			MakeInline<0x006E32AB, 0x006E32AB + 8>([](reg_pack& regs)
			{
				*(uint32_t*)(regs.esp + 0x14) = -1; //mov     dword ptr [esp+14h], 0FFFFFFFFh

				CVehicle *veh = (CVehicle *)regs.edi;
				ExtendedData &xdata = xData.Get(veh);
				xdata.flags.bUpgradesUpdated = true;
			});

			MakeInline<0x006DF96C, 0x006DF96C + 6>([](reg_pack& regs)
			{
				regs.eax = *(uint16_t*)(regs.esi + 0x12); //mov     ax, [esi+12h]
				regs.esi = regs.eax; //mov     esi, eax

				CVehicle *veh = (CVehicle *)regs.edi;
				ExtendedData &xdata = xData.Get(veh);
				xdata.flags.bUpgradesUpdated = true;
			});

			// RpClumpRender
			MakeNOP(0x00749B3E, 9, true);
			MakeJMP(0x00749B3E, Patches::NeverRender);

			
			// Upgrade replace
			// Add
			MakeCALL(0x006D386C, CustomRwFrameForAllChildren_AddUpgrade);
			// Remove
			MakeCALL(0x006D3A18, CustomRwFrameForAllChildren_RemoveUpgrade);
			// Set exhaust/wheel to destroy
			MakeInline<0x006D3746, 0x006D3746 + 5>([](reg_pack& regs)
			{
				regs.ecx = 0x3F800000; // original code
				RwFrame *frame = (RwFrame *)regs.edi;
				FRAME_EXTENSION(frame)->flags.bDestroyOnRemoveUpgrade = true;
			});
			// Set others to destroy
			MakeInline<0x006D3539, 0x006D3539 + 5>([](reg_pack& regs)
			{
				//mov     esi, eax
				//mov     edi, [esi+4]
				regs.esi = regs.eax;
				regs.edi = *(uintptr_t*)(regs.esi + 0x4);
				RwFrame *frame = (RwFrame *)regs.edi;
				FRAME_EXTENSION(frame)->flags.bDestroyOnRemoveUpgrade = true;
			}); 
			// Add original
			/*MakeInline<0x006D3A35, 0x006D3A35 + 18>([](reg_pack& regs)
			{
				RwFrame *sourceFrame = (RwFrame *)regs.eax;
				RwFrame *destFrame = (RwFrame *)regs.edi;
				RpClump *destClump = (RpClump *)regs.edx;
				//CloneNode(sourceFrame, destClump, destFrame, true, true);
				*(uint32_t*)0xC1CB58 = (uint32_t)destClump;
				RwFrameForAllObjects(sourceFrame, CopyObjectsCB, destFrame);
			});
			WriteMemory<uint8_t>(0x6D3A47 + 2, 0x28, true);*/

			
			// Hitch patch
			// CAutomobile
			int *vmt = (int*)0x00871120;
			int *getlink = vmt + (0xF0 / sizeof(vmt));
			Patches::Hitch::setOriginalFun((Patches::Hitch::GetTowBarPos_t)ReadMemory<int*>(getlink, true));
			WriteMemory(getlink, memory_pointer(Patches::Hitch::GetTowBarPosToHook).as_int(), true);
			// CTrailer
			Patches::Hitch::setOriginalFun_Trailer((Patches::Hitch::GetTowBarPos_t)ReadMemory<int*>(0x871D18, true));
			WriteMemory(0x871D18, memory_pointer(Patches::Hitch::GetTowBarPosToHook).as_int(), true);

			if (useLog) lg << "Core: Started\n";
		};



		// -- On plugins attach
		Events::attachRwPluginsEvent += []() 
		{
			FramePluginOffset = RwFrameRegisterPlugin(sizeof(FramePlugin), PLUGIN_ID_STR, (RwPluginObjectConstructor)FramePlugin::Init, (RwPluginObjectDestructor)FramePlugin::Destroy, (RwPluginObjectCopy)FramePlugin::Copy);
		};



		// -- On game init
		Events::initGameEvent.after += []()
		{
			if (!bFirstFrame)
			{
				if (ReadMemory<uint32_t>(0x004C9148, true) != 0x004C8E30)
				{
					if (useLog) lg << "Core: IVF installed\n";
					WriteMemory(0x004C9148, (int)0x004C8E30, true); // unhook IVF collapse frames
					IVFinstalled = true;
				}
				else
				{
					IVFinstalled = false;
				}

				if (GetModuleHandleA("IndieVehicles.asi")) {
					bIndieVehicles = true;
				}
				else
				{
					lg << "WARNING: Some VehFuncs functions need IndieVehicles.asi installed." << "\n\n";
					bIndieVehicles = false;
				}

				if (useLog) lg.flush();
				bFirstFrame = true;
			}
		};


		// -- On process script (gameProcessEvent isn't compatible with SAMP)
		Events::processScriptsEvent.after += []
		{
			lastRenderedVehicleModel = -1;
			lastInitializedVehicleModel = -1;

			if (!bFirstScriptFrame)
			{
				if (GetModuleHandleA("CLEO.asi")) {
					unsigned int script;
					const unsigned int GET_SCRIPT_STRUCT_NAMED = 0x10AAA;
					Command<GET_SCRIPT_STRUCT_NAMED>("NEWSVAN", &script);
					if (script)
					{
						if (useLog) lg << "Core: AD installed\n";
						APPinstalled = true;
					}
					else
					{
						APPinstalled = false;
					}
					
				} else if (useLog) lg << "Core: CLEO isn't installed." << "\n\n";
				if (useLog) lg.flush();
				bFirstScriptFrame = true;
			}
			bNewFrame = true;
		};


		// -- On vehicle set model
		Events::vehicleSetModelEvent += [](CVehicle *vehicle, int modelId) 
		{
			lastInitializedVehicle = vehicle;
			lastInitializedVehicleModel = modelId;
			if (iniLogModelRender && useLog)
			{
				lg << "After Init model " << lastInitializedVehicleModel << endl;
				lg.flush();
			}
			if (iniDefaultDirtMult != 1.0f) {
				vehicle->m_fDirtLevel *= iniDefaultDirtMult;
				if (vehicle->m_fDirtLevel > 15.0f) vehicle->m_fDirtLevel = 1.0f;
			}
			ExtendedData &xdata = xData.Get(vehicle);
			xdata.nodesProcess = true;
			xdata.nodesProcessForIndieHandling = true;
			xdata.ReInitForReSearch();
			//if (useLog) lg << "Core: Model set\n";
		};



		// -- On vehicle pre render
		vehiclePreRenderEvent += [](CVehicle *vehicle)
		{
			lastRenderedVehicleModel = vehicle->m_nModelIndex;
			if (iniLogModelRender && useLog)
			{
				lg << "After Pre Render " << lastRenderedVehicleModel << endl;
				lg.flush();
			}
			if ((uintptr_t)vehicle->m_pRwClump < (uintptr_t)0x1000 && iniShowCrashInfos) LogVehicleModelWithText("GAME CRASH Clump is invalid on vehicle model ID ", vehicle->m_nModelIndex, ": Game will crash. Check MixMods' Crash List.");
		};

		// -- On vehicle render
		Events::vehicleRenderEvent.before += [](CVehicle *vehicle)
		{
			tempVehicleModel = -1;
			lastInitializedVehicleModel = -1;
			lastRenderedVehicleModel = vehicle->m_nModelIndex;
			if (iniLogModelRender && useLog)
			{
				lg << "Before Render " << lastRenderedVehicleModel << endl;
				lg.flush();
			}
			if ((uintptr_t)vehicle->m_pRwClump < (uintptr_t)0x1000 && iniShowCrashInfos) LogVehicleModelWithText("GAME CRASH Clump is invalid on vehicle model ID ", vehicle->m_nModelIndex, " (start): Game will crash. Check MixMods' Crash List.");

			// Reset material stuff (after render) - doesn't work on .after, I don't know why...
			for (auto &p : resetMats)
				*p.first = p.second;
			resetMats.clear();

			// Init
			curVehicle = vehicle;
			ExtendedData &xdata = xData.Get(vehicle);
			tHandlingData *handling;
			bool bReSearch = false;

			// Set custom seed
			list<CustomSeed> &customSeedList = getCustomSeedList();
			if (customSeedList.size() > 0)
			{
				list<CustomSeed> customSeedsToRemove;
				if (useLog && bNewFrame) lg << "Custom Seed: Running list size " << customSeedList.size() << "\n";

				for (list<CustomSeed>::iterator it = customSeedList.begin(); it != customSeedList.end(); ++it)
				{
					CustomSeed customSeed = *it;

					if (reinterpret_cast<int>(vehicle) == customSeed.pvehicle)
					{
						if (useLog) lg << "Custom Seed: Seed " << customSeed.seed << " set to " << customSeed.pvehicle << "\n";
						xdata.randomSeed = customSeed.seed;
						customSeedsToRemove.push_back(*it);
					}
					else {
						if (CTimer::m_snTimeInMilliseconds > customSeed.timeToDeleteOfNotFound)
						{
							if (useLog) lg << "Custom Seed: Not found vehicle " << customSeed.pvehicle << ". Time limit.\n";
							customSeedsToRemove.push_back(*it);
						}
					}
				}
				for (list<CustomSeed>::iterator it = customSeedsToRemove.begin(); it != customSeedsToRemove.end(); ++it)
				{
					customSeedList.remove(*it);
				}
				customSeedsToRemove.clear(); 
			}


			// For IndieVehHandling / Get re-search
			bool isIndieHandling = true;
			if (IsIndieHandling(vehicle, &handling))
			{
				isIndieHandling = true;
				bReSearch = ExtraInfoBitReSearch(vehicle, handling);
			}

			// Search nodes
			if (xdata.nodesProcess || bReSearch)
			{
				if (bReSearch)
				{
					// Reset extended data
					xdata.ReInitForReSearch();
				}
				// Clear temp class list
				list<string> &classList = getClassList();
				classList.clear();

				// Need to fix lack of chassis? (fixes tuning and other stuff)
				if (vehicle->m_nVehicleSubClass == VEHICLE_BOAT || vehicle->m_nVehicleSubClass == VEHICLE_TRAIN) noChassis = false;
				else noChassis = (reinterpret_cast<CAutomobile*>(vehicle)->m_aCarNodes[CAR_CHASSIS]) ? false : true;

				// Process all nodes
				xdata.randomSeedUsage = 0;
				RwFrame *rootFrame = (RwFrame *)vehicle->m_pRwClump->object.parent;
				FindNodesRecursive(rootFrame, vehicle, bReSearch, false);

				if (!bReSearch)
				{
					// Set wheels
					if (xdata.wheelFrame[0]) SetWheel(xdata.wheelFrame, vehicle);

					// Fix materials
					FixMaterials(vehicle->m_pRwClump);
				}

				// Post set
				SetCharacteristicsInRender(vehicle, bReSearch);
				xdata.nodesProcess = false;

				// Set kms
				if (xdata.kms == -1.0f)
				{
					float factorA = Random(5000.0f, 500000.0f);
					if (vehicle->m_nVehicleFlags.bIsDamaged) factorA *= 2.0f;
					float factorB = Random(0.0f, vehicle->m_fDirtLevel * 100000.0f);
					xdata.kms = factorA + factorB;
				}

				// TEST
				/*
				RwStream *splate = RwStreamOpen(RwStreamType::rwSTREAMFILENAME, RwStreamAccessType::rwSTREAMREAD, "grass0_1.dff");
				RpClump *cplate;
				RpAtomic *aplate;

				if (RwStreamFindChunk(splate, 0x10, 0, 0)) {

					cplate = RpClumpStreamRead(splate);
					aplate = GetFirstAtomic(cplate);

					if (aplate) {

						CAutomobile *aveh = (CAutomobile*)vehicle;
						RwFrame *boot = aveh->m_aCarNodes[CAR_BONNET];

						if (boot) {

							RwFrame *newFrame = RwFrameCreate();
							RpAtomic *plateAtomic = RpAtomicClone(aplate);

							RpAtomicSetFrame(plateAtomic, newFrame);
							RpClumpAddAtomic(vehicle->m_pRwClump, plateAtomic);
							RwFrameAddChild(boot, newFrame);


							int slot = CTxdStore::AddTxdSlot("platetest");
							CTxdStore::LoadTxd(slot, "plant1.txd");
							CTxdStore::AddRef(slot);
							CTxdStore::SetCurrentTxd(slot);
							RwTexture *text = plugin::CallAndReturn<RwTexture*, 0x7F3AC0, const char*, const char*>("txgrass1_3", 0);
							CTxdStore::PopCurrentTxd();

							CAutomobile *aveh = (CAutomobile*)vehicle;
							RwFrame *boot = aveh->m_aCarNodes[CAR_BONNET];

							if (boot) {

								RwFrame *newFrame = RwFrameCreate();
								RpAtomic *plateAtomic = RpAtomicClone(aplate);

								RpAtomicSetFrame(plateAtomic, newFrame);
								RpClumpAddAtomic(vehicle->m_pRwClump, plateAtomic);
								RwFrameAddChild(boot, newFrame);

								RpGeometryLock(plateAtomic->geometry, 0xFFF);
								plateAtomic->geometry->flags = plateAtomic->geometry->flags & 0xFFFFFFCF | 0x40;
								RpGeometryUnlock(plateAtomic->geometry);

								int slot = CTxdStore::AddTxdSlot("platetest");
								CTxdStore::LoadTxd(slot, "plant1.txd");
								CTxdStore::AddRef(slot);
								CTxdStore::SetCurrentTxd(slot);
								RwTexture *text = plugin::CallAndReturn<RwTexture*, 0x7F3AC0, const char*, const char*>("txgrass1_3", 0);
								CTxdStore::PopCurrentTxd();

								if (useLog) lg << "Shared Extras Texture: " << text << "\n";
								fs.flush();

								text->filterAddressing = 2;

								if (text) {
									RwRGBA *rgb = new RwRGBA{ 255,255,255 };
									plateAtomic->geometry->matList.materials[0]->color = *rgb;
									plateAtomic->geometry->matList.materials[0]->texture = text;
								}

							}

						}
					}

				}
				RwStreamClose(splate, 0);

				if (useLog) lg << "Shared Extras: " << aplate << "\n";
				fs.flush();
			*/
			}

			if (isIndieHandling && (xdata.nodesProcessForIndieHandling || bReSearch))
			{
				SetCharacteristicsForIndieHandling(vehicle, bReSearch);
				xdata.nodesProcessForIndieHandling = false;
			}

			///////////////////////////////////////////////////////////////////////////////////////

			int subClass = vehicle->m_nVehicleSubClass;

			// Process store smooth pedal
			int gasSoundProgress = vehicle->m_vehicleAudio.field_14C;
			int rpmSound = vehicle->m_vehicleAudio.field_148;

			if ((subClass == VEHICLE_AUTOMOBILE || subClass == VEHICLE_BIKE || subClass == VEHICLE_MTRUCK || subClass == VEHICLE_QUAD) &&
				gasSoundProgress == 0 && vehicle->m_fMovingSpeed > 0.2f && rpmSound != -1)
			{ // fix me: the last gear (max speed) is ignored
				xdata.smoothGasPedal = 0.0f;
			}
			else
			{
				float gasPedal = abs(vehicle->m_fGasPedal);
				if (gasPedal > 0.0f)
				{
					xdata.smoothGasPedal += (CTimer::ms_fTimeStep / 1.6666f) * (gasPedal / 6.0f);
					if (xdata.smoothGasPedal > 1.0f) xdata.smoothGasPedal = 1.0f;
					else if (xdata.smoothGasPedal > gasPedal) xdata.smoothGasPedal = gasPedal;
				}
				else
				{
					if (xdata.smoothGasPedal > 0.0f)
					{
						xdata.smoothGasPedal -= (CTimer::ms_fTimeStep / 1.6666f) * 0.3;
						if (xdata.smoothGasPedal < 0.0f) xdata.smoothGasPedal = 0.0f;
					}
				}
			}

			float brakePedal = abs(vehicle->m_fBreakPedal);
			if (brakePedal > 0.0f)
			{
				xdata.smoothBrakePedal += (CTimer::ms_fTimeStep / 1.6666f) * (brakePedal / 6.0f);
				if (xdata.smoothBrakePedal > 1.0f) xdata.smoothBrakePedal = 1.0f;
				else if (xdata.smoothBrakePedal > brakePedal) xdata.smoothBrakePedal = brakePedal;
			}
			else
			{
				if (xdata.smoothBrakePedal > 0.0f)
				{
					xdata.smoothBrakePedal -= (CTimer::ms_fTimeStep / 1.6666f) * 0.3;
					if (xdata.smoothBrakePedal < 0.0f) xdata.smoothBrakePedal = 0.0f;
				}
			}

			float realisticSpeed = GetVehicleSpeedRealistic(vehicle);
			xdata.realisticSpeed = realisticSpeed;
			double kmSum = abs((double)realisticSpeed);
			kmSum /= 3.6; // to m/s
			kmSum *= 2.0; // for timeStep use
			kmSum *= 0.96; // final tweak to fix imprecision, because, I don't know
			kmSum /= 10000.0; // so last digit is 100 meters*/

			xdata.kms += (kmSum * CTimer::ms_fTimeStep);

			///////////////////////////////////////////////////////////////////////////////////////

			// Process material stuff (before render)
			if (xdata.taxiSignMaterial)
			{
				if (reinterpret_cast<CAutomobile*>(vehicle)->taxiAvaliable & 1)
				{
					resetMats.push_back(std::make_pair(reinterpret_cast<unsigned int *>(&xdata.taxiSignMaterial->surfaceProps.ambient), *reinterpret_cast<unsigned int *>(&xdata.taxiSignMaterial->surfaceProps.ambient)));
					xdata.taxiSignMaterial->surfaceProps.ambient = 10.0f;
				}
			}

			// Process speedo
			if (xdata.speedoFrame != nullptr)
			{
				if (xdata.speedoDigits != nullptr)
				{
					ProcessDigitalSpeedo(vehicle, xdata.speedoFrame);
				}
			}

			// Process odometer
			if (xdata.odometerFrame != nullptr)
			{
				if (xdata.odometerDigits != nullptr)
				{
					ProcessDigitalOdometer(vehicle, xdata.odometerFrame);
				}
			}


			if (vehicle->m_nVehicleFlags.bEngineOn)
			{
				// Process gear
				if (!xdata.gearFrame.empty()) ProcessRotatePart(vehicle, xdata.gearFrame, true);

				// Process fan
				if (!xdata.fanFrame.empty()) ProcessRotatePart(vehicle, xdata.fanFrame, false);
			}
			// Process shake
			if (!xdata.shakeFrame.empty()) ProcessShake(vehicle, xdata.shakeFrame);

			// Process gas pedal
			if (!xdata.gaspedalFrame.empty()) ProcessPedal(vehicle, xdata.gaspedalFrame, 1);

			// Process brake pedal
			if (!xdata.brakepedalFrame.empty()) ProcessPedal(vehicle, xdata.brakepedalFrame, 2);

			if (vehicle->m_fHealth > 0 && !vehicle->m_nVehicleFlags.bEngineBroken && !vehicle->m_nVehicleFlags.bIsDrowning)
			{
				// Process anims
				if (!xdata.anims.empty()) ProcessAnims(vehicle, xdata.anims);

				// Process popup lights
				if (xdata.popupFrame[0]) ProcessPopup(vehicle, &xdata);
			}

			// Process trifork
			if (xdata.triforkFrame) ProcessTrifork(vehicle, xdata.triforkFrame);

			// Process footpegs
			if (vehicle->m_nVehicleSubClass == VEHICLE_BIKE || vehicle->m_nVehicleSubClass == VEHICLE_BMX)
			{
				if (!xdata.fpegFront.empty()) ProcessFootpegs(vehicle, xdata.fpegFront, 1);
				if (!xdata.fpegBack.empty()) ProcessFootpegs(vehicle, xdata.fpegBack, 2);
			}

			// Process tuning spoiler (before render)
			if (xdata.flags.bUpgradesUpdated)
			{
				ProcessSpoiler(vehicle, xdata.spoilerFrames, false);
			}

			// Process steer
			if (!xdata.steer.empty()) ProcessSteer(vehicle, xdata.steer);

			if ((uintptr_t)vehicle->m_pRwClump < (uintptr_t)0x1000 && iniShowCrashInfos) LogVehicleModelWithText("GAME CRASH Clump is invalid on vehicle model ID ", vehicle->m_nModelIndex, " (end): Game will crash. Check MixMods' Crash List.");
		
			if (iniLogModelRender && useLog)
			{
				lg << "VehFuncs Update Finished " << vehicle->m_nModelIndex << endl;
				lg.flush();
			}
		};

		///////////////////////////////////////////////////////////////////////////////////////

		Events::vehicleRenderEvent.after += [](CVehicle *vehicle)
		{
			curVehicle = vehicle;
			ExtendedData &xdata = xData.Get(vehicle);

			// Process tuning spoiler (after render)
			if (xdata.flags.bUpgradesUpdated)
			{
				ProcessSpoiler(vehicle, xdata.spoilerFrames, true);
			}

			if (iniLogModelRender && useLog)
			{
				lg << "Render Finished " << vehicle->m_nModelIndex << endl;
				lg.flush();
			}

			// Post reset flags
			xdata.flags.bDamageUpdated = false;
			xdata.flags.bUpgradesUpdated = false;
			bNewFrame = false;
		};

		///////////////////////////////////////////////////////////////////////////////////////

		// -- Flush log during unfocus (ie minimizing)
		Events::onPauseAllSounds += []
		{
			if (useLog) lg.flush();
		};

		/*
		// -- On game reload
		Events::reInitGameEvent += [] // It's also called first time the game is initialized
		{
			if (reInit)
			{
				if (useLog) lg << "Core: Reinitializing game...\n";
				if (useLog) lg.flush();
				// ...?
			}
			else reInit = true;
		};
		*/
    }

	///////////////////////////////////////////////////////////////////////////////////////////////




	///////////////////////////////////////////// Find Nodes

	static void FindNodesRecursive(RwFrame * frame, CVehicle * vehicle, bool bReSearch, bool bOnExtras)
	{
		while (frame) 
		{
			const string name = GetFrameNodeName(frame);
			size_t found;

			if (name[0] == 'f' && name[1] == '_') 
			{
				//if (useLog) lg << "Checking for " << name << endl;

				ExtendedData &xdata = xData.Get(vehicle);
				if (!bReSearch) 
				{
					// Don't process extras if seed is 0 (for Tuning Mod and other mods)
					if (xdata.randomSeed != 0)
					{
						// Set extras class
						found = name.find("f_class");
						if (found != string::npos) 
						{
							if (useLog) lg << "Extras: Found 'f_class' \n";

							ProcessClassesRecursive(frame, vehicle, bReSearch, false);

							if (RwFrame * tempFrame = frame->next) 
							{
								if (useLog) lg << "Extras: Jumping class nodes \n";
								frame = tempFrame;
								continue;
							}
						}
						
						// Recursive extras
						found = name.find("f_extras");
						if (found != string::npos) 
						{
							if (useLog) lg << "Extras: Found 'f_extras' \n";

							RwFrame * tempFrame = frame->child;
							if (tempFrame != nullptr) 
							{
								// Prepare for extra search
								srand((xdata.randomSeed + xdata.randomSeedUsage));
								xdata.randomSeedUsage++;

								list<string> &classList = getClassList();

								if (useLog) {
									lg << "Extras: Seed: " << xdata.randomSeed << endl;
									lg << "Extras: Classes: ";
									for (list<string>::iterator it = classList.begin(); it != classList.end(); ++it) {
										lg << *it << " ";
									}
									lg << "\nExtras: --- Starting extras for veh ID " << vehicle->m_nModelIndex << "\n";
								}

								ProcessExtraRecursive(frame, vehicle);
								if (useLog) lg << "Extras: --- Ending \n";
							}
							else if (useLog) lg << "Extras: (error) 'f_extras' has no childs \n";
						}
					}
				}

				// Digital speedo
				found = name.find("f_dspeedo");
				if (found != string::npos) 
				{
					if (useLog) lg << "DigitalSpeedo: Found 'f_dspeedo' \n";

					SetupDigitalSpeedo(vehicle, frame);
					xdata.speedoFrame = frame;
					FRAME_EXTENSION(frame)->owner = vehicle;

					float speedMult = 1.0;

					found = name.find("_mph");
					if (found != string::npos) 
					{
						speedMult *= 0.621371f;
					}

					found = name.find("_mu=");
					if (found != string::npos) 
					{
						float mult = stof(&name[found + 4]);
						speedMult *= mult;
					}

					if (useLog) lg << "DigitalSpeedo: Speed multiplicator: " << speedMult << "\n";

					xdata.speedoMult = speedMult;
				}

				// Digital odometer
				found = name.find("f_dodometer");
				if (found != string::npos)
				{
					if (useLog) lg << "DigitalOdometer: Found 'f_dodometer' \n";

					SetupDigitalOdometer(vehicle, frame);
					xdata.odometerFrame = frame;
					FRAME_EXTENSION(frame)->owner = vehicle;

					float speedMult = 1.0f;
					found = name.find("_mph");
					if (found != string::npos)
					{
						speedMult *= 0.621371f;
					}

					found = name.find("_prec");
					if (found != string::npos)
					{
						int precision = name[found + 5] - '0';
						if (precision == 0) speedMult *= 0.1f;
						//if (precision == 1) speedMult * 1.0f;
						if (precision == 2) speedMult *= 10.0f;
						if (precision == 3) speedMult *= 100.0f;
					}

					xdata.odometerMult = speedMult;

					found = name.find("_no0");
					if (found != string::npos)
					{
						xdata.odometerHideZero = true;
					}
					else xdata.odometerHideZero = false;
				}

				// Gear
				if (vehicle->m_nVehicleSubClass == VEHICLE_PLANE || vehicle->m_nVehicleSubClass == VEHICLE_HELI)
				{
					found = name.find("f_spin");
				}
				else
				{
					found = name.find("f_gear");
				}
				if (found != string::npos) 
				{
					if (useLog) {
						if (vehicle->m_nVehicleSubClass == VEHICLE_PLANE || vehicle->m_nVehicleSubClass == VEHICLE_HELI)
						{
							lg << "Gear: Found 'f_spin' \n";
						}
						else {
							lg << "Gear: Found 'f_gear' \n";
						}
					}
					xdata.gearFrame.push_back(frame);
					FRAME_EXTENSION(frame)->owner = vehicle;
				}

				// Fan
				found = name.find("f_fan");
				if (found != string::npos) 
				{
					if (useLog) lg << "Gear: Found 'f_fan' \n";
					xdata.fanFrame.push_back(frame);
					FRAME_EXTENSION(frame)->owner = vehicle;
				}

				// Shake
				found = name.find("f_shake");
				if (found != string::npos) 
				{
					if (useLog) lg << "Shake: Found 'f_shake' \n";
					if (CreateMatrixBackup(frame)) {
						xdata.shakeFrame.push_back(frame);
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

				// Gaspedal
				found = name.find("f_gas");
				if (found != string::npos) 
				{
					if (useLog) lg << "Pedal: Found 'f_gas' \n";
					if (CreateMatrixBackup(frame)) {
						xdata.gaspedalFrame.push_back(frame);
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

				// brakepedal
				found = name.find("f_brake");
				if (found != string::npos) 
				{
					if (useLog) lg << "Pedal: Found 'f_brake' \n";
					if (CreateMatrixBackup(frame)) {
						xdata.brakepedalFrame.push_back(frame);
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

				found = name.find("f_wiper");
				if (found != string::npos)
				{
					if (useLog) lg << "Wipers: Found 'f_wiper' \n";
					if (CreateMatrixBackup(frame))
					{
						F_an *an = new F_an(frame);
						an->mode = 1001;
						an->submode = 0;

						xdata.anims.push_back(an);
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

				// Animation by condition
				found = name.find("f_an"); //f_an1a=
				if (found != string::npos)
				{
					if (name[6] == '=')
					{
						int mode = stoi(&name[4]);
						int submode = (name[5] - 'a');

						switch (mode)
						{
						case 0:
							if (useLog) lg << "Anims: Found 'f_an" << mode << "': ping pong \n";
							break;
						case 1:
							switch (submode)
							{
							case 0:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": engine off \n";
								break;
							case 1:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": engine off or alarm on \n";
								break;
							default:
								if (useLog) lg << "Anims: Found 'f_an" << mode << " ERROR: submode not found \n";
								submode = -1;
								break;
							}
							break;
						case 2:
							switch (submode)
							{
							case 0:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": driver \n";
								break;
							case 1:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": passenger 1 \n";
								break;
							case 2:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": passenger 2 \n";
								break;
							case 3:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": passenger 3 \n";
								break;
							default:
								if (useLog) lg << "Anims: Found 'f_an" << mode << " ERROR: submode not found \n";
								submode = -1;
								break;
							}
							break;
						case 3:
							switch (submode)
							{
							case 0:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": high speed \n";
								break;
							case 1:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": high speed and f_spoiler \n";
								xdata.spoilerFrames.push_back(frame);
								break;
							default:
								if (useLog) lg << "Anims: Found 'f_an" << mode << " ERROR: submode not found \n";
								submode = -1;
								break;
							}
							break;
						case 4:
							switch (submode)
							{
							case 0:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": brake \n";
								break;
							case 1:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": high speed brake \n";
								break;
							case 2:
								if (useLog) lg << "Anims: Found 'f_an" << mode << "' " << submode << ": high speed brake and f_spoiler \n";
								xdata.spoilerFrames.push_back(frame);
								break;
							default:
								if (useLog) lg << "Anims: Found 'f_an" << mode << " ERROR: submode not found \n";
								submode = -1;
								break;
							}
							break;
						default:
							if (useLog) lg << "Anims: Found 'f_an' ERROR: mode not found \n";
							mode = -1;
							break;
						}

						if (mode >= 0 && submode >= 0)
						{
							if (CreateMatrixBackup(frame))
							{
								F_an *an = new F_an(frame);
								an->mode = mode;
								an->submode = submode;

								xdata.anims.push_back(an);
								FRAME_EXTENSION(frame)->owner = vehicle;
							}
						}
					}
				}

				//if (vehicle->m_nVehicleSubClass == VEHICLE_QUAD) // Quadbike on SetModelEvent is 0, idkw
				//{
					// tricycle fork
					found = name.find("f_trifork");
					if (found != string::npos)
					{
						if (useLog) lg << "Trifork: Found 'f_trifork' \n";
						if (CreateMatrixBackup(frame))
						{
							xdata.triforkFrame = frame;
							FRAME_EXTENSION(frame)->owner = vehicle;

							//if (!frame->child)
							//{
							RwFrame *wheelFrame = CClumpModelInfo::GetFrameFromId(vehicle->m_pRwClump, 5);
							RwFrameAddChild(frame->child, wheelFrame->child);
							//}
						}
					}
				//}

				if (vehicle->m_nVehicleSubClass == VEHICLE_BIKE || vehicle->m_nVehicleSubClass == VEHICLE_BMX || vehicle->m_nVehicleSubClass == VEHICLE_QUAD)
				{
					// footpeg driver
					found = name.find("f_fpeg1");
					if (found != string::npos) 
					{
						if (useLog) lg << "Footpegs: Found 'f_fpeg1' (footpeg driver) \n";
						if (CreateMatrixBackup(frame)) {
							xdata.fpegFront.push_back(new F_footpegs(frame));
							FRAME_EXTENSION(frame)->owner = vehicle;
						}
					}

					// footpeg passenger
					found = name.find("f_fpeg2");
					if (found != string::npos) 
					{
						if (useLog) lg << "Footpegs: Found 'f_fpeg2' (footpeg passenger) \n";
						if (CreateMatrixBackup(frame)) {
							xdata.fpegBack.push_back(new F_footpegs(frame));
							FRAME_EXTENSION(frame)->owner = vehicle;
						}
					}
				}

				// Hitch
				found = name.find("f_hitch"); 
				if (found != string::npos) 
				{
					if (useLog) lg << "FunctionalHitch: Found 'f_hitch' \n";
					xdata.hitchFrame = frame;
					FRAME_EXTENSION(frame)->owner = vehicle;
				}

				// Cop light
				found = name.find("f_coplight");
				if (found != string::npos) 
				{
					if (useLog) lg << "CopLight: Found 'f_coplight' \n";
					xdata.coplightFrame = frame;
					FRAME_EXTENSION(frame)->owner = vehicle;
					vehicle->m_vehicleAudio.m_bModelWithSiren = true;
					if (RwFrame *child = frame->child) 
					{
						const string childname = GetFrameNodeName(child);
						found = childname.find("outpoint");
						if (found != string::npos) 
						{
							if (useLog) lg << "CopLight: Found 'outpoint' \n";
							xdata.coplightoutFrame = child;
						}
					}
				}

				// Taxi light
				found = name.find("f_taxilight");
				if (found != string::npos)
				{
					if (useLog) lg << "TaxiLight: Found 'f_taxilight' \n";
					xdata.taxilightFrame = frame;
					FRAME_EXTENSION(frame)->owner = vehicle;
				}

				// Wheel
				if (!bReSearch) {
					found = name.find("f_wheel");
					if (found != string::npos) 
					{
						if (useLog) lg << "Wheel: Found 'f_wheel' at " << name << " \n";
						for (int i = 0; i < 6; i++) 
						{
							if (!xdata.wheelFrame[i]) 
							{
								xdata.wheelFrame[i] = frame;
								FRAME_EXTENSION(frame)->owner = vehicle;
								break;
							}
						}
					}
				}

				// Popup lights
				found = name.find("f_pop");
				if (found != string::npos)
				{
					if (CreateMatrixBackup(frame)) {
						if (name[found + 5] == 'l') {
							if (useLog) lg << "Popup lights: Found 'f_popl' \n";
							xdata.popupFrame[0] = frame;
						}
						else
						{
							if (useLog) lg << "Popup lights: Found 'f_popr' \n";
							xdata.popupFrame[1] = frame;
						}
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

				// Spoiler
				found = name.find("f_spoiler");
				if (found != string::npos)
				{
					if (useLog) lg << "Spoiler: Found 'f_spoiler' \n";
					xdata.spoilerFrames.push_back(frame);
					FRAME_EXTENSION(frame)->owner = vehicle;
				}

				// Steer
				found = name.find("f_steer");
				if (found != string::npos)
				{
					if (useLog) lg << "Steer: Found 'f_steer' \n";
					if (CreateMatrixBackup(frame)) {
						xdata.steer.push_back(frame);
						FRAME_EXTENSION(frame)->owner = vehicle;
					}
				}

			} // end of "f_"
			else // retrocompatibility
			{
				if (!IVFinstalled) {
					ExtendedData &xdata = xData.Get(vehicle);
					found = name.find("movsteer");
					if (found != string::npos)
					{
						found = name.find("movsteer");
						if (found != string::npos)
						{
							if (name[8] == '_') {
								if (isdigit(name[9])) {
									if (useLog) lg << "Retrocompatibility: Found 'movsteer_*' \n";
									if (CreateMatrixBackup(frame)) {
										xdata.steer.push_back(frame);
										FRAME_EXTENSION(frame)->owner = vehicle;
									}
								}
							}
							else {
								if (useLog) lg << "Retrocompatibility: Found 'movsteer' \n";
								if (CreateMatrixBackup(frame)) {
									xdata.steer.push_back(frame);
									FRAME_EXTENSION(frame)->owner = vehicle;
								}
							}
						}
					}
				}
				if (!APPinstalled) {
					ExtendedData &xdata = xData.Get(vehicle);
					found = name.find("steering_dummy");
					if (found != string::npos)
					{
						if (frame->child) {
							if (useLog) lg << "Retrocompatibility: Found 'steering' \n";
							if (CreateMatrixBackup(frame->child)) {
								xdata.steer.push_back(frame->child);
								FRAME_EXTENSION(frame->child)->owner = vehicle;
							}
						}
					}

					found = name.find("dvornik_dummy");
					if (found != string::npos)
					{
						if (useLog) lg << "Retrocompatibility: Found 'dvornik_dummy' \n";

						if (frame->child)
						{
							RwFrame *tempFrame = frame->child;
							do
							{
								const string tempName = GetFrameNodeName(tempFrame);
								string vfName;

								found = tempName.find("right");
								if (found != string::npos)
								{
									vfName = "f_wiper=ay60";
								}
								else {
									vfName = "f_wiper=ay-60";
								}

								if (useLog) lg << "Retrocompatibility: " << tempName << " changed to " << vfName << endl;

								if (CreateMatrixBackup(tempFrame))
								{
									SetFrameNodeName(tempFrame, &vfName[0]);
									F_an *an = new F_an(tempFrame);
									an->mode = 1001;
									an->submode = 0;
									xdata.anims.push_back(an);
									FRAME_EXTENSION(tempFrame)->owner = vehicle;
								}

								tempFrame = tempFrame->next;
							}
							while (tempFrame);
						}
					}
					
				}
				if (noChassis) {
					if (name[0] == 'b' && name[1] == 'o' && name[2] == 'd' && name[3] == 'y')
					{
						reinterpret_cast<CAutomobile*>(vehicle)->m_aCarNodes[CAR_CHASSIS] = frame;
						reinterpret_cast<CAutomobile*>(vehicle)->m_swingingChassis.m_nDoorState = eDoorState::DOOR_NOTHING;
						CVisibilityPlugins::SetFrameHierarchyId(frame, 1);
						// some peoples uses 'body' instead of 'chassis' to disable it without changing the handling flag
						vehicle->m_pHandlingData->m_nHandlingFlags.m_bSwingingChassis = false;
						vehicle->m_nHandlingFlags.bSwingingChassis = false;
						if (useLog) lg << "Error fixed: Using '" << name << "' as chassis for vehicle id " << vehicle->m_nModelIndex << endl;
						noChassis = false;
					}
				}
			}

			// Characteristics
			FindVehicleCharacteristicsFromNode(frame, vehicle, bReSearch);

			/////////////////////////////////////////
			if (RwFrame * newFrame = frame->child)  FindNodesRecursive(newFrame, vehicle, bReSearch, bOnExtras);
			if (RwFrame * newFrame = frame->next)   FindNodesRecursive(newFrame, vehicle, bReSearch, bOnExtras);
			return;
		}
		return;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////

} vehfuncs;


void LogLastVehicleRendered()
{
	if (lastRenderedVehicleModel > 0 && useLog)
	{
		lg << "Last rendered vehicle model ID is '" << lastRenderedVehicleModel << "'. Note: not always the last rendered vehicle is the crash reason, this information may be useless." << endl;
		lg.flush();
	}
}

void LogCrashText(string str)
{
	if (!ignoreCrashInfo)
	{
		if (useLog)
		{
			lg << str;
			lg.flush();
		}
		if (MessageBoxA(0, str.c_str(), "VehFuncs", MB_OKCANCEL) == IDCANCEL) ignoreCrashInfo = true;
	}
}

void LogVehicleModelWithText(string str1, int vehicleModel, string str2)
{
	if (!ignoreCrashInfo)
	{
		string crashInfo = str1 + str2;
		if (useLog)
		{
			lg << crashInfo;
			lg.flush();
		}
		if (MessageBoxA(0, crashInfo.c_str(), "VehFuncs", MB_OKCANCEL) == IDCANCEL) ignoreCrashInfo = true;
	}
}

RwFrame *__cdecl CustomRwFrameForAllChildren_AddUpgrade(RwFrame *frame, RwFrame *(__cdecl *callback)(RwFrame *, void *), void *data)
{
	if (RwFrame * newFrame = frame->child)  CustomRwFrameForAllChildren_AddUpgrade_Recurse(newFrame, callback, data);
	return frame;
}

RwFrame *__cdecl CustomRwFrameForAllChildren_AddUpgrade_Recurse(RwFrame *frame, RwFrame *(__cdecl *callback)(RwFrame *, void *), void *data)
{
	if (!FRAME_EXTENSION(frame)->flags.bDestroyOnRemoveUpgrade) FRAME_EXTENSION(frame)->flags.bNeverRender = true;

	if (RwFrame * newFrame = frame->child)  CustomRwFrameForAllChildren_AddUpgrade_Recurse(newFrame, callback, data);
	if (RwFrame * newFrame = frame->next)   CustomRwFrameForAllChildren_AddUpgrade_Recurse(newFrame, callback, data);
	return frame;
}

RwFrame *__cdecl CustomRwFrameForAllChildren_RemoveUpgrade(RwFrame *frame, RwFrame *(__cdecl *callback)(RwFrame *, void *), void *data)
{
	// Similar to RwFrameForAllChildren
	RwFrameForAllChildren(frame, (RwFrameCallBack)CustomRwFrameForAllChildren_RemoveUpgrade_Recurse, data);
	return frame;
}

RwFrame *__cdecl CustomRwFrameForAllChildren_RemoveUpgrade_Recurse(RwFrame *frame, RwFrame *(__cdecl *callback)(RwFrame *, void *), void *data)
{
	if (FRAME_EXTENSION(frame)->flags.bDestroyOnRemoveUpgrade) {
		RwFrameForAllObjects(frame, RemoveObjectsCB, data);
	}
	else {
		FRAME_EXTENSION(frame)->flags.bNeverRender = false;
	}

	RwFrameForAllChildren(frame, (RwFrameCallBack)CustomRwFrameForAllChildren_RemoveUpgrade_Recurse, data);
	return frame;
}




RwFrame *__cdecl CustomRwFrameForAllObjects_Upgrades(RwFrame *frame, RpAtomicCallBack callback, void *data)
{
	if (!rwLinkListEmpty(&frame->objectList))
	{
		RwObjectHasFrame * atomic;

		RwLLLink * current = rwLinkListGetFirstLLLink(&frame->objectList);
		RwLLLink * end = rwLinkListGetTerminator(&frame->objectList);

		current = rwLinkListGetFirstLLLink(&frame->objectList);
		while (current != end) {
			atomic = rwLLLinkGetData(current, RwObjectHasFrame, lFrame);

			if (!callback((RpAtomic *)atomic, data)) break;

			current = rwLLLinkGetNext(current);
		}
	}
	return frame;
}

RwFrame *__cdecl CustomCollapseFramesCB(RwFrame *frame, void *data)
{
	RwFrameForAllChildren(frame, (RwFrameCallBack)CustomCollapseFramesCB, data);
	RwFrameForAllObjects(frame, (RwObjectCallBack)CustomMoveObjectsCB, data);

	/*const string parentName = GetFrameNodeName(RwFrameGetParent(frame));
	found = parentName.find("wheel_");
	if (found != string::npos)
	{
		found = parentName.find("_dummy");
		{
			RwFrameDestroy(frame);
		}
	}*/

	return frame;
}

RpAtomic *__cdecl CustomMoveObjectsCB(RpAtomic *atomic, RwFrame *frame)
{
	RwFrame * frameAtomic = GetObjectParent((RwObject*)atomic);
	RwFrame * frameAtomicParent = RwFrameGetParent(frameAtomic);

	int frameId = CVisibilityPlugins::GetFrameHierarchyId(frameAtomicParent);

	// If parent is dummy, always collapse
	if (frameId > 0)
	{
		const string frameAtomicName = GetFrameNodeName(frameAtomic);
		if (frameAtomicName[0] == 'e')
		{
			if (frameAtomicName[1] == 'x' && frameAtomicName[2] == 't' && frameAtomicName[3] == 'r' && frameAtomicName[4] == 'a') 
			{
				return atomic;
			}
		}
		else
		{
			if (frameAtomicName[0] == 'f' && frameAtomicName[1] == '_')
			{
				return atomic;
			}
		}
		RpAtomicSetFrame(atomic, frame);
		return atomic;
	}


	// If parent isn't dummy, collapse if is alpha and damageable
	if ((uint32_t)atomic->renderCallBack == AtomicAlphaCallBack) {
		RwFrame * parent = RwFrameGetParent(frameAtomic);

		while (parent != frame) {
			const string parentName = GetFrameNodeName(parent);
			if (parentName[0] == 'f') 
			{
				if (parentName[1] == '_') 
				{
					return atomic;
				}
			}
			parent = RwFrameGetParent(parent);
			if (!parent) break;
		} 

		const string frameAtomicName = GetFrameNodeName(frameAtomic);
		int len = frameAtomicName.length();

		if (len >= 4) 
		{
			if (frameAtomicName[0] == 'e')
			{
				if (frameAtomicName[1] == 'x' && frameAtomicName[2] == 't' && frameAtomicName[3] == 'r' && frameAtomicName[4] == 'a')
				{
					return atomic;
				}
			}
			else 
			{
				if (frameAtomicName[0] == 'f' && frameAtomicName[1] == '_')
				{
					return atomic;
				}
			}

			if (frameAtomicName[(len - 3)] == '_'
				&& frameAtomicName[(len - 2)] == 'o'
				&& frameAtomicName[(len - 1)] == 'k') 
			{
				RpAtomicSetFrame(atomic, frame);
				return atomic;
			}

			if (frameAtomicName[(len - 4)] == '_'
				&& frameAtomicName[(len - 3)] == 'd'
				&& frameAtomicName[(len - 2)] == 'a'
				&& frameAtomicName[(len - 1)] == 'm') 
			{
				RpAtomicSetFrame(atomic, frame);
				return atomic;
			}
		}
	}

	return atomic;
}
