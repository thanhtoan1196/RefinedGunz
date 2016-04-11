#include "stdafx.h"
#include "ZApplication.h"
#include "ZFile.h"
#include "ZGameClient.h"
#include "ZReplay.h"
#include "ZGame.h"
#include "ZNetCharacter.h"
#include "ZMyCharacter.h"
#include "ZPost.h"
#include "MMatchUtil.h"
#include "ZRuleDuel.h"

#include "RGMain.h"
#include "ZReplay.inl"

bool g_bTestFromReplay = false;

bool CreateReplayGame(char *filename)
{
	static std::string LastFile;

	char szBuf[256];

	if (filename)
	{
		strcpy_safe(szBuf, filename);
		LastFile = szBuf;
	}
	else if (LastFile.size())
		strcpy_safe(szBuf, LastFile.c_str());
	else
		return false;

	ZReplayLoader loader;

	if (!loader.Load(szBuf))
		return false;

	g_pGame->OnLoadReplay(&loader);

	return true;

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
ZReplayLoader::ZReplayLoader() : m_fGameTime(0.0f)
{
	Version.Server = SERVER_NONE;
	Version.nVersion = 0;
	Version.nSubVersion = 0;
	memset(&m_StageSetting, 0, sizeof(REPLAY_STAGE_SETTING_NODE));
}

ZReplayLoader::~ZReplayLoader()
{
}

bool ZReplayLoader::Load(const char* filename)
{
	auto pair = ReadZFile(filename);

	if (!pair.first)
		return false;

	InflatedFile = std::move(pair.second);

	try
	{
		Version = GetVersion();

		auto VersionString = Version.GetVersionString();

		MLog("Replay header loaded -- %s\n", VersionString.c_str());

		GetStageSetting(m_StageSetting);

		ChangeGameState();

		LoadStageSettingEtc();

		CreatePlayers(GetCharInfo());

		auto PerCommand = [&](MCommand *Command, float Time)
		{
			ZObserverCommandItem *pZCommand = new ZObserverCommandItem;

			pZCommand->pCommand = Command;
			pZCommand->fTime = Time;

			g_pGame->GetReplayCommandList()->push_back(pZCommand);
		};

		GetCommands(PerCommand);
	}
	catch (EOFException& e)
	{
		MLog("Unexpected EOF while reading replay %s at position %d\n", filename, e.GetPosition());
		return true; // Try to play it
	}
	catch (...)
	{
		MLog("Something went wrong while reading replay %s\n", filename);
		return false;
	}

	/*pFile = zfopen(filename);
	
	if (!LoadHeader()) return false;
	if (!LoadStageSetting()) return false;
	ChangeGameState();

	if (!LoadStageSettingEtc()) return false;

	if (!LoadCharInfo()) return false;
	if (!LoadCommandStream()) return false;

	zfclose(pFile);*/

	return true;
}

void ZReplayLoader::ChangeGameState()
{
	MSTAGE_SETTING_NODE stageSetting;
	memset(&stageSetting, 0, sizeof(MSTAGE_SETTING_NODE));
	ConvertStageSettingNode(&m_StageSetting, &stageSetting);
	ZGetGameClient()->GetMatchStageSetting()->UpdateStageSetting(&stageSetting);
	ZApplication::GetStageInterface()->SetMapName(ZGetGameClient()->GetMatchStageSetting()->GetMapName());
	ZGetGameInterface()->SetState(GUNZ_GAME);
	ZGetCharacterManager()->Clear();
	ZGetObjectManager()->Clear();
}

bool ZReplayLoader::LoadHeader()
{
	unsigned int version = 0;
	unsigned int header;
	int nRead;

	char szServer[32] = "Unknown";
	bool bFoundServer = false;

	nRead = zfread(&header, sizeof(header), 1, pFile);
	if(nRead==0) return false;

	if (header == RG_REPLAY_MAGIC_NUMBER)
	{
		Version.Server = SERVER_REFINED_GUNZ;
		strcpy_safe(szServer, "Refined Gunz");
		bFoundServer = true;
	}
	else if(header != GUNZ_REC_FILE_ID)
	{
		Version.Server = SERVER_NONE;
		return false;
	}

	nRead = zfread(&version, sizeof(version), 1, pFile);
	if (!nRead)// || ( version > GUNZ_REC_FILE_VERSION))
		return false;

	Version.nVersion = version;

	if (!bFoundServer)
	{
		if (Version.nVersion >= 7 && Version.nVersion <= 9 && InflatedFile[0x4A] <= 0x01)
		{
			Version.Server = SERVER_FREESTYLE_GUNZ;
			strcpy_safe(szServer, "Freestyle Gunz");
		}
		else
		{
			Version.Server = SERVER_OFFICIAL;
			strcpy_safe(szServer, "Official");
		}
	}

	MLog("Replay header loaded -- Server: %s, version: %d\n", szServer, Version.nVersion);

	return true;
}

bool ZReplayLoader::LoadStageSettingEtc()
{
	if (Version.Server == SERVER_OFFICIAL && Version.nVersion < 4)
		return true;

	if(m_StageSetting.nGameType==MMATCH_GAMETYPE_DUEL)
	{
		ZRuleDuel* pDuel = (ZRuleDuel*)ZGetGameInterface()->GetGame()->GetMatch()->GetRule();
		/*int nRead = zfread(&pDuel->QInfo,sizeof(MTD_DuelQueueInfo), 1, pFile);
		if(nRead==0) return false;*/
		Read(pDuel->QInfo);
	}

	return true;
}

#define COPY_CHARINFO(member) info.member = oldinfo.member
template<typename T>
static void copy_charinfo(MTD_CharInfo &info, const T& oldinfo)
{
	strcpy_safe(info.szName, oldinfo.szName);
	strcpy_safe(info.szClanName, oldinfo.szClanName);
	COPY_CHARINFO(nClanGrade);
	COPY_CHARINFO(nClanContPoint);
	COPY_CHARINFO(nCharNum);
	COPY_CHARINFO(nLevel);
	COPY_CHARINFO(nSex);
	COPY_CHARINFO(nHair);
	COPY_CHARINFO(nFace);
	COPY_CHARINFO(nXP);
	COPY_CHARINFO(nBP);
	COPY_CHARINFO(fBonusRate);
	COPY_CHARINFO(nPrize);
	COPY_CHARINFO(nHP);
	COPY_CHARINFO(nAP);
	COPY_CHARINFO(nMaxWeight);
	COPY_CHARINFO(nSafeFalls);
	COPY_CHARINFO(nFR);
	COPY_CHARINFO(nCR);
	COPY_CHARINFO(nER);
	COPY_CHARINFO(nWR);
	for (int i = 0; i < min(MMCIP_END, sizeof(oldinfo.nEquipedItemDesc) / sizeof(oldinfo.nEquipedItemDesc[0])); i++)
		COPY_CHARINFO(nEquipedItemDesc[i]);
	COPY_CHARINFO(nUGradeID);
};
#undef COPY_CHARINFO

bool ZReplayLoader::LoadCharInfo()
{
	int nRead;

	// character info
	int nCharacterCount;
	zfread(&nCharacterCount, sizeof(nCharacterCount), 1, pFile);

	for(int i = 0; i < nCharacterCount; i++)
	{
		bool bHero;
		nRead = zfread(&bHero, sizeof(bHero), 1, pFile);
		if(nRead != 1) return false;

		MTD_CharInfo info;

		if (Version.Server == SERVER_OFFICIAL)
		{
			if (Version.nVersion <= 5)
			{
				MTD_CharInfo_V5 oldinfo;
				if (Version.nVersion < 2)
				{
					nRead = zfread(&oldinfo, sizeof(oldinfo)-4, 1, pFile);
					if (nRead != 1) return false;
					oldinfo.nClanCLID = 0;
				}
				else
				{
					nRead = zfread(&oldinfo, sizeof(oldinfo), 1, pFile);
					if (nRead != 1) return false;
				}
				copy_charinfo(info, oldinfo);
			}
			else if (Version.nVersion == 6)
			{
				MTD_CharInfo_V6 oldinfo;
				pFile->Read(oldinfo);
				copy_charinfo(info, oldinfo);
			}
			else if (Version.nVersion == 11)
			{
				MTD_CharInfo_V11 oldinfo;
				pFile->Read(oldinfo);
				copy_charinfo(info, oldinfo);
			}
		}
		else if (Version.Server == SERVER_FREESTYLE_GUNZ)
		{
			if (Version.nVersion == 9)
			{
				MTD_CharInfo_FG_V9 charinfo;
				pFile->Read(charinfo);
				copy_charinfo(info, charinfo);
			}
			else if (Version.nVersion == 7)
			{
				if (Version.nSubVersion == 0)
				{
					MTD_CharInfo_FG_V7_0 charinfo;
					pFile->Read(charinfo);
					copy_charinfo(info, charinfo);
				}
				else if (Version.nSubVersion == 1)
				{
					MTD_CharInfo_FG_V7_1 charinfo;
					pFile->Read(charinfo);
					copy_charinfo(info, charinfo);
				}
			}

			//MLog("HP/AP: %08X/%08X\n", info.nHP, info.nAP);

			info.nEquipedItemDesc[MMCIP_MELEE] = 2;
		}
		else if(Version.Server == SERVER_REFINED_GUNZ)
		{
			nRead = zfread(&info, sizeof(info), 1, pFile);
			if(nRead != 1) return false;
		}

		ZCharacter* pChar=NULL;
		if(bHero)
		{
			g_pGame->m_pMyCharacter=new ZMyCharacter;
			g_pGame->CreateMyCharacter(info);
			pChar=g_pGame->m_pMyCharacter;
			pChar->Load(pFile, Version);
		}else
		{
			pChar=new ZNetCharacter;
			pChar->Load(pFile, Version);
			pChar->Create(info);
		}
		ZGetCharacterManager()->Add(pChar);

		pChar->SetVisible(true);
	}

	return true;
}

void ZReplayLoader::CreatePlayers(const std::vector<ReplayPlayerInfo>& Players)
{
	for (auto& Player : Players)
	{
		ZCharacter* Char = nullptr;

		if (Player.IsHero)
		{
			g_pGame->m_pMyCharacter = new ZMyCharacter;
			g_pGame->CreateMyCharacter(Player.Info);
			Char = g_pGame->m_pMyCharacter;
			Char->Load(Player.State);
		}
		else
		{
			Char = new ZNetCharacter;
			Char->Load(Player.State);
			Char->Create(Player.Info);
		}
		ZGetCharacterManager()->Add(Char);

		Char->SetVisible(true);
	}
}

bool ZReplayLoader::LoadCommandStream()
{
	float fGameTime;
	zfread(&fGameTime, sizeof(fGameTime), 1, pFile);
	m_fGameTime = fGameTime;

	int nCommandCount=0;

	int nSize;
	float fTime;
	while (zfread(&fTime, sizeof(fTime), 1, pFile))
	{
		nCommandCount++;

		char CommandBuffer[1024];

		MUID uidSender;
		zfread(&uidSender, sizeof(uidSender), 1, pFile);
		zfread(&nSize, sizeof(nSize), 1, pFile);

		if(nSize<=0 || nSize>sizeof(CommandBuffer)) {
			return false;
		}
		zfread(CommandBuffer, nSize, 1, pFile);


		ZObserverCommandItem *pZCommand = new ZObserverCommandItem;

		if (!CreateCommandFromStream(Version, CommandBuffer, &pZCommand->pCommand))
			continue;

		pZCommand->pCommand->m_Sender=uidSender;

		pZCommand->fTime=fTime;

		if (Version.Server == SERVER_FREESTYLE_GUNZ && IsDojo)
		{
			auto Transform = [](float pos[3])
			{
				pos[0] -= 600;
				pos[1] += 2800;
				pos[2] += 400;
			};

			if (pZCommand->pCommand->GetID() == MC_PEER_BASICINFO)
			{
				MCommandParameter* pParam = pZCommand->pCommand->GetParameter(0);
				if (pParam->GetType() != MPT_BLOB)
					continue;

				ZPACKEDBASICINFO* ppbi = (ZPACKEDBASICINFO*)pParam->GetPointer();

				float pos[3] = { (float)ppbi->posx, (float)ppbi->posy, (float)ppbi->posz };

				if (pos[2] < 0)
				{
					Transform(pos);

					ppbi->posx = pos[0];
					ppbi->posy = pos[1];
					ppbi->posz = pos[2];
				}
			}
		}
		else if (Version.Server == SERVER_OFFICIAL && Version.nVersion == 11)
		{
			if (pZCommand->pCommand->GetID() == MC_PEER_BASICINFO)
			{
				MCommandParameter* pParam = pZCommand->pCommand->GetParameter(0);
				if (pParam->GetType() != MPT_BLOB)
					continue;

				BYTE *ppbi = (BYTE *)pParam->GetPointer();

				for (int i = 0; i < 3; i++)
				{
					ppbi[22 + i] = ppbi[28 + i];
				}
			}
		}

		g_pGame->GetReplayCommandList()->push_back(pZCommand);
	}

	return true;
}


void ZReplayLoader::ConvertStageSettingNode(REPLAY_STAGE_SETTING_NODE* pSource, MSTAGE_SETTING_NODE* pTarget)
{
	pTarget->uidStage = pSource->uidStage;
	strcpy_safe(pTarget->szStageName, pSource->szStageName);
	strcpy_safe(pTarget->szMapName, pSource->szMapName);
	pTarget->nMapIndex = pSource->nMapIndex;
	pTarget->nGameType = pSource->nGameType;
	pTarget->nRoundMax = pSource->nRoundMax;
	pTarget->nLimitTime = pSource->nLimitTime;
	pTarget->nLimitLevel = pSource->nLimitLevel;
	pTarget->nMaxPlayers = pSource->nMaxPlayers;
	pTarget->bTeamKillEnabled = pSource->bTeamKillEnabled;
	pTarget->bTeamWinThePoint = pSource->bTeamWinThePoint;
	pTarget->bForcedEntryEnabled = pSource->bForcedEntryEnabled;
}

bool ZReplayLoader::CreateCommandFromStream(const ReplayVersion& Version, char* pStream, MCommand **ppRetCommand)
{
	if (Version.Server == SERVER_OFFICIAL && Version.nVersion <= 2)
	{
		*ppRetCommand = CreateCommandFromStreamVersion2(pStream);
		return true;
	}

	bool ReadSerial = !(Version.Server == SERVER_OFFICIAL && Version.nVersion == 11);

	MCommand* pCommand = new MCommand;
	if (!pCommand->SetData(pStream, ZGetGameClient()->GetCommandManager(), 65535, ReadSerial))
	{
		delete pCommand;
		*ppRetCommand = nullptr;
		return false;
	}

	*ppRetCommand = pCommand;
	return true;
}


MCommand* ZReplayLoader::CreateCommandFromStreamVersion2(char* pStream)
{
	MCommandManager* pCM = ZGetGameClient()->GetCommandManager();

	MCommand* pCommand = new MCommand;
	
	BYTE nParamCount = 0;
	unsigned short int nDataCount = 0;

	// Get Total Size
	unsigned short nTotalSize = 0;
	memcpy(&nTotalSize, pStream, sizeof(nTotalSize));
	nDataCount += sizeof(nTotalSize);

	// Command
	unsigned short int nCommandID = 0;
	memcpy(&nCommandID, pStream+nDataCount, sizeof(nCommandID));
	nDataCount += sizeof(nCommandID);

	MCommandDesc* pDesc = pCM->GetCommandDescByID(nCommandID);
	if (pDesc == NULL)
	{
		mlog("Error(MCommand::SetData): Wrong Command ID(%d)\n", nCommandID);
		_ASSERT(0);

		return pCommand;
	}
	pCommand->SetID(pDesc);

	if (ParseVersion2Command(pStream+nDataCount, pCommand))
	{
		return pCommand;
	}

	// Parameters
	memcpy(&nParamCount, pStream+nDataCount, sizeof(nParamCount));
	nDataCount += sizeof(nParamCount);
	for(int i=0; i<nParamCount; i++)
	{
		BYTE nType;
		memcpy(&nType, pStream+nDataCount, sizeof(BYTE));
		nDataCount += sizeof(BYTE);

		MCommandParameter* pParam = MakeVersion2CommandParameter((MCommandParameterType)nType, pStream, &nDataCount);
		if (pParam == NULL) return false;
		
		pCommand->m_Params.push_back(pParam);
	}

	return pCommand;
}

bool ZReplayLoader::ParseVersion2Command(char* pStream, MCommand* pCmd)
{
	switch (pCmd->GetID())
	{
	case MC_PEER_HPINFO:
	case MC_PEER_HPAPINFO:
	case MC_MATCH_OBJECT_CACHE:
	case MC_MATCH_STAGE_ENTERBATTLE:
	case MC_MATCH_STAGE_LIST:
	case MC_MATCH_CHANNEL_RESPONSE_PLAYER_LIST:
	case MC_MATCH_GAME_RESPONSE_SPAWN:
	case MC_PEER_DASH:
	case MC_MATCH_BRIDGEPEER:
	case MC_MATCH_SPAWN_WORLDITEM:
		{

		}
		break;
	default:
		return false;
	};

	BYTE nParamCount = 0;
	unsigned short int nDataCount = 0;
	vector<MCommandParameter*> TempParams;

	// Count
	memcpy(&nParamCount, pStream+nDataCount, sizeof(nParamCount));
	nDataCount += sizeof(nParamCount);

	for(int i=0; i<nParamCount; i++)
	{
		BYTE nType;
		memcpy(&nType, pStream+nDataCount, sizeof(BYTE));
		nDataCount += sizeof(BYTE);

		MCommandParameter* pParam = MakeVersion2CommandParameter((MCommandParameterType)nType, pStream, &nDataCount);
		if (pParam == NULL) return false;
		
		TempParams.push_back(pParam);
	}


	switch (pCmd->GetID())
	{
	case MC_PEER_HPAPINFO:
		{
			void* pBlob = TempParams[1]->GetPointer();
			struct REPLAY2_HP_AP_INFO 
			{
				MUID muid;
				float fHP;
				float fAP;
			};

			REPLAY2_HP_AP_INFO* pBlobData = (REPLAY2_HP_AP_INFO*)MGetBlobArrayElement(pBlob, 0);
			pCmd->AddParameter(new MCmdParamFloat(pBlobData->fHP));
			pCmd->AddParameter(new MCmdParamFloat(pBlobData->fAP));
		}
		break;
	case MC_PEER_HPINFO:
		{
			void* pBlob = TempParams[1]->GetPointer();
			struct REPLAY2_HP_INFO 
			{
				MUID muid;
				float fHP;
			};

			REPLAY2_HP_INFO* pBlobData = (REPLAY2_HP_INFO*)MGetBlobArrayElement(pBlob, 0);
			pCmd->AddParameter(new MCmdParamFloat(pBlobData->fHP));
		}
		break;
	case MC_MATCH_OBJECT_CACHE:
		{
			unsigned int nType;
			TempParams[0]->GetValue(&nType);
			MCmdParamBlob* pBlobParam = ((MCmdParamBlob*)TempParams[1])->Clone();

			pCmd->AddParameter(new MCmdParamUChar((unsigned char)nType));
			pCmd->AddParameter(pBlobParam);
		}
		break;
	case MC_MATCH_STAGE_ENTERBATTLE:
		{
			MUID uidPlayer, uidStage;
			int nParam;
			
			TempParams[0]->GetValue(&uidPlayer);
			TempParams[1]->GetValue(&uidStage);
			TempParams[2]->GetValue(&nParam);

			struct REPLAY2_ExtendInfo
			{
				char			nTeam;
				unsigned char	nPlayerFlags;
				unsigned char	nReserved1;
				unsigned char	nReserved2;
			};

			struct REPLAY2_PeerListNode
			{
				MUID				uidChar;
				char				szIP[64];
				unsigned int		nPort;
				MTD_CharInfo		CharInfo;
				REPLAY2_ExtendInfo	ExtendInfo;
			};


			void* pBlob = TempParams[3]->GetPointer();
			//int nCount = MGetBlobArrayCount(pBlob);
			REPLAY2_PeerListNode* pNode = (REPLAY2_PeerListNode*)MGetBlobArrayElement(pBlob, 0);


			void* pNewBlob = MMakeBlobArray(sizeof(MTD_PeerListNode), 1);
			MTD_PeerListNode* pNewNode = (MTD_PeerListNode*)MGetBlobArrayElement(pNewBlob, 0);
			pNewNode->uidChar = pNode->uidChar;
			pNewNode->dwIP = inet_addr(pNode->szIP);
			pNewNode->nPort = pNode->nPort;
			memcpy(&pNewNode->CharInfo, &pNode->CharInfo, sizeof(MTD_CharInfo));
			pNewNode->ExtendInfo.nTeam = pNode->ExtendInfo.nTeam;
			pNewNode->ExtendInfo.nPlayerFlags = pNode->ExtendInfo.nPlayerFlags;
			pNewNode->ExtendInfo.nReserved1 = pNode->ExtendInfo.nReserved1;
			pNewNode->ExtendInfo.nReserved2 = pNode->ExtendInfo.nReserved1;
			

			pCmd->AddParameter(new MCmdParamUChar((unsigned char)nParam));
			pCmd->AddParameter(new MCommandParameterBlob(pNewBlob, MGetBlobArraySize(pNewBlob)));

			MEraseBlobArray(pNewBlob);
		}
		break;
	case MC_MATCH_STAGE_LIST:
		{
			_ASSERT(0);
		}
		break;
	case MC_MATCH_CHANNEL_RESPONSE_PLAYER_LIST:
		{
			_ASSERT(0);
		}
		break;
	case MC_MATCH_GAME_RESPONSE_SPAWN:
		{
			MUID uidChar;
			rvector pos, dir;

			TempParams[0]->GetValue(&uidChar);
			TempParams[1]->GetValue(&pos);
			TempParams[2]->GetValue(&dir);

			pCmd->AddParameter(new MCmdParamUID(uidChar));
			pCmd->AddParameter(new MCmdParamShortVector(pos.x, pos.y, pos.z));
			pCmd->AddParameter(new MCmdParamShortVector(DirElementToShort(dir.x), DirElementToShort(dir.y), DirElementToShort(dir.z)));
		}
		break;
	case MC_PEER_DASH:
		{
			rvector pos, dir;
			int nSelType;

			TempParams[0]->GetValue(&pos);
			TempParams[1]->GetValue(&dir);
			TempParams[2]->GetValue(&nSelType);

			ZPACKEDDASHINFO pdi;
			pdi.posx = Roundf(pos.x);
			pdi.posy = Roundf(pos.y);
			pdi.posz = Roundf(pos.z);

			pdi.dirx = DirElementToShort(dir.x);
			pdi.diry = DirElementToShort(dir.y);
			pdi.dirz = DirElementToShort(dir.z);

			pdi.seltype = (BYTE)nSelType;

			pCmd->AddParameter(new MCommandParameterBlob(&pdi,sizeof(ZPACKEDDASHINFO)));
		}
		break;
	case MC_MATCH_SPAWN_WORLDITEM:
		{
			struct REPLAY2_WorldItem
			{
				unsigned short	nUID;
				unsigned short	nItemID;
				unsigned short  nItemSubType;
				float			x;
				float			y;
				float			z;
			};


			void* pBlob = TempParams[0]->GetPointer();
			int nCount = MGetBlobArrayCount(pBlob);

			void* pNewBlob = MMakeBlobArray(sizeof(MTD_WorldItem), nCount);

			for (int i = 0; i < nCount; i++)
			{
				REPLAY2_WorldItem* pNode = (REPLAY2_WorldItem*)MGetBlobArrayElement(pBlob, i);
				MTD_WorldItem* pNewNode = (MTD_WorldItem*)MGetBlobArrayElement(pNewBlob, i);

				pNewNode->nUID = pNode->nUID;
				pNewNode->nItemID = pNode->nItemID;
				pNewNode->nItemSubType = pNode->nItemSubType;
				pNewNode->x = (short)Roundf(pNode->x);
				pNewNode->y = (short)Roundf(pNode->y);
				pNewNode->z = (short)Roundf(pNode->z);
			}
			pCmd->AddParameter(new MCommandParameterBlob(pNewBlob, MGetBlobArraySize(pNewBlob)));
			MEraseBlobArray(pNewBlob);

		}
		break;
	case MC_MATCH_BRIDGEPEER:
		{
			_ASSERT(0);
		}
		break;
	};


	for(int i=0; i<(int)TempParams.size(); i++){
		delete TempParams[i];
	}
	TempParams.clear();


	return true;
}


MCommandParameter* ZReplayLoader::MakeVersion2CommandParameter(MCommandParameterType nType, char* pStream, unsigned short int* pnDataCount)
{
	MCommandParameter* pParam = NULL;

	switch(nType) 
	{
	case MPT_INT:
		pParam = new MCommandParameterInt;
		break;
	case MPT_UINT:
		pParam = new MCommandParameterUInt;
		break;
	case MPT_FLOAT:
		pParam = new MCommandParameterFloat;
		break;
	case MPT_STR:
		{
			pParam = new MCommandParameterString;
			MCommandParameterString* pStringParam = (MCommandParameterString*)pParam;

			char* pStreamData = pStream+ *pnDataCount;

			int nValueSize = 0;
			memcpy(&nValueSize, pStreamData, sizeof(nValueSize));
			pStringParam->m_Value = new char[nValueSize];
			memcpy(pStringParam->m_Value, pStreamData+sizeof(nValueSize), nValueSize);
			int nParamSize = nValueSize+sizeof(nValueSize);

			*pnDataCount += nParamSize;
			return pParam;
		}
		break;
	case MPT_VECTOR:
		pParam = new MCommandParameterVector;
		break;
	case MPT_POS:
		pParam = new MCommandParameterPos;
		break;
	case MPT_DIR:
		pParam = new MCommandParameterDir;
		break;
	case MPT_BOOL:
		pParam = new MCommandParameterBool;
		break;
	case MPT_COLOR:
		pParam = new MCommandParameterColor;
		break;
	case MPT_UID:
		pParam = new MCommandParameterUID;
		break;
	case MPT_BLOB:
		pParam = new MCommandParameterBlob;
		break;
	case MPT_CHAR:
		pParam = new MCommandParameterChar;
		break;
	case MPT_UCHAR:
		pParam = new MCommandParameterUChar;
		break;
	case MPT_SHORT:
		pParam = new MCommandParameterShort;
		break;
	case MPT_USHORT:
		pParam = new MCommandParameterUShort;
		break;
	case MPT_INT64:
		pParam = new MCommandParameterInt64;
		break;
	case MPT_UINT64:
		pParam = new MCommandParameterUInt64;
		break;
	default:
		mlog("Error(MCommand::SetData): Wrong Param Type\n");
		_ASSERT(false);		// Unknow Parameter!!!
		return NULL;
	}

	*pnDataCount += pParam->SetData(pStream+ *pnDataCount);

	return pParam;
}