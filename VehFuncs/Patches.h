#include "plugin.h"
#include "../injector/assembly.hpp"

namespace Patches {
	extern uint32_t defaultCopLightSwitch;
	void CustomAssignRemapTxd(const char *txdName, uint16_t txdId);
	void PatchForCoplights();
	void __declspec() NeverRender();
	void __declspec() UseCopLights();
	void __declspec() RenderBus();
	void __declspec() IsLawEnforcement();
	void __cdecl RenderBusCheck(CVehicle *veh);
	void __cdecl IsLawEnforcementCheck(CVehicle *veh);
	void __declspec() ForceRenderCustomLOD();
	void __declspec() ForceRenderCustomLODAlpha();
	void __declspec() ForceRenderCustomLODBoat();
	void __declspec() ForceRenderCustomLODBoatAlpha();
	void __declspec() ForceRenderCustomLODBig();
	void __declspec() ForceRenderCustomLODBigAlpha();
	void __declspec() ForceRenderCustomLODTrain();
	void __declspec() ForceRenderCustomLODTrainAlpha();
	void PatchForAdditionalVehicleTxd();
	namespace Hitch {
		typedef bool(__thiscall *GetTowBarPos_t)(CAutomobile *ths, RwV3d *point, char a3, CAutomobile *a4);
		extern void setOriginalFun(GetTowBarPos_t f);
		extern void setOriginalFun_Trailer(GetTowBarPos_t f);
		void __declspec() GetTowBarPosToHook();
	}
	namespace FindDamage {
		RwFrame *__cdecl CustomFindDamageAtomics(RwFrame *frame, RwFrame *(__cdecl *callback)(RwFrame *, void *), void *data);
		RwFrame *__cdecl CustomFindDamageAtomicsCB(RwFrame *frame, void *data);
	}
}