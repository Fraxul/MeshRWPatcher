#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Psapi.h>
#include <detours.h>
#include <stdio.h>
#include <malloc.h>
#include <stdint.h>


static void debugPrintf(const char* fmt, ...) {
    va_list v;
    va_start(v, fmt);
    int count = vsnprintf(NULL, 0, fmt, v);
    if (count >= 0) {
        char* buf = (char*) malloc(count + 1);
        vsnprintf(buf, count + 1, fmt, v);
        OutputDebugStringA(buf);
        free(buf);
    }
    va_end(v);
}

static const uint8_t* memmem(const uint8_t* big, size_t big_length, const uint8_t* little, size_t little_length) {
    if (little_length > big_length)
        return NULL;

    const uint8_t* bigmax = big + (big_length - little_length);
    for (const uint8_t* bp = big; bp != bigmax; ++bp) {
        if (memcmp(bp, little, little_length) == 0)
            return bp;
    }

    return NULL;
}

/*
 * Typical call-stack during asset bundle build:
 * 
 * 	Unity.exe!PrepareMeshDataForBuildTarget::PrepareMeshDataForBuildTarget(class Mesh &,struct BuildTargetSelection,int,int,enum ShaderChannelMask,enum MeshUsageFlags,struct BuildUsageTagGlobal)
 * 	Unity.exe!Mesh::Transfer<class StreamedBinaryWrite>(class StreamedBinaryWrite &)
 * 	Unity.exe!SerializedFile::WriteObject(class Object &,__int64,short,struct BuildUsageTag const &,struct GlobalBuildData const &)
 * 	Unity.exe!PersistentManager::WriteFile(class core::basic_string_ref<char>,int,struct WriteData const *,int,struct GlobalBuildData const &,enum VerifyWriteObjectResult (*)(class Object *,enum BuildTargetPlatform),struct BuildTargetSelection,enum TransferInstructionFlags,struct WriteInformation &,struct InstanceIDResolver const *,enum PersistentManager::LockFlags,int (*)(enum PersistentManager::ReportWriteObjectStep,int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,int,void *),void *)
 * 	Unity.exe!PersistentManager::WriteFile(class core::basic_string_ref<char>,int,struct WriteData const *,int,struct GlobalBuildData const &,enum VerifyWriteObjectResult (*)(class Object *,enum BuildTargetPlatform),struct BuildTargetSelection,enum TransferInstructionFlags,struct InstanceIDResolver const *,enum PersistentManager::LockFlags,int (*)(enum PersistentManager::ReportWriteObjectStep,int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,int,void *),void *)
 * 	Unity.exe!WriteSharedAssetFile(int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,class std::map<int,struct BuildAsset,struct std::less<int>,class std::allocator<struct std::pair<int const ,struct BuildAsset> > > const &,struct BuildUsageTagGlobal const &,struct BuildTargetSelection,void (*)(int,struct LocalSerializedObjectIdentifier &,void *),enum TransferInstructionFlags,class std::set<struct ResourceFile,struct std::less<struct ResourceFile>,class std::allocator<struct ResourceFile> > &,class vector_map<int,struct SerializedObjectIdentifier,struct std::less<int>,class std::allocator<struct std::pair<int,struct SerializedObjectIdentifier> > > &,class BuildReporting::BuildReport *)
 * 	Unity.exe!CompileSharedAssetsFile(int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,class core::basic_string<char,class core::StringStorageDefault<char> > const &,class std::map<int,struct BuildAsset,struct std::less<int>,class std::allocator<struct std::pair<int const ,struct BuildAsset> > > const &,struct BuildUsageTagGlobal const &,struct BuildTargetSelection const &,enum TransferInstructionFlags,class std::set<struct ResourceFile,struct std::less<struct ResourceFile>,class std::allocator<struct ResourceFile> > &,class vector_map<int,struct SerializedObjectIdentifier,struct std::less<int>,class std::allocator<struct std::pair<int,struct SerializedObjectIdentifier> > > &,class BuildReporting::BuildReport &)
 * 	Unity.exe!BuildPlayerData()
 * 	Unity.exe!DoBuildPlayer_Build()
 * 	Unity.exe!DoBuildPlayer(struct BuildPlayerSetup const &,struct dynamic_array<struct EditorSceneBackup,0> &,class core::basic_string<char,class core::StringStorageDefault<char> >,struct dynamic_array<class core::basic_string<char,class core::StringStorageDefault<char> >,0> const &,bool,class BuildReporting::BuildReport &,struct BuildReporting::BuiltAssetBundleInfo *,class core::basic_string<char,class core::StringStorageDefault<char> > const &)
 * 	Unity.exe!BuildStreamedSceneBundle()
 * 	Unity.exe!BuildAssetBundlesInternal()
 * 	Unity.exe!BuildAssetBundles(class core::basic_string<char,class core::StringStorageDefault<char> > const &,struct dynamic_array<struct AssetBundleBuildInfo,0> const &,enum BuildTargetPlatformGroup,enum BuildTargetPlatform,int,enum BuildAssetBundleOptions,bool,class BuildReporting::BuildReport &)
 * 	Unity.exe!BuildPipeline_CUSTOM_BuildAssetBundlesWithInfoInternal(class ScriptingBackendNativeStringPtrOpaque *,class ScriptingBackendNativeArrayPtrOpaque *,enum BuildAssetBundleOptions,enum BuildTargetPlatformGroup,enum BuildTargetPlatform,int)
 *
 */


/*
 * Callstack for saving meshes inside of a scene file during the build:
 * (we avoid modifying meshes saved like this by adding a guard around BackupScenes)
 *
 *	Unity.exe!Mesh::Transfer<class StreamedBinaryWrite>(class StreamedBinaryWrite &)
 * 	Unity.exe!SerializedFile::WriteObject(class Object &,__int64,short,struct BuildUsageTag const &,struct GlobalBuildData const &)
 *	Unity.exe!PersistentManager::WriteFile(class core::basic_string_ref<char>,int,struct WriteData const *,int,struct GlobalBuildData const &,enum VerifyWriteObjectResult (*)(class Object *,enum BuildTargetPlatform),struct BuildTargetSelection,enum TransferInstructionFlags,struct WriteInformation &,struct InstanceIDResolver const *,enum PersistentManager::LockFlags,int (*)(enum PersistentManager::ReportWriteObjectStep,int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,int,void *),void *)
 *	Unity.exe!PersistentManager::WriteFile(class core::basic_string_ref<char>,int,struct WriteData const *,int,struct GlobalBuildData const &,enum VerifyWriteObjectResult (*)(class Object *,enum BuildTargetPlatform),struct BuildTargetSelection,enum TransferInstructionFlags,struct InstanceIDResolver const *,enum PersistentManager::LockFlags,int (*)(enum PersistentManager::ReportWriteObjectStep,int,class core::basic_string<char,class core::StringStorageDefault<char> > const &,int,void *),void *)
 *	Unity.exe!SaveScene(class UnityScene const &,class core::basic_string<char,class core::StringStorageDefault<char> > const &,class core::hash_map<__int64,int,struct core::hash<__int64>,struct std::equal_to<__int64> > *,enum TransferInstructionFlags)
 *	Unity.exe!EditorSceneManager::SerializeSceneIntoFile(class UnityScene &,class core::basic_string<char,class core::StringStorageDefault<char> > const &,enum TransferInstructionFlags,class core::hash_map<__int64,int,struct core::hash<__int64>,struct std::equal_to<__int64> > *)
 *	Unity.exe!EditorSceneManager::BackupScenesFromSceneManager(bool,struct dynamic_array<struct EditorSceneBackup,0> &)
 *  Unity.exe!BackupScenes(struct dynamic_array<struct EditorSceneBackup,0> & __ptr64) XXX this call goes here, but it gets tail-call optimized out so it doesn't appear in the backtrace.
 *	Unity.exe!BuildAssetBundlesInternal()
 *	Unity.exe!BuildAssetBundles(class core::basic_string<char,class core::StringStorageDefault<char> > const &,struct dynamic_array<struct AssetBundleBuildInfo,0> const &,enum BuildTargetPlatformGroup,enum BuildTargetPlatform,int,enum BuildAssetBundleOptions,bool,class BuildReporting::BuildReport &)
 *	Unity.exe!BuildPipeline_CUSTOM_BuildAssetBundlesWithInfoInternal(class ScriptingBackendNativeStringPtrOpaque *,class ScriptingBackendNativeArrayPtrOpaque *,enum BuildAssetBundleOptions,enum BuildTargetPlatformGroup,enum BuildTargetPlatform,int)
*/


// All signatures are for Unity 2022.3.6f1
// Unity.exe!Mesh::Transfer<class StreamedBinaryWrite>(class StreamedBinaryWrite &)
static void (__cdecl *original_Mesh_Transfer__class_StreamedBinaryWriter)(void* this_Mesh, void* streamedBinaryWriter);
const uint8_t Mesh_Transfer_signature[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x38, 0xfd, 0xff, 0xff, 0x48, 0x81, 0xec, 0xc8, 0x03, 0x00, 0x00, 0x48, 0x8b, 0xf2, 0x4c, 0x8b, 0xf1 };

// Unity.exe!IsBuildingPlayer()
const uint8_t IsBuildingPlayer_signature[] = { 0x0f, 0xb6, 0x05, 0xb1, 0x08, 0xd1, 0x01, 0xc3 };
static bool(__cdecl* IsBuildingPlayer)(void);

// void __cdecl BackupScenes(struct dynamic_array<struct EditorSceneBackup,0> & __ptr64)
const uint8_t BackupScenes_signature[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x82, 0x59, 0x8a, 0xff, 0xe8, 0x3d, 0x38, 0x45, 0xff };
static void (__cdecl *original_BackupScenes)(void* param1);
static bool s_inBackupScenes = false;



void __cdecl replacement_Mesh_Transfer__class_StreamedBinaryWriter(void* this_Mesh, void* streamedBinaryWriter) {
    if (!IsBuildingPlayer() || s_inBackupScenes) {
        // Don't do anything if we're not building the player right now, or if we're saving a scene through BackupScenes()
        (*original_Mesh_Transfer__class_StreamedBinaryWriter)(this_Mesh, streamedBinaryWriter);
    }

    //debugPrintf("MeshRWPatcher: replacement Mesh::Transfer: this=%p streamedBinaryWriter=%p", this_Mesh, streamedBinaryWriter);
    uint8_t* pIsReadable = reinterpret_cast<uint8_t*>(this_Mesh) + 0x161; // m_IsReadable offset in Mesh class
    uint8_t old_IsReadable = *pIsReadable;
    *pIsReadable = 0;
    (*original_Mesh_Transfer__class_StreamedBinaryWriter)(this_Mesh, streamedBinaryWriter);
    *pIsReadable = old_IsReadable;
}

void __cdecl replacement_BackupScenes(void* param1) {
    s_inBackupScenes = true;
    original_BackupScenes(param1);
    s_inBackupScenes = false;
}



BOOL APIENTRY DllMain( HMODULE hModule, DWORD  dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        OutputDebugString(L"MeshRWPatcher: DLL_PROCESS_ATTACH");
        DetourRestoreAfterWith();

        HMODULE unityModule = NULL;
        if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, NULL, &unityModule)) {
            OutputDebugString(L"MeshRWPatcher: Can't get the Unity HMODULE");
            return TRUE;
        }

        MODULEINFO moduleInfo;
        GetModuleInformation(GetCurrentProcess(), unityModule,&moduleInfo, sizeof(moduleInfo));
        debugPrintf("MeshRWPatcher: Unity module is at 0x%p, 0x%lu bytes", moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage);

        {
            const uint8_t* fn_base = memmem((const uint8_t*) moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage, Mesh_Transfer_signature, sizeof(Mesh_Transfer_signature));

            if (!fn_base) {
                OutputDebugString(L"MeshRWPatcher: Can't find the Mesh::Transfer function!");
                return TRUE;
            }

            debugPrintf("MeshRWPatcher: Mesh::Transfer function is at 0x%p", fn_base);
            original_Mesh_Transfer__class_StreamedBinaryWriter = reinterpret_cast<void(__cdecl*)(void*, void*)>(fn_base);
        }

        {
            const uint8_t* fn_base = memmem((const uint8_t*) moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage, IsBuildingPlayer_signature, sizeof(IsBuildingPlayer_signature));
            if (!fn_base) {
                OutputDebugString(L"MeshRWPatcher: Can't find the IsBuildingPlayer function!");
                return TRUE;
            }
            debugPrintf("MeshRWPatcher: IsBuildingPlayer() function is at 0x%p", fn_base);
            IsBuildingPlayer = reinterpret_cast<bool(__cdecl*)()>(fn_base);
        }

        {
            const uint8_t* fn_base = memmem((const uint8_t*) moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage, BackupScenes_signature, sizeof(BackupScenes_signature));
            if (!fn_base) {
                OutputDebugString(L"MeshRWPatcher: Can't find the BackupScenes function!");
                return TRUE;
            }
            debugPrintf("MeshRWPatcher: BackupScenes() function is at 0x%p", fn_base);
            original_BackupScenes = reinterpret_cast<void(__cdecl*)(void*)>(fn_base);
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) original_Mesh_Transfer__class_StreamedBinaryWriter, replacement_Mesh_Transfer__class_StreamedBinaryWriter);
        DetourAttach(&(PVOID&) original_BackupScenes, replacement_BackupScenes);
        DetourTransactionCommit();
        
    } else if (dwReason == DLL_PROCESS_DETACH) {
        OutputDebugString(L"MeshRWPatcher: DLL_PROCESS_DETACH");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) original_Mesh_Transfer__class_StreamedBinaryWriter, replacement_Mesh_Transfer__class_StreamedBinaryWriter);
        DetourDetach(&(PVOID&) original_BackupScenes, replacement_BackupScenes);
        DetourTransactionCommit();
    }

    return TRUE;
}
