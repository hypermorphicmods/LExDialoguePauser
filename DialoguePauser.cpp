#include <unordered_map>
#include "DialoguePauser.h"
#include "SubtitleHistory.h"

#define VERSION L"2.0.1"

#if defined GAMELE1
SPI_PLUGINSIDE_SUPPORT(L"LE1DialoguePauser", VERSION, L"ME3Tweaks", SPI_GAME_LE1, SPI_VERSION_ANY);
#define MYHOOK "LE1DialoguePauser_"
#define LOGNAME "LE1DialoguePauser.log"
#define ENGINENAME "GameEngine_0"
#define EXENAME L"MassEffect1.exe"
#elif defined GAMELE2
SPI_PLUGINSIDE_SUPPORT(L"LE2DialoguePauser", VERSION, L"ME3Tweaks", SPI_GAME_LE2, SPI_VERSION_ANY);
#define MYHOOK "LE2DialoguePauser_"
#define LOGNAME "LE2DialoguePauser.log"
#define ENGINENAME "SFXEngine_0"
#define EXENAME L"MassEffect2.exe"
#elif defined GAMELE3
SPI_PLUGINSIDE_SUPPORT(L"LE3DialoguePauser", VERSION, L"ME3Tweaks", SPI_GAME_LE3, SPI_VERSION_ANY);
#define MYHOOK "LE3DialoguePauser_"
#define LOGNAME "LE3DialoguePauser.log"
#define ENGINENAME "SFXEngine_0"
#define EXENAME L"MassEffect3.exe"
#else
#error Select a game configuration
#endif

SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;


enum class ConvState
{
    Init,
    WaitForConversation,
    WaitForNode,
    WaitForVoiceover,
    ActiveVoiceover,
    Paused
};

enum class PauseState
{
    Unpaused,
    Dialogue,
    Manual
};

ConvState convstate = ConvState::Init;
PauseState pausestate = PauseState::Unpaused;


// Hooked functions
#if defined GAMELE1
FunctionName<UConsole>* UXConsole_InputKey;
#else
FunctionName<USFXConsole>* UXConsole_InputKey;
#endif
FunctionName<USFXSaveGame> *USFXSaveGame_LoadPlayer;
FunctionName<UBioSeqAct_StartConversation> *UBioSeqAct_StartConversation_Activated, *UBioSeqAct_StartConversation_Deactivated;
FunctionName<UBioSeqEvt_ConvNode> *UBioSeqEvt_ConvNode_Activated;
FunctionName<ABioHUD> *ABioHUD_PostRender;
FunctionName<ASFXPlayerCamera> *SFXPlayerCamera_UpdateCamera;
FunctionName<USeqAct_Interp> *USeqAct_Interp_Activated, *USeqAct_Interp_Deactivated;
FunctionName<USFXGameViewportClient> *SFXGameViewportClient_GameSessionEnded;


UMassEffectGuiManager* guiManager = nullptr;
UGameUISceneClient* scene = nullptr;
ABioWorldInfo* bioWorldInfo = nullptr;
int currentbc = 0;
unordered_map<int, UBioConversation*> bcs = {};
UBioSeqEvt_ConvNode* node = nullptr;
USeqAct_Interp* interp = nullptr;
SubtitleHistory* subhistory = new SubtitleHistory();
bool manualPauseHistory = true, convPauseHistory = false;


UGameEngine* gameEngine = nullptr;
void DrawHUDString(UCanvas* canvas, WCHAR* hudstring, float scaleX = 1.0, float scaleY = 1.0)
{
    if (!gameEngine)
    {
        // there is no Find function for instanced name ??
        for (int i = 0; i < UObject::GObjObjects()->Count; ++i)
        {
            UObject* Object = UObject::GObjObjects()->Data[i];

            // skip no T class objects 
            if (
                !Object
                || !Object->IsA(UGameEngine::StaticClass())
                )
                continue;

            if (!strcmp(ENGINENAME, Object->GetInstancedName()))
            {
                gameEngine = (UGameEngine*)Object;
                LOGFORMAT("GameEngine found");
                break;
            }
        }

        if (!gameEngine)
            return;
    }

    UFont *savefont = canvas->Font;
    canvas->Font = gameEngine->MediumFont;
    canvas->SetDrawColor(0, 0, 0, 255);
    canvas->SetPos(11, 11);
    canvas->DrawTextW(hudstring, wcslen(hudstring), scaleX * canvas->SizeX / 1920.0, scaleY * canvas->SizeX / 1920.0, nullptr);
    canvas->SetDrawColor(245, 245, 245, 255);
    canvas->SetPos(10, 10);
    canvas->DrawTextW(hudstring, wcslen(hudstring), scaleX * canvas->SizeX / 1920.0, scaleY * canvas->SizeX / 1920.0, nullptr);
    canvas->Font = savefont;
}

void ReplaceDisplayedSubtitle()
{
    subhistory->ShowSubtitle(bioWorldInfo);
#if defined GAMELE1
    // Conversation panel is hidden after the conversation ends
    guiManager->eventShowConversationGui(bioWorldInfo->m_oCurrentConversation ? bioWorldInfo->m_oCurrentConversation->IsAmbient() : 1);
    // Update display
    if (auto convhandler = (UBioSFHandler_Conversation*)guiManager->m_oConversationPanel->m_DefaultHandler; convhandler)
        convhandler->Update(0.001);
#else
    // update display
    bioWorldInfo->Tick(0.001);
#endif
    subhistory->RestoreSubtitle(bioWorldInfo);
}

void ResetDisplayedSubitle()
{
#if defined GAMELE1
    if (auto convhandler = (UBioSFHandler_Conversation*)guiManager->m_oConversationPanel->m_DefaultHandler; convhandler)
        convhandler->Update(0.001);
#else
    bioWorldInfo->Tick(0.001);
#endif
}

bool isvalid_bc(UBioConversation* bc, int resid = 0)
{
    // try to detect a bad pointer
    try
    {
        if (resid > 0 && bc->m_nResRefID != resid)
            return false;

        if (bc->m_EntryList.Count > 0 && bc->m_nResRefID > 0)
            return true;
    }
    catch (...)
    {
        LOGFORMAT("Exception caught testing bc");
        return false;
    }
}


// ProcessEvent hook
// ======================================================================

typedef void (*tProcessEvent)(UObject* Context, UFunction* Function, void* Parms, void* Result);
tProcessEvent ProcessEvent = nullptr;
tProcessEvent ProcessEvent_orig = nullptr;


bool stoprecursion = false, logcall = false, stopeverything = false;
void ProcessEvent_hook(UObject* Context, UFunction* Function, void* Parms, void* Result)
{
#ifdef ASI_DEBUG
    if (logcall)
    {
        // trace the result of function calls
        if (auto thisnode = UBioSeqEvt_ConvNode_Activated->Matches(Context, Function); thisnode)
            LOGFORMAT("Event %s - %s - conv %d node %d ", Context->Class->GetName(), Function->GetInstancedName(), thisnode->m_nConvResRefID, thisnode->m_nNodeID)
        else if (auto bc = UBioSeqAct_StartConversation_Activated->Matches(Context, Function); bc)
            LOGFORMAT("Event %s - %s - conv %d", Context->Class->GetName(), Function->GetInstancedName(), bc->Conv->m_nResRefID)
        else if (auto bc = UBioSeqAct_StartConversation_Deactivated->Matches(Context, Function); bc)
            LOGFORMAT("Event %s - %s - conv %d", Context->Class->GetName(), Function->GetInstancedName(), bc->Conv->m_nResRefID)
        else
            LOGFORMAT("Event %s - %s", Context->Class->GetName(), Function->GetInstancedName())
    }
#endif

    if (stoprecursion || stopeverything)
    {
        // don't hook our own function calls
        return ProcessEvent_orig(Context, Function, Parms, Result);
    }
    stoprecursion = true;

    if (!scene && Context->Class == UGameUISceneClient::StaticClass())
    {
        scene = reinterpret_cast<UGameUISceneClient*>(Context);
        convstate = ConvState::WaitForNode;
        LOGFORMAT("Scene found");
    }

    if (!bioWorldInfo && 
        Context->Class == UMassEffectGuiManager::StaticClass() &&
        !strcmp("BioWorldInfo_0", ((UMassEffectGuiManager*)Context)->oBioWorldInfo->GetInstancedName()))
    {
        guiManager = reinterpret_cast<UMassEffectGuiManager*>(Context);
        bioWorldInfo = guiManager->oBioWorldInfo;
        LOGFORMAT("BioWorldInfo found");
    }

    if (auto result = (bool)USFXSaveGame_LoadPlayer->Matches(Context, Function) | (bool)SFXGameViewportClient_GameSessionEnded->Matches(Context, Function) * 2 ; result)
    {
        convstate = ConvState::WaitForConversation;
        pausestate = PauseState::Unpaused;
        bioWorldInfo = nullptr;
        currentbc = 0;
        bcs = {};
        node = nullptr;
        if (result > 1)
            LOGFORMAT("Game session ended reset")
        else
            LOGFORMAT("Save loaded reset")
    }

    // minimum requirements
    if (!scene || !bioWorldInfo)
    {
        stoprecursion = false;
        return ProcessEvent_orig(Context, Function, Parms, Result);
    }

    // Capture conversations or nodes any time
    if (auto bc = UBioSeqAct_StartConversation_Activated->Matches(Context, Function); bc)
    {
        bcs[bc->Conv->m_nResRefID] = bc->Conv;
        LOGFORMAT("Start Conversation %s %s set size %d", bc->GetInstancedName(), Function->GetName(), bcs.size());
    }
    else if (auto bc = UBioSeqAct_StartConversation_Deactivated->Matches(Context, Function); bc)
    {
        bcs.erase(bc->Conv->m_nResRefID);
        if (!bcs.size() || currentbc == bc->Conv->m_nResRefID)
        {
            convstate = ConvState::WaitForConversation;
            currentbc = 0;
        }
        LOGFORMAT("End Conversation %s %s set size %d", bc->GetInstancedName(), Function->GetName(), bcs.size());
    }

    if (auto thisnode = UBioSeqEvt_ConvNode_Activated->Matches(Context, Function); thisnode)
    {
        if (bcs.find(thisnode->m_nConvResRefID) != bcs.end())
        {
            node = thisnode;
            currentbc = thisnode->m_nConvResRefID;
            convstate = ConvState::WaitForVoiceover;
            LOGFORMAT("ConvNode started %s - %s", bcs[currentbc]->GetInstancedName(), node->GetInstancedName());
        }
        else
        {
            // conversation creation isn't always captured
            if (isvalid_bc(bioWorldInfo->m_oCurrentConversation, thisnode->m_nConvResRefID))
            {
                node = thisnode;
                currentbc = bioWorldInfo->m_oCurrentConversation->m_nResRefID;
                bcs[currentbc] = bioWorldInfo->m_oCurrentConversation;
                convstate = ConvState::WaitForVoiceover;
                LOGFORMAT("Stray ConvNode started %s - %s", bcs[currentbc]->GetInstancedName(), node->GetInstancedName());
            }
        }
    }

    // capture animation sequences
    switch (convstate)
    {
    case ConvState::WaitForVoiceover:
        // link interp to conversation node
        if (auto thisinterp = USeqAct_Interp_Activated->Matches(Context, Function); thisinterp)
        {
            for (int i = 0; i < node->OutputLinks.Count; i++)
            {
                for (int j = 0; j < node->OutputLinks.Data[i].Links.Count; j++)
                {
                    if (node->OutputLinks.Data[i].Links.Data[j].LinkedOp == thisinterp && thisinterp->InterpData)
                    {
                        interp = thisinterp;
                        convstate = ConvState::ActiveVoiceover;
                        LOGFORMAT("Interp activated: %s length %f", interp->GetInstancedName(), (interp->InterpData ? interp->InterpData->InterpLength : -1.0));
                    }
                }
            }
        }
        break;

    case ConvState::ActiveVoiceover:
        if (auto thisinterp = USeqAct_Interp_Deactivated->Matches(Context, Function); thisinterp &&
            thisinterp == interp)
        {
            // skipped dialogue or conversation wheel choice
            LOGFORMAT("Interp deactivated %s", interp->GetInstancedName());
            convstate = ConvState::WaitForVoiceover;
            break;
        }
        break;
    }

    // input handling
    if (UXConsole_InputKey->Matches(Context, Function))
    {
        auto ikp = (UConsole_execInputKey_Parms*)Parms;
        auto keyname = ikp->Key.GetName();
        // event == 0 means initial press
        // event == 1 means continued hold
        auto event = ikp->Event;

        switch (pausestate)
        {
        case PauseState::Unpaused:
            if (!strcmp("F8", keyname) && event == 0)
            {
                pausestate = PauseState::Manual;
                scene->eventPauseGame(true, 0);
                LOGFORMAT("Manual pause");
                if (manualPauseHistory)
                    ReplaceDisplayedSubtitle();
            }
            break;

        case PauseState::Dialogue:
            if (!strcmp("H", keyname))
            {
                if (event == 0)
                {
                    if (!convPauseHistory)
                        ReplaceDisplayedSubtitle();
                    else
                        ResetDisplayedSubitle();
                    convPauseHistory = !convPauseHistory;
                }
            }
            else if (strcmp("F8", keyname) &&
                strcmp("LeftAlt", keyname) &&
                strcmp("RightAlt", keyname) &&
                strcmp("Tab", keyname))
            {
                // ignore event state and continue if keys are already down
                convstate = ConvState::WaitForNode;
                pausestate = PauseState::Unpaused;
                scene->eventPauseGame(false, 0);
                LOGFORMAT("Unpause %s", keyname);
            }
            break;

        case PauseState::Manual:
            if (!strcmp("H", keyname) && event == 0)
            {
                if (!manualPauseHistory)
                    ReplaceDisplayedSubtitle();
                else
                    ResetDisplayedSubitle();

                manualPauseHistory = !manualPauseHistory;
            }
            else if (!strcmp("F8", keyname) && event == 0)
            {
                pausestate = PauseState::Unpaused;
                ResetDisplayedSubitle();
                scene->eventPauseGame(false, 0);
            }
        }
    }
    
    // this function is called in 1st person and movie modes
    if (SFXPlayerCamera_UpdateCamera->Matches(Context, Function))
    {
#if defined GAMELE1
        // capture all subtitles
        if (guiManager->m_oConversationPanel &&
            guiManager->m_oConversationPanel->m_DefaultHandler)
        {
            if (auto lastsub = ((UBioSFHandler_Conversation*)guiManager->m_oConversationPanel->m_DefaultHandler)->m_oLastSubtitle; lastsub &&
                lastsub->m_sSubtitle.Count)
#else
        {
            if (auto lastsub = bioWorldInfo->m_Subtitles; lastsub &&
                lastsub->m_sSubtitle.Count)
#endif
            {
                if (lastsub->m_sSubtitle.Data != subhistory->laststr ||
                    lastsub->m_sSubtitle.Count != subhistory->lastsize)
                    subhistory->SaveSubtitle(new FString(lastsub->m_sSubtitle));
            }
        }
    }

    // post-frame functions
    if (auto thishud = ABioHUD_PostRender->Matches(Context, Function); thishud)
    {
        switch (pausestate)
        {
        case PauseState::Unpaused:
            if (convstate == ConvState::ActiveVoiceover)
            {
                // safety checks to prevent a crash, probably overkill
                if (bcs.find(currentbc) == bcs.end())
                {
                    LOGFORMAT("Conversation obj is missing");
                    currentbc = 0;
                }
                else if (!isvalid_bc(bcs[currentbc], currentbc))
                {
                    LOGFORMAT("Conversation obj invalid");
                    bcs.erase(currentbc);
                    currentbc = 0;
                }

                if (!currentbc)
                {
                    if (bioWorldInfo &&
                        isvalid_bc(bioWorldInfo->m_oCurrentConversation, node->m_nConvResRefID))
                    {
                        currentbc = node->m_nConvResRefID;
                        bcs[currentbc] = bioWorldInfo->m_oCurrentConversation;
                        LOGFORMAT("Recovered missing conversation obj from BioWorldInfo")
                    }
                    else
                    {
                        convstate = ConvState::WaitForConversation;
                        LOGFORMAT("Skipping interp %s due to missing conversation obj", interp->GetInstancedName());
                        break;
                    }
                }

                if (auto thisbc = bcs[currentbc]; 
                    GetKeyState(VK_SCROLL) & 1)
                {
                    if ((thisbc->m_EntryList.Count && thisbc->m_nCurrentEntry > -1 && thisbc->m_EntryList(thisbc->m_nCurrentEntry).ReplyListNew.Count < 2) ||
                        thisbc->m_nSelectedReply > -1)
                    {
                        if (thisbc->m_sCurrentSubTitle.Count &&
                            interp->InterpData &&
                            interp->InterpData->InterpLength)
                        {
                            if (interp->InterpData->InterpLength == interp->Position)
                            {
                                BYTE keystate[256];
                                GetKeyboardState(keystate);
                                for (int i = 0; i < 256; i++)
                                {
                                    // skip pause if any key is down
                                    if (keystate[i] & 0x80)
                                    {
                                        convstate = ConvState::WaitForVoiceover;
                                        break;
                                    }
                                    if (i == 255)
                                    {
                                        convstate = ConvState::Paused;
                                        pausestate = PauseState::Dialogue;
                                        scene->eventPauseGame(true, 0);
                                        LOGFORMAT("Pause");

                                        if (convPauseHistory)
                                            ReplaceDisplayedSubtitle();
                                    }
                                }
                            }
                            else
                                DrawHUDString(thishud->Canvas, (WCHAR*)wstring_format(L"%.*s", int(ceil(interp->InterpData->InterpLength - interp->Position)), L"....................").c_str());
                        }
                    }
                    else
                        DrawHUDString(thishud->Canvas, L"O", 1.75);
                }
            }
            break;

        case PauseState::Manual:
            if (manualPauseHistory)
                DrawHUDString(((ABioHUD*)Context)->Canvas, L"F8: Unpause\nH: Disable history");
            else
                DrawHUDString(((ABioHUD*)Context)->Canvas, L"F8: Unpause\nH: Enable history");
            break;

        case PauseState::Dialogue:
            if (convPauseHistory)
                DrawHUDString(((ABioHUD*)Context)->Canvas, L"Any key: Unpause\nH: Disable history");
            else
                DrawHUDString(((ABioHUD*)Context)->Canvas, L"Any key: Unpause\nH: Enable history");
            break;
        }
    }


    stoprecursion = false;
    ProcessEvent_orig(Context, Function, Parms, Result);
}


SPI_IMPLEMENT_ATTACH
{
#ifdef ASI_DEBUG
    Common::OpenConsole();
    logs = fopen(LOGNAME, "w");
#endif

#if 0
    while (!IsDebuggerPresent())
        Sleep(100);
#endif

    auto _ = SDKInitializer::Instance();

    WCHAR moduleName[2048];
    if (auto size = GetModuleFileName(NULL, moduleName, sizeof(moduleName)/sizeof(WCHAR));  size &&
        _wcsnicmp(moduleName + size - wcslen(EXENAME), EXENAME, wcslen(EXENAME))
    )
    {
        moduleName[size] = 0;
        MessageBox(NULL, wstring_format(L"Wrong game, expected executable name %s", EXENAME).c_str(), L"DialogPauser", MB_ICONSTOP);
        LOGFORMAT("DialoguePauser: Wrong game, expected executable name %s", EXENAME);
        throw runtime_error("DialoguePauser: Wrong game");
    }

#if defined GAMELE1
    UXConsole_InputKey = new FunctionName<UConsole>("InputKey");
#else
    UXConsole_InputKey = new FunctionName<USFXConsole>("InputKey");
#endif
    USFXSaveGame_LoadPlayer = new FunctionName<USFXSaveGame>("LoadPlayer");
    UBioSeqAct_StartConversation_Activated = new FunctionName<UBioSeqAct_StartConversation>("Activated");
    UBioSeqAct_StartConversation_Deactivated = new FunctionName<UBioSeqAct_StartConversation>("Deactivated");
    UBioSeqEvt_ConvNode_Activated = new FunctionName<UBioSeqEvt_ConvNode>("Activated");
    ABioHUD_PostRender = new FunctionName<ABioHUD>("PostRender");
    SFXPlayerCamera_UpdateCamera = new FunctionName<ASFXPlayerCamera>("UpdateCamera");
    USeqAct_Interp_Activated = new FunctionName<USeqAct_Interp>("Activated");
    USeqAct_Interp_Deactivated = new FunctionName<USeqAct_Interp>("Deactivated");
    SFXGameViewportClient_GameSessionEnded = new FunctionName<USFXGameViewportClient>("GameSessionEnded");

    // Hook ProcessEvent for Non-Native
    INIT_FIND_PATTERN_POSTHOOK(ProcessEvent, LE_PATTERN_POSTHOOK_PROCESSEVENT);
    INIT_HOOK_PATTERN(ProcessEvent);

    return true;
}

SPI_IMPLEMENT_DETACH
{
#ifdef ASI_DEBUG
    Common::CloseConsole();
    fclose(logs);
#endif

    return true;
}
